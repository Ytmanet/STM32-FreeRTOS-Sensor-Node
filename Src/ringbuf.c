/**
 ******************************************************************************
 * @file    ringbuf.c
 * @brief   环形缓冲区实现
 *
 * 空/满判断: 留一个空位区分空和满
 *   空: head == tail
 *   满: (head + 1) % SIZE == tail
 ******************************************************************************
 */
#include "ringbuf.h"

void rb_init(RingBuf *rb)
{
    rb->head = 0;
    rb->tail = 0;
}

int rb_write(RingBuf *rb, uint8_t byte)
{
    uint16_t next = (uint16_t)((rb->head + 1) & (RINGBUF_SIZE - 1));

    if (next == rb->tail)       /* 满 */
    {
        return 0;
    }

    rb->buf[rb->head] = byte;
    rb->head = next;
    return 1;
}

int rb_read(RingBuf *rb, uint8_t *byte)
{
    if (rb->head == rb->tail)   /* 空 */
    {
        return 0;
    }

    *byte = rb->buf[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1) & (RINGBUF_SIZE - 1));
    return 1;
}

uint16_t rb_count(const RingBuf *rb)
{
    return (uint16_t)((rb->head - rb->tail) & (RINGBUF_SIZE - 1));
}
