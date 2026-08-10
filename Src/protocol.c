/**
 ******************************************************************************
 * @file    protocol.c
 * @brief   协议状态机: 字节流 -> 完整帧
 *
 * 三态状态机:
 *   FRAME_IDLE    -> 等 0xAA 帧头
 *   FRAME_HEADER  -> 收 ID+CMD+LEN (3 字节)
 *   FRAME_PAYLOAD -> 按 LEN 收 DATA, 再收 2 字节 CRC, 校验
 * 任何一帧出错(CRC/LEN)都直接丢弃, 回到 IDLE 等下一帧头, 实现"自动同步"
 ******************************************************************************
 */
#include "protocol.h"
#include "crc16.h"
#include <string.h>

enum { FRAME_IDLE, FRAME_HEADER, FRAME_PAYLOAD };

void proto_init(ProtoParser *p)
{
    p->state = FRAME_IDLE;
    p->idx = 0;
}

/**
 * @brief 喂一个字节进状态机
 * @retval PROTO_NEED_MORE / PROTO_FRAME_OK / PROTO_CRC_ERR / PROTO_LEN_ERR
 */
ProtoStatus proto_feed(ProtoParser *p, uint8_t byte)
{
    ProtoStatus st = PROTO_NEED_MORE;

    switch (p->state)
    {
        case FRAME_IDLE:    /* 等帧头 */
            if (byte == PROTO_HEAD)
            {
                p->buf[0] = byte;
                p->idx = 1;
                p->state = FRAME_HEADER;
            }
            break;

        case FRAME_HEADER:  /* 收 ID + CMD + LEN */
            p->buf[p->idx++] = byte;
            if (p->idx == 4)
            {
                p->data_len = p->buf[3];
                if (p->data_len > PROTO_MAX_DATA_LEN)
                {
                    p->state = FRAME_IDLE;      /* 长度非法, 丢弃 */
                    st = PROTO_LEN_ERR;
                }
                else
                {
                    p->state = FRAME_PAYLOAD;
                }
            }
            break;

        case FRAME_PAYLOAD: /* 收 DATA + CRC */
            p->buf[p->idx++] = byte;
            if (p->idx == (uint8_t)(4 + p->data_len + 2))
            {
                uint16_t crc_calc;
                uint16_t crc_rx = (uint16_t)(p->buf[p->idx - 2] | (p->buf[p->idx - 1] << 8));

                p->state = FRAME_IDLE;
                crc_calc = crc16_modbus(&p->buf[1], (uint16_t)(3 + p->data_len));

                if (crc_calc == crc_rx)
                {
                    p->id = p->buf[1];
                    p->cmd = p->buf[2];
                    p->data = &p->buf[4];
                    st = PROTO_FRAME_OK;
                }
                else
                {
                    st = PROTO_CRC_ERR;         /* 校验失败, 丢弃 */
                }
            }
            break;
    }

    return st;
}

/**
 * @brief 组帧: 0xAA + ID + CMD + LEN + DATA + CRC16(低字节在前)
 * @retval 帧总长度
 */
uint16_t proto_build(uint8_t id, uint8_t cmd, const uint8_t *data, uint8_t len, uint8_t *out)
{
    uint16_t crc;

    out[0] = PROTO_HEAD;
    out[1] = id;
    out[2] = cmd;
    out[3] = len;

    if (len > 0)
    {
        memcpy(&out[4], data, len);
    }

    crc = crc16_modbus(&out[1], (uint16_t)(3 + len));
    out[4 + len] = (uint8_t)(crc & 0xFF);       /* CRC 低字节在前 */
    out[5 + len] = (uint8_t)(crc >> 8);

    return (uint16_t)(6 + len);
}
