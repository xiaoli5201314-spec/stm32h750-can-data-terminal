/*
 * test_mem_pool.c
 * ---------------------------------------------------------------------------
 * 定长内存池测试：初始化、分配/释放、耗尽、重复释放、野指针、
 * 结构完整性自检、峰值统计、负载清零与对齐。
 */
#include <string.h>
#include "test_framework.h"
#include "mem_pool.h"

#define BLOCKS 16u
#define PAYLOAD 48u

static uint8_t s_mem[BLOCKS * (PAYLOAD + MP_HDR_BYTES + 8u)];

void test_mem_pool(void)
{
    mem_pool_t p;
    void *blocks[BLOCKS];
    uint32_t i;
    int rc;

    tf_suite_begin("定长内存池（分配/释放/耗尽）");

    TF_ASSERT_EQ(mp_init(&p, s_mem, PAYLOAD, BLOCKS, "test"), 0);
    TF_ASSERT_EQ(mp_block_count(&p), BLOCKS);
    TF_ASSERT_EQ(mp_payload_bytes(&p), PAYLOAD);
    TF_ASSERT_EQ(mp_used(&p), 0u);
    TF_ASSERT_EQ(mp_free_blocks(&p), BLOCKS);
    TF_ASSERT_EQ(mp_init(0, s_mem, PAYLOAD, BLOCKS, "x"), -1);
    TF_ASSERT_EQ(mp_init(&p, 0, PAYLOAD, BLOCKS, "x"), -1);

    TF_ASSERT_EQ(mp_init(&p, s_mem, PAYLOAD, BLOCKS, "test"), 0);

    /* ---- 分配全部块 ---- */
    for (i = 0u; i < BLOCKS; i++) {
        blocks[i] = mp_alloc(&p);
        TF_ASSERT(blocks[i] != 0);
        TF_ASSERT_EQ(((uintptr_t)blocks[i] & 7u), 0u);      /* 8 字节对齐 */
        TF_ASSERT_EQ(mp_block_index(&p, blocks[i]), i);
        /* 分配时清零：便于协议解析确定性 */
        TF_ASSERT_EQ(((uint8_t *)blocks[i])[PAYLOAD - 1u], 0u);
    }
    TF_ASSERT_EQ(mp_used(&p), BLOCKS);
    TF_ASSERT_EQ(mp_free_blocks(&p), 0u);
    TF_ASSERT_EQ(mp_peak(&p), BLOCKS);
    TF_ASSERT_EQ(mp_check_integrity(&p), 0);

    /* ---- 耗尽：再分配返回空并计数 ---- */
    TF_ASSERT(mp_alloc(&p) == 0);
    TF_ASSERT(mp_alloc(&p) == 0);
    TF_ASSERT_EQ(mp_alloc_fail_count(&p), 2u);
    TF_ASSERT_EQ(mp_used(&p), BLOCKS);

    /* ---- 释放后可重新分配（优先复用刚释放的块） ---- */
    TF_ASSERT_EQ(mp_free(&p, blocks[5]), 0);
    TF_ASSERT_EQ(mp_used(&p), BLOCKS - 1u);
    {
        void *again = mp_alloc(&p);
        TF_ASSERT(again == blocks[5]);           /* 后进先出复用 */
        TF_ASSERT_EQ(mp_used(&p), BLOCKS);
        TF_ASSERT_EQ(mp_free(&p, again), 0);
    }

    /* ---- 重复释放被检测 ---- */
    rc = mp_free(&p, blocks[5]);
    TF_ASSERT_EQ(rc, -3);
    TF_ASSERT(mp_free_err_count(&p) >= 1u);

    /* ---- 野指针（不属于本池）被拒绝 ---- */
    {
        uint8_t outside[64];
        TF_ASSERT_EQ(mp_free(&p, outside), -2);
        TF_ASSERT_EQ(mp_free(&p, 0), -1);
    }

    /* ---- 释放全部并验证重分配覆盖所有块号 ---- */
    for (i = 0u; i < BLOCKS; i++) {
        if (i == 5u) {
            continue;             /* 块 5 已在重复释放测试中释放过 */
        }
        TF_ASSERT_EQ(mp_free(&p, blocks[i]), 0);
    }
    TF_ASSERT_EQ(mp_used(&p), 0u);
    TF_ASSERT_EQ(mp_free_blocks(&p), BLOCKS);
    TF_ASSERT_EQ(mp_check_integrity(&p), 0);
    {
        uint32_t seen = 0u;
        for (i = 0u; i < BLOCKS; i++) {
            void *b = mp_alloc(&p);
            uint32_t idx;
            blocks[i] = b;
            TF_ASSERT(b != 0);
            idx = mp_block_index(&p, b);
            TF_ASSERT(idx < BLOCKS);
            TF_ASSERT_EQ((seen >> idx) & 1u, 0u);   /* 不得重复发放同一块 */
            seen |= (1u << idx);
        }
        TF_ASSERT_EQ(seen, (1u << BLOCKS) - 1u);    /* 覆盖全部 16 个块 */
        for (i = 0u; i < BLOCKS; i++) {
            TF_ASSERT_EQ(mp_free(&p, blocks[i]), 0);
        }
    }

    /* ---- 乱序释放-分配不会破坏结构 ---- */
    for (i = 0u; i < 200u; i++) {
        void *a = mp_alloc(&p);
        void *b = mp_alloc(&p);
        TF_ASSERT(a != 0 && b != 0);
        TF_ASSERT_EQ(mp_free(&p, a), 0);
        TF_ASSERT_EQ(mp_free(&p, b), 0);
    }
    TF_ASSERT_EQ(mp_check_integrity(&p), 0);
    TF_ASSERT_EQ(mp_used(&p), 0u);

    /* ---- 越界指针与对齐校验 ---- */
    TF_ASSERT_EQ(mp_block_index(&p, s_mem), 0xFFFFFFFFu);
    TF_ASSERT_EQ(mp_block_index(&p, &s_mem[sizeof(s_mem)]), 0xFFFFFFFFu);
    TF_ASSERT(mp_payload_at(&p, BLOCKS) == 0);

    /* ---- 复位 ---- */
    (void)mp_alloc(&p);
    (void)mp_alloc(&p);
    mp_reset(&p);
    TF_ASSERT_EQ(mp_used(&p), 0u);
    TF_ASSERT_EQ(mp_peak(&p), 0u);
    TF_ASSERT_EQ(mp_alloc_fail_count(&p), 0u);
    TF_ASSERT_EQ(mp_check_integrity(&p), 0);

    tf_suite_end();
}
