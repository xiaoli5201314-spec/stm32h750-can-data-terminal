/*
 * test_ring_buffer.c
 * ---------------------------------------------------------------------------
 * SPSC 环形缓冲测试：初始化校验、容量语义、满/空、丢弃计数、
 * 生产者-消费者交替、峰值水位、结构自检。
 */
#include <string.h>
#include "test_framework.h"
#include "ring_buffer.h"

#define SLOT_WORDS 4u
#define CAP        8u

static uint32_t s_buf[CAP * SLOT_WORDS];

static void fill(uint32_t *slot, uint32_t v)
{
    uint32_t i;
    for (i = 0u; i < SLOT_WORDS; i++) {
        slot[i] = v * 10u + i;
    }
}

static int check(const uint32_t *slot, uint32_t v)
{
    uint32_t i;
    for (i = 0u; i < SLOT_WORDS; i++) {
        if (slot[i] != (v * 10u + i)) {
            return 0;
        }
    }
    return 1;
}

void test_ring_buffer(void)
{
    ring_buffer_t rb;
    uint32_t slot[SLOT_WORDS];
    uint32_t i;

    tf_suite_begin("SPSC 无锁环形缓冲");

    TF_ASSERT_EQ(rb_is_power_of_two(1u), 1);
    TF_ASSERT_EQ(rb_is_power_of_two(8u), 1);
    TF_ASSERT_EQ(rb_is_power_of_two(0u), 0);
    TF_ASSERT_EQ(rb_is_power_of_two(6u), 0);

    TF_ASSERT_EQ(rb_init(&rb, s_buf, sizeof(slot), CAP), 0);
    TF_ASSERT_EQ(rb_init(&rb, s_buf, sizeof(slot), 6u), -2);   /* 非 2 的幂 */
    TF_ASSERT_EQ(rb_init(0, s_buf, sizeof(slot), CAP), -1);
    TF_ASSERT_EQ(rb_init(&rb, 0, sizeof(slot), CAP), -1);

    TF_ASSERT_EQ(rb_init(&rb, s_buf, sizeof(slot), CAP), 0);
    TF_ASSERT_EQ(rb_capacity(&rb), CAP);
    TF_ASSERT_EQ(rb_count(&rb), 0u);
    TF_ASSERT_EQ(rb_free_slots(&rb), CAP);
    TF_ASSERT_EQ(rb_pop(&rb, slot), 1);       /* 空 */
    TF_ASSERT_EQ(rb_peek(&rb, slot), 1);

    /* 逐个压入直到满 */
    for (i = 0u; i < CAP; i++) {
        fill(slot, i + 1u);
        TF_ASSERT_EQ(rb_push(&rb, slot), 0);
    }
    TF_ASSERT_EQ(rb_count(&rb), CAP);
    TF_ASSERT_EQ(rb_free_slots(&rb), 0u);
    TF_ASSERT_EQ(rb.peak, CAP);
    TF_ASSERT_EQ((int)rb.drops, 0);

    /* 满时丢弃新数据并计数 */
    fill(slot, 99u);
    TF_ASSERT_EQ(rb_push(&rb, slot), 1);
    TF_ASSERT_EQ(rb_push(&rb, slot), 1);
    TF_ASSERT_EQ((int)rb.drops, 2);
    TF_ASSERT_EQ(rb_count(&rb), CAP);

    /* FIFO 顺序正确 */
    for (i = 0u; i < CAP; i++) {
        TF_ASSERT_EQ(rb_pop(&rb, slot), 0);
        TF_ASSERT(check(slot, i + 1u));
    }
    TF_ASSERT_EQ(rb_count(&rb), 0u);
    TF_ASSERT_EQ((int)rb.pops, (int)CAP);
    TF_ASSERT_EQ((int)rb.pushes, (int)CAP);

    /* 生产者-消费者交替：容量 8 的缓冲可以连续推进 100 次 */
    TF_ASSERT_EQ(rb_init(&rb, s_buf, sizeof(slot), CAP), 0);
    for (i = 0u; i < 100u; i++) {
        fill(slot, i);
        TF_ASSERT_EQ(rb_push(&rb, slot), 0);
        TF_ASSERT_EQ(rb_pop(&rb, slot), 0);
        TF_ASSERT(check(slot, i));
    }
    TF_ASSERT_EQ(rb_count(&rb), 0u);
    TF_ASSERT_EQ(rb_validate(&rb), 0);

    /* peek 不消费 */
    fill(slot, 7u);
    TF_ASSERT_EQ(rb_push(&rb, slot), 0);
    TF_ASSERT_EQ(rb_peek(&rb, slot), 0);
    TF_ASSERT(check(slot, 7u));
    TF_ASSERT_EQ(rb_count(&rb), 1u);
    TF_ASSERT_EQ(rb_pop(&rb, slot), 0);
    TF_ASSERT_EQ(rb_count(&rb), 0u);

    /* 结构自检能发现非法状态 */
    rb.head = 100u;
    rb.tail = 0u;
    rb.capacity = CAP;
    TF_ASSERT_EQ(rb_validate(&rb), -4);
    TF_ASSERT_EQ(rb_validate(0), -1);

    /* 复位 */
    TF_ASSERT_EQ(rb_init(&rb, s_buf, sizeof(slot), CAP), 0);
    fill(slot, 3u);
    TF_ASSERT_EQ(rb_push(&rb, slot), 0);
    rb_reset(&rb);
    TF_ASSERT_EQ(rb_count(&rb), 0u);
    TF_ASSERT_EQ((int)rb.pushes, 0);
    TF_ASSERT_EQ((int)rb.drops, 0);

    tf_suite_end();
}
