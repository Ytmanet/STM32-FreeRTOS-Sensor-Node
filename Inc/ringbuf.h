/**
 ******************************************************************************
 * @file    ringbuf.h
 * @brief   环形缓冲区: 单生产者(中断) + 单消费者(任务)
 *          生产者和消费者各有一个指针, 互不读写对方指针, 因此无需加锁
 ******************************************************************************
 */
#ifndef __RINGBUF_H
#define __RINGBUF_H

#include <stdint.h>

#define RINGBUF_SIZE    256     /* 缓冲区大小, 必须为 2 的幂 (用于 & (SIZE-1) 取模) */

typedef struct
{
    volatile uint16_t head;             /* 写入位置, 只由生产者(中断)修改 */
    volatile uint16_t tail;             /* 读取位置, 只由消费者(任务)修改 */
    uint8_t buf[RINGBUF_SIZE];
} RingBuf;

void rb_init(RingBuf *rb);
int  rb_write(RingBuf *rb, uint8_t byte);   /* 返回 1=成功, 0=缓冲区满 */
int  rb_read(RingBuf *rb, uint8_t *byte);   /* 返回 1=读到数据, 0=缓冲区空 */
uint16_t rb_count(const RingBuf *rb);

#endif /* __RINGBUF_H */
