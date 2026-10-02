/*
 * ring_buffer.h
 * ---------------------------------------------------------------------------
 * 单生产者-单消费者（SPSC）无锁环形缓冲：用于"FDCAN 中断 -> 接收线程"、
 * "解析线程 -> 上报线程"之间的数据搬运，避免在中断里使用互斥量。
 *
 * 约束：
 *   - 容量必须是 2 的幂（用掩码代替取模）；
 *   - 生产者只写 head，消费者只写 tail，用内存屏障保证顺序；
 *   - 满时丢弃新数据（由上层分层缓存决定丢弃策略）。
 */
#ifndef H750_RING_BUFFER_H
#define H750_RING_BUFFER_H

#include <stdint.h>

typedef struct {
    volatile uint32_t head;        /* 生产者索引（只由生产者修改） */
    volatile uint32_t tail;        /* 消费者索引（只由消费者修改） */
    uint8_t          *buf;
    uint32_t          slot_size;
    uint32_t          capacity;    /* 2 的幂 */
    uint32_t          mask;
    volatile uint32_t pushes;
    volatile uint32_t pops;
    volatile uint32_t drops;       /* 因满而丢弃的次数 */
    volatile uint32_t peak;        /* 历史最大占用条目数 */
} ring_buffer_t;

int      rb_init(ring_buffer_t *rb, void *buf, uint32_t slot_size, uint32_t capacity);
int      rb_push(ring_buffer_t *rb, const void *item);
int      rb_pop(ring_buffer_t *rb, void *item);
int      rb_peek(const ring_buffer_t *rb, void *item);
uint32_t rb_count(const ring_buffer_t *rb);
uint32_t rb_free_slots(const ring_buffer_t *rb);
uint32_t rb_capacity(const ring_buffer_t *rb);
void     rb_reset(ring_buffer_t *rb);
int      rb_is_power_of_two(uint32_t v);
int      rb_validate(const ring_buffer_t *rb);

#endif /* H750_RING_BUFFER_H */
