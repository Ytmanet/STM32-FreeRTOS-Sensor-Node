/**
 ******************************************************************************
 * @file    protocol.h
 * @brief   自定义通信协议: 帧格式与解析状态机
 *
 * 帧格式: | 0xAA | ID | CMD | LEN | DATA[LEN] | CRC16(2B, 低字节在前) |
 * CRC 覆盖: ID + CMD + LEN + DATA
 ******************************************************************************
 */
#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include <stdint.h>

#define PROTO_HEAD           0xAA        /* 帧头 */
#define PROTO_MAX_DATA_LEN   64          /* DATA 最大长度 */

/* 命令定义 */
#define PROTO_CMD_REPORT     0x01        /* 主动上报传感器数据 (上行) */
#define PROTO_CMD_SET_PARAM  0x02        /* 设置参数 (下行) */
#define PROTO_CMD_QUERY      0x03        /* 查询设备信息 (下行) */
#define PROTO_CMD_SET_TIME   0x04        /* 校时 (下行): DATA=YY(2B) MM DD HH MM SS */

/* 参数 ID (CMD 0x02 用) */
#define PROTO_PARAM_INTERVAL 0x01        /* 采样周期 ms: DATA[1]高字节 DATA[2]低字节 */
#define PROTO_PARAM_DEVID    0x02        /* 设备 ID: DATA[1] */

typedef enum
{
    PROTO_NEED_MORE = 0,    /* 帧未收全, 继续喂字节 */
    PROTO_FRAME_OK,         /* 收到完整帧且 CRC 正确 */
    PROTO_CRC_ERR,          /* 帧完整但 CRC 校验失败 (应丢弃) */
    PROTO_LEN_ERR,          /* LEN 超限 (应丢弃) */
} ProtoStatus;

typedef struct
{
    uint8_t state;          /* 状态机当前状态 */
    uint8_t buf[PROTO_MAX_DATA_LEN + 6];    /* AA+ID+CMD+LEN+DATA+CRC */
    uint8_t idx;            /* 已收到的字节数 */
    uint8_t data_len;       /* 帧头声明的 DATA 长度 */
    uint8_t id;             /* 解析出的设备 ID */
    uint8_t cmd;            /* 解析出的命令 */
    uint8_t *data;          /* 指向 DATA 起始 (仅 PROTO_FRAME_OK 后有效) */
} ProtoParser;

void proto_init(ProtoParser *p);
ProtoStatus proto_feed(ProtoParser *p, uint8_t byte);

/* 组帧: 把 ID+CMD+DATA 打包成完整帧(含 CRC)写到 out, 返回帧总长度 */
uint16_t proto_build(uint8_t id, uint8_t cmd, const uint8_t *data, uint8_t len, uint8_t *out);

#endif /* __PROTOCOL_H */
