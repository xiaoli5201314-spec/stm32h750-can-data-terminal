/*
 * test_fdcan_filter.c
 * ---------------------------------------------------------------------------
 * FDCAN 过滤器分组测试（表驱动）：
 *   - 标准帧范围 / 经典（掩码）/ 双 ID 过滤
 *   - 扩展帧掩码 / 范围（含 XIDAM）过滤
 *   - 不匹配时的全局策略（拒收 / 嗅探模式收进 FIFO0）
 *   - 过滤器元素寄存器编码与消息 RAM 编程结果
 * 同时把用例与期望结果导出为 CSV，供 tools/verify_filter.py 用独立实现
 * 交叉验证（两套实现结论一致才算通过）。
 */
#include <stdio.h>
#include <string.h>
#include "test_framework.h"
#include "fdcan_filter.h"
#include "fdcan_driver.h"
#include "hal_stub.h"

typedef struct {
    uint8_t     bus;          /* 0 = CAN1, 1 = CAN2 */
    uint8_t     xtd;
    uint32_t    id;
    int         expect_route; /* FDCAN_ROUTE_* */
    uint8_t     expect_hp;
    const char *desc;
} filter_case_t;

static const filter_case_t s_cases[] = {
    /* ---- FDCAN1 标准帧 ---- */
    { 0u, 0u, 0x080u, FDCAN_ROUTE_FIFO0, 0u, "G0 范围下边界 0x080 -> FIFO0" },
    { 0u, 0u, 0x0C0u, FDCAN_ROUTE_FIFO0, 0u, "G0 范围内 0x0C0 -> FIFO0" },
    { 0u, 0u, 0x0FFu, FDCAN_ROUTE_FIFO0, 0u, "G0 范围上边界 0x0FF -> FIFO0" },
    { 0u, 0u, 0x100u, FDCAN_ROUTE_FIFO0, 0u, "G1 掩码 0x7F0 命中 0x100" },
    { 0u, 0u, 0x10Fu, FDCAN_ROUTE_FIFO0, 0u, "G1 掩码命中 0x10F" },
    { 0u, 0u, 0x110u, FDCAN_ROUTE_REJECT, 0u, "0x110 不匹配任何组 -> 拒收" },
    { 0u, 0u, 0x123u, FDCAN_ROUTE_REJECT, 0u, "0x123 掩码不命中 -> 拒收" },
    { 0u, 0u, 0x200u, FDCAN_ROUTE_FIFO1, 1u, "G2 双 ID 0x200 -> FIFO1 高优先级" },
    { 0u, 0u, 0x201u, FDCAN_ROUTE_FIFO1, 1u, "G2 双 ID 0x201 -> FIFO1 高优先级" },
    { 0u, 0u, 0x202u, FDCAN_ROUTE_REJECT, 0u, "0x202 不属于双 ID -> 拒收" },
    { 0u, 0u, 0x400u, FDCAN_ROUTE_FIFO1, 1u, "G3 报警范围下边界 -> FIFO1" },
    { 0u, 0u, 0x47Fu, FDCAN_ROUTE_FIFO1, 1u, "G3 报警范围上边界 -> FIFO1" },
    { 0u, 0u, 0x480u, FDCAN_ROUTE_REJECT, 0u, "0x480 越界 -> 拒收" },
    { 0u, 0u, 0x700u, FDCAN_ROUTE_FIFO1, 0u, "G4 本机心跳 -> FIFO1" },
    { 0u, 0u, 0x701u, FDCAN_ROUTE_FIFO1, 0u, "G4 本机心跳 0x701 -> FIFO1" },
    { 0u, 0u, 0x300u, FDCAN_ROUTE_REJECT, 0u, "0x300 属于 CAN2，CAN1 拒收" },
    /* ---- FDCAN1 扩展帧 ---- */
    { 0u, 1u, 0x18FF0000u, FDCAN_ROUTE_FIFO0, 0u, "G5 J1939 掩码 0x1FFF0000 命中" },
    { 0u, 1u, 0x18FF1234u, FDCAN_ROUTE_FIFO0, 0u, "G5 J1939 同 PGN 命中" },
    { 0u, 1u, 0x18FE0000u, FDCAN_ROUTE_REJECT, 0u, "G5 不同 PGN -> 拒收" },
    { 0u, 1u, 0x0A000000u, FDCAN_ROUTE_FIFO1, 1u, "G6 私有扩展范围下边界 -> FIFO1" },
    { 0u, 1u, 0x0A0000FFu, FDCAN_ROUTE_FIFO1, 1u, "G6 私有扩展范围上边界 -> FIFO1" },
    { 0u, 1u, 0x0A000100u, FDCAN_ROUTE_REJECT, 0u, "G6 越界 -> 拒收" },
    /* ---- FDCAN2 标准帧 ---- */
    { 1u, 0u, 0x300u, FDCAN_ROUTE_FIFO0, 0u, "CAN2 G0 高频采样下边界 -> FIFO0" },
    { 1u, 0u, 0x37Fu, FDCAN_ROUTE_FIFO0, 0u, "CAN2 G0 高频采样上边界 -> FIFO0" },
    { 1u, 0u, 0x380u, FDCAN_ROUTE_REJECT, 0u, "CAN2 0x380 越界 -> 拒收" },
    { 1u, 0u, 0x580u, FDCAN_ROUTE_FIFO1, 1u, "CAN2 G1 时间同步 SYNC -> FIFO1" },
    { 1u, 0u, 0x581u, FDCAN_ROUTE_FIFO1, 1u, "CAN2 G1 SYNC 0x581 -> FIFO1" },
    { 1u, 0u, 0x582u, FDCAN_ROUTE_REJECT, 0u, "CAN2 0x582 非 SYNC -> 拒收" },
    { 1u, 0u, 0x080u, FDCAN_ROUTE_FIFO0, 0u, "CAN2 G2 跨总线镜像 -> FIFO0" },
    { 1u, 0u, 0x400u, FDCAN_ROUTE_FIFO1, 1u, "CAN2 G3 关键报警 -> FIFO1 高优先级" },
    /* ---- FDCAN2 扩展帧 ---- */
    { 1u, 1u, 0x1FFFFFFFu, FDCAN_ROUTE_FIFO1, 1u, "CAN2 G4 全掩码精确匹配 -> FIFO1" },
    { 1u, 1u, 0x1FFFFFFEu, FDCAN_ROUTE_REJECT, 0u, "CAN2 G4 不匹配 -> 拒收" },
    { 1u, 1u, 0x0A000010u, FDCAN_ROUTE_FIFO1, 0u, "CAN2 G5 私有扩展范围 -> FIFO1" },
};

#define CASE_COUNT (uint32_t)(sizeof(s_cases) / sizeof(s_cases[0]))

uint32_t test_fdcan_filter_case_count(void)
{
    return CASE_COUNT;
}

static const fdcan_filter_config_t *cfg_of(uint8_t bus)
{
    return (bus == 0u) ? &h750_can1_filter_cfg : &h750_can2_filter_cfg;
}

int test_fdcan_filter_dump_csv(const char *path)
{
    FILE *fp;
    uint32_t i;

    if (path == 0) {
        return -1;
    }
    fp = fopen(path, "w");
    if (fp == 0) {
        return -2;
    }
    fprintf(fp, "# bus,xtd,can_id,route,high_priority,description\n");
    for (i = 0u; i < CASE_COUNT; i++) {
        uint8_t hp = 0u;
        int route = fdcan_filter_route(cfg_of(s_cases[i].bus), s_cases[i].id,
                                       s_cases[i].xtd, &hp);
        fprintf(fp, "%u,%u,0x%08X,%d,%u,%s\n",
                (unsigned)s_cases[i].bus, (unsigned)s_cases[i].xtd,
                (unsigned)s_cases[i].id, route, (unsigned)hp, s_cases[i].desc);
    }
    fclose(fp);
    return 0;
}

void test_fdcan_filter(void)
{
    uint32_t i;
    uint32_t passed = 0u;
    uint32_t route_fail = 0u;

    tf_suite_begin("FDCAN 过滤器分组（标准/扩展/掩码/范围路由）");

    /* ---- 1. 表驱动路由用例 ---- */
    tf_note("用例总数 %u（要求 >= 8）", CASE_COUNT);
    for (i = 0u; i < CASE_COUNT; i++) {
        uint8_t hp = 0u;
        int route = fdcan_filter_route(cfg_of(s_cases[i].bus), s_cases[i].id,
                                       s_cases[i].xtd, &hp);
        if ((route != s_cases[i].expect_route) || (hp != s_cases[i].expect_hp)) {
            route_fail++;
            printf("   [FAIL] 用例 %u (%s): route=%d hp=%u 期望 route=%d hp=%u\n",
                   i, s_cases[i].desc, route, (unsigned)hp,
                   s_cases[i].expect_route, (unsigned)s_cases[i].expect_hp);
        } else {
            passed++;
        }
        TF_ASSERT_EQ(route, s_cases[i].expect_route);
        TF_ASSERT_EQ(hp, s_cases[i].expect_hp);
    }
    tf_note("过滤器路由用例：%u/%u 通过", passed, CASE_COUNT);
    TF_ASSERT(CASE_COUNT >= 8u);
    TF_ASSERT_EQ(route_fail, 0u);

    /* ---- 2. 嗅探模式：不匹配帧进 FIFO0 ---- */
    {
        fdcan_filter_config_t sniff = h750_can1_filter_cfg;
        uint8_t hp = 0u;
        sniff.anfs = 0u;
        sniff.anfe = 0u;
        TF_ASSERT_EQ(fdcan_filter_route(&sniff, 0x480u, 0u, &hp), FDCAN_ROUTE_FIFO0);
        TF_ASSERT_EQ(fdcan_filter_route(&sniff, 0x12345678u, 1u, &hp), FDCAN_ROUTE_FIFO0);
        /* 正常模式下必须拒收 */
        TF_ASSERT_EQ(fdcan_filter_route(&h750_can1_filter_cfg, 0x480u, 0u, &hp),
                     FDCAN_ROUTE_REJECT);
        sniff.anfs = 1u;
        TF_ASSERT_EQ(fdcan_filter_route(&sniff, 0x480u, 0u, &hp), FDCAN_ROUTE_FIFO1);
        sniff.anfs = 2u;
        TF_ASSERT_EQ(fdcan_filter_route(&sniff, 0x480u, 0u, &hp), FDCAN_ROUTE_REJECT);
        sniff.anfs = 3u;
        TF_ASSERT_EQ(fdcan_filter_route(&sniff, 0x480u, 0u, &hp), FDCAN_ROUTE_REJECT);
    }

    /* ---- 3. 过滤器元素编码（手工推导的期望值） ---- */
    TF_ASSERT_EQ(fdcan_filter_encode_std(&h750_can1_std_filters[0]), 0x088000FFu);
    TF_ASSERT_EQ(fdcan_filter_encode_std(&h750_can1_std_filters[1]), 0x890007F0u);
    TF_ASSERT_EQ(fdcan_filter_encode_std(&h750_can1_std_filters[2]), 0x72000201u);
    TF_ASSERT_EQ(fdcan_filter_encode_std(&h750_can1_std_filters[3]), 0x3400047Fu);
    {
        uint32_t f0 = 0u;
        uint32_t f1 = 0u;
        fdcan_filter_encode_ext(&h750_can1_ext_filters[0], &f0, &f1);
        TF_ASSERT_EQ(f0, 0x38FF0000u);   /* EFEC=FIFO0(1)<<29 | EFID1=0x18FF0000 */
        TF_ASSERT_EQ(f1, 0x9FFF0000u);   /* EFT=掩码(2)<<30    | EFID2=0x1FFF0000 */
        fdcan_filter_encode_ext(&h750_can1_ext_filters[1], &f0, &f1);
        TF_ASSERT_EQ(f0, 0xCA000000u);   /* EFEC=FIFO1_PRIO(6)<<29 | EFID1 */
        TF_ASSERT_EQ(f1, 0xCA0000FFu);   /* EFT=范围无EIDM(3)<<30  | EFID2 */
    }

    /* ---- 4. 全局过滤与配置寄存器值 ---- */
    TF_ASSERT_EQ(fdcan_filter_gfc_value(&h750_can1_filter_cfg), 0x28u);
    {
        fdcan_filter_config_t sniff = h750_can1_filter_cfg;
        sniff.anfs = 0u;
        sniff.anfe = 0u;
        sniff.rrfs = 1u;
        sniff.rrfe = 1u;
        TF_ASSERT_EQ(fdcan_filter_gfc_value(&sniff), 0x03u);   /* FIFO0 + RRFE|RRFS */
    }
    TF_ASSERT_EQ(fdcan_filter_sidfc_value(0u, 5u), 0x00050000u);
    TF_ASSERT_EQ(fdcan_filter_xidfc_value(10u, 2u), 0x00020028u);

    /* ---- 5. 消息 RAM 编程结果 ---- */
    hal_sim_reset();
    {
        fdcan_msgram_layout_t lay;
        volatile uint32_t *mram;
        TF_ASSERT_EQ(fdcan_layout_compute(h750_can1_std_filter_count,
                                          h750_can1_ext_filter_count,
                                          32u, 16u, 8u, 8u, 18u, &lay), 0);
        TF_ASSERT_EQ(lay.std_filter_words, 0u);
        TF_ASSERT_EQ(lay.ext_filter_words, 5u);      /* 5 个标准元素 * 1 word */
        TF_ASSERT_EQ(lay.rx0_words, 9u);             /* 5 + 2*2 */
        TF_ASSERT_EQ(lay.rx1_words, 9u + 32u * 18u);
        TF_ASSERT_EQ(fdcan_filter_program(H750_FDCAN1_BASE, H750_FDCAN_MSGRAM_BASE,
                                          &h750_can1_filter_cfg, &lay), 0);
        mram = (volatile uint32_t *)hal_mem_ptr(H750_FDCAN_MSGRAM_BASE, 64u * 4u);
        TF_ASSERT(mram != 0);
        TF_ASSERT_EQ(mram[0], fdcan_filter_encode_std(&h750_can1_std_filters[0]));
        TF_ASSERT_EQ(mram[4], fdcan_filter_encode_std(&h750_can1_std_filters[4]));
        TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_SIDFC),
                     fdcan_filter_sidfc_value(0u, h750_can1_std_filter_count));
        TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_XIDFC),
                     fdcan_filter_xidfc_value(5u, h750_can1_ext_filter_count));
        TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_XIDAM), 0x1FFFFFFFu);
    }

    tf_suite_end();
}
