/*
 * ring_buffer.c
 * ---------------------------------------------------------------------------
 * SPSC 无锁环形缓冲实现。中断（生产者）与线程（消费者）之间不需要临界区，
 * 只需要在更新索引前后插入内存屏障，保证数据先于索引可见。
 */
#include <string.h>
#include "ring_buffer.h"

#if defined(__GNUC__)
#define RB_BARRIER()  __atomic_thread_fence(__ATOMIC_SEQ_CST)
#else
#define RB_BARRIER()  do { } while (0)
#endif

int rb_is_power_of_two(uint32_t v)
{
    if ((v == 0u) || ((v & (v - 1u)) != 0u)) {
        return 0;
    }
    return 1;
}

int rb_init(ring_buffer_t *rb, void *buf, uint32_t slot_size, uint32_t capacity)
{
    if ((rb == 0) || (buf == 0) || (slot_size == 0u)) {
        return -1;
    }
    if (rb_is_power_of_two(capacity) == 0) {
        return -2;
    }
    rb->buf = (uint8_t *)buf;
    rb->slot_size = slot_size;
    rb->capacity = capacity;
    rb->mask = capacity - 1u;
    rb->head = 0u;
    rb->tail = 0u;
    rb->pushes = 0u;
    rb->pops = 0u;
    rb->drops = 0u;
    rb->peak = 0u;
    return 0;
}

static uint8_t *slot_ptr(const ring_buffer_t *rb, uint32_t index)
{
    return &rb->buf[(index & rb->mask) * rb->slot_size];
}

int rb_push(ring_buffer_t *rb, const void *item)
{
    uint32_t head;
    uint32_t tail;
    uint32_t used;

    if ((rb == 0) || (item == 0)) {
        return -1;
    }
    head = rb->head;
    tail = rb->tail;
    used = head - tail;
    if (used >= rb->capacity) {
        rb->drops++;
        return 1;   /* 满 */
    }
    memcpy(slot_ptr(rb, head), item, rb->slot_size);
    RB_BARRIER();
    rb->head = head + 1u;
    if ((used + 1u) > rb->peak) {
        rb->peak = used + 1u;
    }
    rb->pushes++;
    return 0;
}

int rb_pop(ring_buffer_t *rb, void *item)
{
    uint32_t head;
    uint32_t tail;

    if (rb == 0) {
        return -1;
    }
    head = rb->head;
    tail = rb->tail;
    if (head == tail) {
        return 1;   /* 空 */
    }
    if (item != 0) {
        memcpy(item, slot_ptr(rb, tail), rb->slot_size);
    }
    RB_BARRIER();
    rb->tail = tail + 1u;
    rb->pops++;
    return 0;
}

int rb_peek(const ring_buffer_t *rb, void *item)
{
    if ((rb == 0) || (item == 0)) {
        return -1;
    }
    if (rb->head == rb->tail) {
        return 1;
    }
    memcpy(item, slot_ptr(rb, rb->tail), rb->slot_size);
    return 0;
}

uint32_t rb_count(const ring_buffer_t *rb)
{
    if (rb == 0) {
        return 0u;
    }
    return rb->head - rb->tail;
}

uint32_t rb_free_slots(const ring_buffer_t *rb)
{
    if (rb == 0) {
        return 0u;
    }
    return rb->capacity - (rb->head - rb->tail);
}

uint32_t rb_capacity(const ring_buffer_t *rb)
{
    return (rb == 0) ? 0u : rb->capacity;
}

void rb_reset(ring_buffer_t *rb)
{
    if (rb == 0) {
        return;
    }
    rb->head = 0u;
    rb->tail = 0u;
    rb->pushes = 0u;
    rb->pops = 0u;
    rb->drops = 0u;
    rb->peak = 0u;
}

int rb_validate(const ring_buffer_t *rb)
{
    if (rb == 0) {
        return -1;
    }
    if ((rb->buf == 0) || (rb->slot_size == 0u)) {
        return -2;
    }
    if (rb_is_power_of_two(rb->capacity) == 0) {
        return -3;
    }
    if ((rb->head - rb->tail) > rb->capacity) {
        return -4;   /* 索引非法：说明存在并发写冲突或内存被踩 */
    }
    return 0;
}
