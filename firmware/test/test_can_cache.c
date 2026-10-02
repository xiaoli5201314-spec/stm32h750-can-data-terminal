/*
 * test_can_cache.c
 * ---------------------------------------------------------------------------
 * 分层缓存与批量上报测试：
 *   - 溢出时的丢弃顺序（低优先级先丢 -> 中优先级 -> 最后才是高优先级）
 *   - 三层出队顺序、水位计算
 *   - TLV 打包/解包往返一致性与单包上限
 *   - CAN ID -> 优先级映射
 *
 * 策略说明（与 can_cache.c 的实现一致）：
 *   每层是独立配额的环形缓冲；当本层已满时，先按"低 -> 中"的顺序牺牲更低
 *   优先级的数据，最后才淘汰本层最老条目。因此低优先级永远最早、最多地被
 *   丢弃，高优先级只在自身层被同类占满时才丢。
 */
#include <string.h>
#include "test_framework.h"
#include "can_cache.h"

static can_report_rec_t s_p0[H750_CACHE_P0_SLOTS];
static can_report_rec_t s_p1[H750_CACHE_P1_SLOTS];
static can_report_rec_t s_p2[H750_CACHE_P2_SLOTS];
static uint8_t s_pack[H750_REPORT_MAX_BYTES];

static can_report_rec_t make_rec(uint32_t id, can_prio_t prio, uint8_t dlc, uint32_t seq)
{
    can_report_rec_t r;
    uint32_t i;
    (void)memset(&r, 0, sizeof(r));
    r.can_id = id;
    r.prio = (uint8_t)prio;
    r.dlc = dlc;
    r.seq = seq;
    r.ts_us = 1000u + seq;
    for (i = 0u; i < dlc; i++) {
        r.data[i] = (uint8_t)(seq + i);
    }
    return r;
}

void test_can_cache(void)
{
    can_cache_t c;
    can_report_rec_t r;
    uint32_t i;
    uint32_t evicted;

    tf_suite_begin("分层缓存溢出策略与批量上报");

    TF_ASSERT_EQ(can_cache_init(&c, s_p0, H750_CACHE_P0_SLOTS,
                                s_p1, H750_CACHE_P1_SLOTS,
                                s_p2, H750_CACHE_P2_SLOTS), 0);
    TF_ASSERT_EQ(can_cache_capacity(&c), 64u + 256u + 512u);
    TF_ASSERT_EQ(can_cache_init(0, s_p0, 1u, s_p1, 1u, s_p2, 1u), -1);
    TF_ASSERT_EQ(can_cache_init(&c, s_p0, 0u, s_p1, 1u, s_p2, 1u), -2);

    /* =====================================================================
     * 阶段 1：填满低优先级层 —— 低优先级最先被丢弃
     * ===================================================================== */
    for (i = 0u; i < H750_CACHE_P2_SLOTS; i++) {
        r = make_rec(0x300u + (i % 0x80u), CAN_PRIO_LOW, 8u, i);
        TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    }
    TF_ASSERT_EQ(can_cache_count_of(&c, CAN_PRIO_LOW), 512u);
    TF_ASSERT_EQ(can_cache_watermark_pm(&c), (512u * 1000u) / 832u);

    r = make_rec(0x301u, CAN_PRIO_LOW, 8u, 9999u);
    TF_ASSERT_EQ(can_cache_push(&c, &r), -1);          /* P2 满：丢弃新来的低优先级 */
    TF_ASSERT_EQ(c.p2.dropped_low, 1u);
    TF_ASSERT_EQ(c.p1.dropped_mid, 0u);
    TF_ASSERT_EQ(c.p0.dropped_high, 0u);
    TF_ASSERT_EQ(can_cache_count(&c), 512u);
    tf_note("阶段1：低优先级丢弃 %u 条，中/高优先级丢弃 0 条（低优先级最先被丢）",
            c.p2.dropped_low);

    /* =====================================================================
     * 阶段 2：P0 尚有配额 —— 高优先级入队不产生任何丢弃
     * ===================================================================== */
    r = make_rec(0x400u, CAN_PRIO_HIGH, 16u, 1u);
    TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    TF_ASSERT_EQ(c.p0.count, 1u);
    TF_ASSERT_EQ(c.p2.evicted_low_for_higher, 0u);
    TF_ASSERT_EQ(c.total_dropped, 1u);                 /* 仍然只有阶段 1 的那一条 */

    for (i = 1u; i < H750_CACHE_P0_SLOTS; i++) {
        r = make_rec(0x401u, CAN_PRIO_HIGH, 16u, i);
        TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    }
    TF_ASSERT_EQ(c.p0.count, 64u);
    TF_ASSERT_EQ(c.p0.dropped_high, 0u);
    TF_ASSERT_EQ(c.total_dropped, 1u);
    tf_note("阶段2：P0 填满 %u 条，全程 0 丢弃（P2 已满也未影响高优先级）", c.p0.count);

    /* =====================================================================
     * 阶段 3：P0 满 —— 先牺牲低优先级，再淘汰 P0 最老条目
     * ===================================================================== */
    evicted = c.p2.evicted_low_for_higher;
    TF_ASSERT_EQ(c.p2.dropped_low, 1u);
    r = make_rec(0x402u, CAN_PRIO_HIGH, 32u, 500u);
    TF_ASSERT_EQ(can_cache_push(&c, &r), 1);           /* 1 = 入队成功但发生淘汰 */
    TF_ASSERT_EQ(c.p2.evicted_low_for_higher, evicted + 1u);
    TF_ASSERT_EQ(c.p2.dropped_low, 2u);                /* 牺牲了一条低优先级 */
    TF_ASSERT_EQ(c.p0.dropped_high, 1u);               /* 同时淘汰了一条最老的高优先级 */
    TF_ASSERT_EQ(c.p0.count, 64u);
    TF_ASSERT_EQ(c.p1.dropped_mid, 0u);                /* 中优先级仍未受影响 */
    TF_ASSERT_EQ(c.high_overflow_alarm, 1u);
    tf_note("阶段3：P0 溢出时牺牲低优先级 %u 条、淘汰高优先级 %u 条，中优先级 0 条",
            c.p2.evicted_low_for_higher, c.p0.dropped_high);

    /* =====================================================================
     * 阶段 4：持续高优先级冲击 —— 低优先级被清空，中优先级仍未丢
     * ===================================================================== */
    {
        uint32_t low_before = c.p2.count;
        for (i = 0u; i < 100u; i++) {
            r = make_rec(0x403u, CAN_PRIO_HIGH, 32u, 600u + i);
            TF_ASSERT(can_cache_push(&c, &r) >= 0);
        }
        TF_ASSERT_EQ(c.p2.count, low_before - 100u);
        TF_ASSERT_EQ(c.p1.dropped_mid, 0u);
    }

    /* 把 P2 抽干 */
    while (c.p2.count > 0u) {
        r = make_rec(0x404u, CAN_PRIO_HIGH, 8u, 800u);
        (void)can_cache_push(&c, &r);
    }
    TF_ASSERT_EQ(c.p2.count, 0u);
    TF_ASSERT_EQ(c.p1.dropped_mid, 0u);                /* 中优先级依然没被丢 */
    tf_note("阶段4：P2 被清空（dropped_low=%u），dropped_mid 仍为 %u",
            c.p2.dropped_low, c.p1.dropped_mid);

    /* =====================================================================
     * 阶段 5：中优先级入队 —— 只有低优先级彻底耗尽后才会被丢
     * ===================================================================== */
    for (i = 0u; i < H750_CACHE_P1_SLOTS; i++) {
        r = make_rec(0x080u + (i % 0x80u), CAN_PRIO_MID, 8u, i);
        TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    }
    TF_ASSERT_EQ(c.p1.count, 256u);
    TF_ASSERT_EQ(c.p1.dropped_mid, 0u);

    r = make_rec(0x081u, CAN_PRIO_MID, 8u, 5000u);
    TF_ASSERT_EQ(can_cache_push(&c, &r), 0);           /* P2 已空，无处可让 -> 淘汰最老 MID */
    TF_ASSERT_EQ(c.p1.dropped_mid, 1u);
    tf_note("阶段5：P2 耗尽后中优先级开始丢弃（dropped_mid=%u）", c.p1.dropped_mid);

    /* =====================================================================
     * 阶段 6：高优先级入队 —— 依次牺牲低、中优先级
     * ===================================================================== */
    evicted = c.p1.evicted_low_for_higher;
    r = make_rec(0x405u, CAN_PRIO_HIGH, 8u, 9000u);
    TF_ASSERT_EQ(can_cache_push(&c, &r), 1);
    TF_ASSERT_EQ(c.p1.evicted_low_for_higher, evicted + 1u);   /* 牺牲一条中优先级 */
    TF_ASSERT_EQ(c.p2.count, 0u);

    /* =====================================================================
     * 阶段 7：丢弃顺序总览（低 -> 中 -> 高）
     * ===================================================================== */
    TF_ASSERT(c.p2.dropped_low > 500u);      /* 低优先级被大量丢弃 */
    TF_ASSERT(c.p2.dropped_low >= c.p1.dropped_mid);
    TF_ASSERT(c.p1.dropped_mid <= c.p0.dropped_high);
    TF_ASSERT(c.total_enqueued > c.total_dropped);
    tf_note("丢弃结算：low=%u mid=%u high=%u；为高优先级让路牺牲 low=%u mid=%u",
            c.p2.dropped_low, c.p1.dropped_mid, c.p0.dropped_high,
            c.p2.evicted_low_for_higher, c.p1.evicted_low_for_higher);

    /* =====================================================================
     * 阶段 8：出队顺序 P0 -> P1 -> P2
     * ===================================================================== */
    can_cache_reset(&c);
    r = make_rec(0x300u, CAN_PRIO_LOW, 8u, 1u);
    TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    r = make_rec(0x080u, CAN_PRIO_MID, 8u, 2u);
    TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    r = make_rec(0x400u, CAN_PRIO_HIGH, 8u, 3u);
    TF_ASSERT_EQ(can_cache_push(&c, &r), 0);

    TF_ASSERT_EQ(can_cache_peek(&c, &r), 1);    /* 1 = 来自 P0 */
    TF_ASSERT_EQ(r.can_id, 0x400u);
    TF_ASSERT_EQ(can_cache_count(&c), 3u);      /* peek 不消费 */
    TF_ASSERT_EQ(can_cache_pop(&c, &r), 1);
    TF_ASSERT_EQ(r.can_id, 0x400u);
    TF_ASSERT_EQ(can_cache_pop(&c, &r), 2);
    TF_ASSERT_EQ(r.can_id, 0x080u);
    TF_ASSERT_EQ(can_cache_pop(&c, &r), 3);
    TF_ASSERT_EQ(r.can_id, 0x300u);
    TF_ASSERT_EQ(can_cache_pop(&c, &r), 0);     /* 空 */

    /* =====================================================================
     * 阶段 9：优先级判定表
     * ===================================================================== */
    TF_ASSERT_EQ(can_cache_prio_of_id(0x400u, 0u), CAN_PRIO_HIGH);
    TF_ASSERT_EQ(can_cache_prio_of_id(0x47Fu, 0u), CAN_PRIO_HIGH);
    TF_ASSERT_EQ(can_cache_prio_of_id(0x580u, 0u), CAN_PRIO_HIGH);
    TF_ASSERT_EQ(can_cache_prio_of_id(0x080u, 0u), CAN_PRIO_MID);
    TF_ASSERT_EQ(can_cache_prio_of_id(0x100u, 0u), CAN_PRIO_MID);
    TF_ASSERT_EQ(can_cache_prio_of_id(0x200u, 0u), CAN_PRIO_MID);
    TF_ASSERT_EQ(can_cache_prio_of_id(0x700u, 0u), CAN_PRIO_MID);
    TF_ASSERT_EQ(can_cache_prio_of_id(0x300u, 0u), CAN_PRIO_LOW);
    TF_ASSERT_EQ(can_cache_prio_of_id(0x37Fu, 0u), CAN_PRIO_LOW);
    TF_ASSERT_EQ(can_cache_prio_of_id(0x18FF0000u, 1u), CAN_PRIO_MID);
    TF_ASSERT_EQ(can_cache_prio_of_id(0x0A000010u, 1u), CAN_PRIO_HIGH);

    /* =====================================================================
     * 阶段 10：批量打包（TLV）与解包往返
     * ===================================================================== */
    can_cache_reset(&c);
    for (i = 0u; i < 20u; i++) {
        uint8_t dlc = (uint8_t)((i % 3u == 0u) ? 8u : ((i % 3u == 1u) ? 16u : 15u));
        can_prio_t p = (can_prio_t)(i % 3u);
        r = make_rec(0x100u + i, p, dlc, i + 1u);
        TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    }
    {
        uint32_t len = 0u;
        uint32_t records = can_cache_pack_tlv(&c, s_pack, sizeof(s_pack), &len);
        TF_ASSERT_EQ(records, 20u);
        TF_ASSERT(len <= H750_REPORT_MAX_BYTES);
        TF_ASSERT(len > 0u);
        TF_ASSERT_EQ(s_pack[0], H750_REPORT_VERSION);
        TF_ASSERT_EQ(s_pack[1], H750_NODE_ID);
        TF_ASSERT_EQ(can_cache_count(&c), 0u);      /* 打包后缓存已清空 */
        tf_note("打包：%u 条记录 / %u 字节（上限 %u 字节，均摊 %.1f 字节/条）",
                records, len, H750_REPORT_MAX_BYTES, (double)len / (double)records);

        {
            static can_report_rec_t out[32];
            uint32_t n = can_cache_unpack_tlv(s_pack, len, out, 32u);
            TF_ASSERT_EQ(n, 20u);
            /* 打包顺序 = 出队顺序：先 P0（i%3==2, dlc=15），再 P1（dlc=16），最后 P2（dlc=8） */
            TF_ASSERT_EQ(out[0].can_id, 0x102u);
            TF_ASSERT_EQ(out[0].dlc, 15u);
            TF_ASSERT_EQ(out[0].prio, (uint8_t)CAN_PRIO_HIGH);
            TF_ASSERT_EQ(out[6].prio, (uint8_t)CAN_PRIO_MID);
            TF_ASSERT_EQ(out[6].can_id, 0x101u);
            TF_ASSERT_EQ(out[13].prio, (uint8_t)CAN_PRIO_LOW);
            TF_ASSERT_EQ(out[19].can_id, 0x112u);
            TF_ASSERT_EQ(out[19].dlc, 8u);
            TF_ASSERT_EQ(out[0].data[3], (uint8_t)(3u + 3u));
            TF_ASSERT_EQ(out[19].data[7], (uint8_t)(19u + 7u));
        }
    }

    /* 单包上限：大量数据时按容量截断，缓存中保留剩余条目 */
    can_cache_reset(&c);
    for (i = 0u; i < 64u; i++) {
        r = make_rec(0x300u + i, CAN_PRIO_LOW, 15u, i);
        TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    }
    {
        uint8_t small[256];
        uint32_t len = 0u;
        uint32_t records = can_cache_pack_tlv(&c, small, sizeof(small), &len);
        TF_ASSERT(records > 0u);
        TF_ASSERT(len <= sizeof(small));
        TF_ASSERT(can_cache_count(&c) > 0u);       /* 剩余条目留待下一包 */
        tf_note("小容量打包：%u 条 / %u 字节，缓存剩余 %u 条",
                records, len, can_cache_count(&c));
    }

    /* =====================================================================
     * 阶段 11：水位与触发条件
     * ===================================================================== */
    can_cache_reset(&c);
    TF_ASSERT_EQ(can_cache_should_flush(&c), 0);
    TF_ASSERT_EQ(can_cache_watermark_pm(&c), 0u);
    r = make_rec(0x400u, CAN_PRIO_HIGH, 8u, 1u);
    TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    TF_ASSERT_EQ(can_cache_should_flush(&c), 1);   /* 有高优先级数据立即触发 */
    can_cache_reset(&c);
    for (i = 0u; i < H750_CACHE_P2_SLOTS; i++) {
        r = make_rec(0x300u, CAN_PRIO_LOW, 8u, i);
        TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    }
    for (i = 0u; i < 71u; i++) {
        r = make_rec(0x100u, CAN_PRIO_MID, 8u, i);
        TF_ASSERT_EQ(can_cache_push(&c, &r), 0);
    }
    tf_note("水位：%u 条 / %u 条 = %u permille（阈值 %u）",
            can_cache_count(&c), can_cache_capacity(&c),
            can_cache_watermark_pm(&c), H750_CACHE_FLUSH_WM_PM);
    TF_ASSERT(can_cache_watermark_pm(&c) >= H750_CACHE_FLUSH_WM_PM);
    TF_ASSERT_EQ(can_cache_should_flush(&c), 1);   /* 水位超过 70% 触发 */

    tf_suite_end();
}
