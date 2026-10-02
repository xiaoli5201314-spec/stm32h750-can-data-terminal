/*
 * test_fdcan_driver.c
 * ---------------------------------------------------------------------------
 * FDCAN 驱动测试：位时序计算、消息 RAM 分区、寄存器配置、接收路径、
 * 发送路径与超时重试、错误计数、总线恢复状态机、时间戳换算。
 */
#include <string.h>
#include "test_framework.h"
#include "fdcan_driver.h"
#include "fdcan_filter.h"
#include "hal_stub.h"
#include "diag_log.h"

static diag_log_entry_t s_log[256];

static fdcan_frame_t make_frame(uint32_t id, uint8_t xtd, uint8_t dlc, uint8_t seed)
{
    fdcan_frame_t f;
    uint32_t n = fdcan_dlc_to_bytes(dlc);   /* DLC 编码 -> 实际数据字节数 */
    uint32_t i;
    (void)memset(&f, 0, sizeof(f));
    f.can_id = id;
    f.xtd = xtd;
    f.dlc = dlc;
    f.fdf = 1u;
    f.brs = 1u;
    for (i = 0u; i < n; i++) {
        f.data[i] = (uint8_t)(seed + i);
    }
    return f;
}

static void test_classic_and_remote_payload(void)
{
    fdcan_msgram_layout_t lay;
    volatile uint32_t *ram;
    uint32_t dlc;
    uint32_t rtr;
    uint32_t i;

    hal_sim_reset();
    TF_ASSERT_EQ(fdcan_init(&h750_fdcan1_config, &lay), 0);
    TF_ASSERT_EQ(fdcan_start(H750_FDCAN1_BASE), 0);
    ram = hal_mem_ptr(h750_fdcan1_config.msgram_base, lay.total_words * 4u);
    TF_ASSERT(ram != 0);
    for (rtr = 0u; rtr <= 1u; rtr++) {
        for (dlc = 0u; dlc <= 15u; dlc++) {
            fdcan_frame_t tx = make_frame(0x080u, 0u, (uint8_t)dlc, 0x31u);
            fdcan_frame_t rx;
            uint32_t want = rtr ? 0u : ((dlc > 8u) ? 8u : dlc);
            uint32_t pi = (H750_REG32(H750_FDCAN1_BASE + FDCAN_RXF0S) >>
                           FDCAN_RXF0S_F0PI_Pos) & 0x3Fu;
            uint32_t idx = lay.rx0_words + pi * lay.elem_words;
            tx.fdf = 0u;
            tx.brs = 0u;
            tx.rtr = (uint8_t)rtr;
            (void)memset(tx.data, 0x31, sizeof(tx.data));
            for (i = 2u; i < lay.elem_words; i++) {
                ram[idx + i] = 0xA5A5A5A5u;
            }
            TF_ASSERT_EQ(hal_sim_fdcan_inject(H750_FDCAN1_BASE, &tx), 0);
            /* Hardware leaves bytes outside the actual payload stale. */
            for (i = want; i < sizeof(tx.data); i++) {
                uint32_t word = idx + 2u + i / 4u;
                uint32_t shift = (i % 4u) * 8u;
                ram[word] = (ram[word] & ~(0xFFu << shift)) | (0xA5u << shift);
            }
            TF_ASSERT_EQ(fdcan_read_rx(&h750_fdcan1_config, &lay, 0u, &rx), 1);
            TF_ASSERT_EQ(rx.dlc, dlc); /* Preserve the raw DLC, not a byte length. */
            TF_ASSERT_EQ(rx.fdf, 0u);
            TF_ASSERT_EQ(rx.rtr, rtr);
            for (i = 0u; i < sizeof(rx.data); i++) {
                TF_ASSERT_EQ(rx.data[i], (i < want) ? 0x31u : 0u);
            }
        }
    }
    tf_note("Classic DLC 0..15 and RTR read no bytes beyond the actual payload");
}

static void test_classic_and_remote_tx_payload(void)
{
    fdcan_msgram_layout_t lay;
    volatile uint32_t *ram;
    uint32_t rtr;
    uint32_t dlc;
    uint32_t i;

    for (rtr = 0u; rtr <= 1u; rtr++) {
        for (dlc = 9u; dlc <= 15u; dlc++) {
            fdcan_frame_t tx = make_frame(0x080u, 0u, (uint8_t)dlc, 0x31u);
            uint32_t want_words = rtr ? 0u : 2u;
            hal_sim_reset();
            TF_ASSERT_EQ(fdcan_init(&h750_fdcan1_config, &lay), 0);
            ram = hal_mem_ptr(h750_fdcan1_config.msgram_base, lay.total_words * 4u);
            TF_ASSERT(ram != 0);
            for (i = 2u; i < lay.elem_words; i++) {
                ram[lay.txbuf_words + i] = 0xA5A5A5A5u;
            }
            tx.fdf = 0u;
            tx.brs = 0u;
            tx.rtr = (uint8_t)rtr;
            (void)memset(tx.data, 0x31, sizeof(tx.data));
            TF_ASSERT_EQ(fdcan_transmit(&h750_fdcan1_config, &lay, &tx), 0);
            for (i = 0u; i < lay.elem_words - 2u; i++) {
                TF_ASSERT_EQ(ram[lay.txbuf_words + 2u + i],
                             (i < want_words) ? 0x31313131u : 0xA5A5A5A5u);
            }
        }
    }
}

static void test_config_write_protection(void)
{
    fdcan_msgram_layout_t lay;
    uint32_t base = H750_FDCAN1_BASE;
    uint32_t cccr;

    hal_sim_reset();
    TF_ASSERT_EQ(fdcan_init(&h750_fdcan1_config, &lay), 0);
    TF_ASSERT_EQ(fdcan_start(base), 0);
    TF_ASSERT_EQ(H750_REG32(base + FDCAN_CCCR) & FDCAN_CCCR_CCE, 0u);
    hal_sim_fdcan_reg_write(base, FDCAN_GFC, 0u);
    TF_ASSERT_EQ(H750_REG32(base + FDCAN_GFC), 0x28u);
    cccr = H750_REG32(base + FDCAN_CCCR);
    hal_sim_fdcan_reg_write(base, FDCAN_CCCR, cccr | FDCAN_CCCR_CCE);
    TF_ASSERT_EQ(H750_REG32(base + FDCAN_CCCR) & FDCAN_CCCR_CCE, 0u);
    TF_ASSERT_EQ(fdcan_stop(base), 0);
    TF_ASSERT_EQ(H750_REG32(base + FDCAN_CCCR) &
                 (FDCAN_CCCR_INIT | FDCAN_CCCR_CCE),
                 FDCAN_CCCR_INIT | FDCAN_CCCR_CCE);
    hal_sim_fdcan_reg_write(base, FDCAN_GFC, FDCAN_GFC_RRFS | FDCAN_GFC_RRFE);
    TF_ASSERT_EQ(H750_REG32(base + FDCAN_GFC), FDCAN_GFC_RRFS | FDCAN_GFC_RRFE);
}

static void test_sniff_runtime_state(void)
{
    fdcan_msgram_layout_t lay;
    fdcan_config_t cfg = h750_fdcan1_config;
    uint32_t base = cfg.base;
    uint32_t running;
    uint32_t cce;

    cfg.loopback = 1u;
    for (running = 0u; running <= 1u; running++) {
        for (cce = 0u; cce <= 1u; cce++) {
            uint32_t before;
            uint32_t gfc;
            fdcan_frame_t f = make_frame(0x6A5u, 0u, 8u, 0x31u);
            hal_sim_reset();
            TF_ASSERT_EQ(fdcan_init(&cfg, &lay), 0);
            hal_sim_fdcan_reg_write(base, FDCAN_GFC,
                H750_REG32(base + FDCAN_GFC) | FDCAN_GFC_RRFS | FDCAN_GFC_RRFE);
            gfc = H750_REG32(base + FDCAN_GFC);
            if (running != 0u) {
                TF_ASSERT_EQ(fdcan_start(base), 0);
            } else if (cce == 0u) {
                hal_sim_fdcan_reg_write(base, FDCAN_CCCR,
                                        H750_REG32(base + FDCAN_CCCR) & ~FDCAN_CCCR_CCE);
            }
            before = H750_REG32(base + FDCAN_CCCR);
            hal_sim_fdcan_set_config_delay(base, 3u);
            TF_ASSERT_EQ(fdcan_set_sniff_mode(&cfg, 1u), 0);
            TF_ASSERT_EQ(H750_REG32(base + FDCAN_CCCR), before);
            TF_ASSERT_EQ(fdcan_get_sniff_mode(&cfg), 1);
            TF_ASSERT_EQ(H750_REG32(base + FDCAN_GFC),
                         gfc & ~(FDCAN_GFC_ANFS_Msk | FDCAN_GFC_ANFE_Msk));
            if (running != 0u) {
                TF_ASSERT_EQ(hal_sim_fdcan_inject(base, &f), 0);
                TF_ASSERT_EQ(fdcan_read_rx(&cfg, &lay, 0u, &f), 1);
                f = make_frame(0x12345678u, 1u, 8u, 0x22u);
                TF_ASSERT_EQ(hal_sim_fdcan_inject(base, &f), 0);
                TF_ASSERT_EQ(fdcan_read_rx(&cfg, &lay, 0u, &f), 1);
            }
            TF_ASSERT_EQ(fdcan_set_sniff_mode(&cfg, 0u), 0);
            TF_ASSERT_EQ(H750_REG32(base + FDCAN_CCCR), before);
            TF_ASSERT_EQ(H750_REG32(base + FDCAN_GFC), gfc);
            TF_ASSERT_EQ(fdcan_get_sniff_mode(&cfg), 0);
        }
    }
}

static void test_sniff_failures_restore_state(void)
{
    static const struct {
        uint32_t fault;
        int once;
        int rc;
        uint8_t stopped;
    } cases[] = {
        { HAL_SIM_FDCAN_FAIL_INIT,  0, -2, 0u },
        { HAL_SIM_FDCAN_FAIL_CCE,   0, -2, 0u },
        { HAL_SIM_FDCAN_FAIL_GFC,   0, -2, 0u },
        { HAL_SIM_FDCAN_FAIL_START, 1, -2, 0u },
        { HAL_SIM_FDCAN_FAIL_START, 0, -3, 1u },
        { HAL_SIM_FDCAN_FAIL_GFC | HAL_SIM_FDCAN_FAIL_START, 0, -3, 1u }
    };
    fdcan_msgram_layout_t lay;
    uint32_t base = H750_FDCAN1_BASE;
    uint32_t k;
    uint32_t enabled;

    for (enabled = 0u; enabled <= 1u; enabled++) {
        for (k = 0u; k < sizeof(cases) / sizeof(cases[0]); k++) {
            uint32_t before;
            uint32_t gfc;
            hal_sim_reset();
            TF_ASSERT_EQ(fdcan_init(&h750_fdcan1_config, &lay), 0);
            if (enabled != 0u) {
                TF_ASSERT_EQ(fdcan_set_sniff_mode(&h750_fdcan1_config, 1u), 0);
            }
            TF_ASSERT_EQ(fdcan_start(base), 0);
            before = H750_REG32(base + FDCAN_CCCR);
            gfc = H750_REG32(base + FDCAN_GFC);
            hal_sim_fdcan_set_config_delay(base, 2u);
            hal_sim_fdcan_set_config_faults(base, cases[k].fault, cases[k].once);
            TF_ASSERT_EQ(fdcan_set_sniff_mode(&h750_fdcan1_config,
                                             (uint8_t)!enabled), cases[k].rc);
            TF_ASSERT_EQ(H750_REG32(base + FDCAN_GFC), gfc);
            TF_ASSERT_EQ(fdcan_get_sniff_mode(&h750_fdcan1_config), enabled);
            TF_ASSERT_EQ(H750_REG32(base + FDCAN_CCCR) &
                         ~(FDCAN_CCCR_INIT | FDCAN_CCCR_CCE), before);
            TF_ASSERT_EQ(H750_REG32(base + FDCAN_CCCR) & FDCAN_CCCR_INIT,
                         cases[k].stopped ? FDCAN_CCCR_INIT : 0u);
        }
    }

    /* A timed-out, still pending INIT request must be cancelled on rollback. */
    hal_sim_reset();
    TF_ASSERT_EQ(fdcan_init(&h750_fdcan1_config, &lay), 0);
    TF_ASSERT_EQ(fdcan_start(base), 0);
    hal_sim_fdcan_set_config_delay(base, 80u);
    TF_ASSERT_EQ(fdcan_set_sniff_mode(&h750_fdcan1_config, 1u), -2);
    for (k = 0u; k < 100u; k++) {
        (void)hal_sim_fdcan_reg_read(base, FDCAN_CCCR);
    }
    TF_ASSERT_EQ(H750_REG32(base + FDCAN_CCCR) & FDCAN_CCCR_INIT, 0u);
    TF_ASSERT_EQ(fdcan_get_sniff_mode(&h750_fdcan1_config), 0);

    /* Stopped with CCE disabled is a distinct original state. */
    hal_sim_reset();
    TF_ASSERT_EQ(fdcan_init(&h750_fdcan1_config, &lay), 0);
    hal_sim_fdcan_reg_write(base, FDCAN_CCCR,
                            H750_REG32(base + FDCAN_CCCR) & ~FDCAN_CCCR_CCE);
    hal_sim_fdcan_set_config_faults(base, HAL_SIM_FDCAN_FAIL_CCE, 0);
    TF_ASSERT_EQ(fdcan_set_sniff_mode(&h750_fdcan1_config, 1u), -2);
    TF_ASSERT_EQ(H750_REG32(base + FDCAN_CCCR) & (FDCAN_CCCR_INIT | FDCAN_CCCR_CCE),
                 FDCAN_CCCR_INIT);
    TF_ASSERT_EQ(fdcan_get_sniff_mode(&h750_fdcan1_config), 0);
    TF_ASSERT_EQ(fdcan_set_sniff_mode(0, 1u), -1);
    tf_note("Sniff failures restore the original filter/state or return restoration failure");
}

void test_fdcan_driver(void)
{
    fdcan_bit_timing_t bt;
    fdcan_msgram_layout_t lay;
    fdcan_status_t st;
    fdcan_frame_t f;

    tf_suite_begin("FDCAN 驱动（位时序 / 消息RAM / 收发 / 恢复）");

    hal_sim_reset();
    (void)diag_log_init(s_log, 256u);

    /* ---- 1. 位时序计算（CAN1 500k/2M，CAN2 1M/5M，采样点 80%） ---- */
    TF_ASSERT_EQ(fdcan_bit_timing_calc(H750_FDCAN_KERNEL_HZ, H750_CAN1_NOMINAL_BPS,
                                       H750_CAN1_DATA_BPS, 800u, &bt), 0);
    TF_ASSERT_EQ(bt.nbrp, 23u);      /* 分频 24 -> 10MHz tq，20 tq/位 */
    TF_ASSERT_EQ(bt.ntseg1, 14u);
    TF_ASSERT_EQ(bt.ntseg2, 3u);
    TF_ASSERT_EQ(bt.nsjw, 3u);
    TF_ASSERT_EQ(bt.nominal_tq, 20u);
    TF_ASSERT_EQ(bt.nominal_bps, 500000u);
    TF_ASSERT_EQ(bt.nominal_sp_pm, 800u);
    TF_ASSERT_EQ(bt.dbrp, 5u);       /* 分频 6 -> 40MHz tq，20 tq/位 */
    TF_ASSERT_EQ(bt.dtseg1, 14u);
    TF_ASSERT_EQ(bt.dtseg2, 3u);
    TF_ASSERT_EQ(bt.data_bps, 2000000u);
    TF_ASSERT_EQ(bt.data_sp_pm, 800u);
    TF_ASSERT_EQ(bt.tdc_enable, 1u);
    TF_ASSERT_EQ(fdcan_nbtp_value(&bt), 0x06170E03u);
    TF_ASSERT_EQ(fdcan_dbtp_value(&bt), 0x00850E33u);
    tf_note("CAN1: NBTP=0x%08X DBTP=0x%08X 采样点=%u/%u permille",
            fdcan_nbtp_value(&bt), fdcan_dbtp_value(&bt),
            bt.nominal_sp_pm, bt.data_sp_pm);

    TF_ASSERT_EQ(fdcan_bit_timing_calc(H750_FDCAN_KERNEL_HZ, H750_CAN2_NOMINAL_BPS,
                                       H750_CAN2_DATA_BPS, 800u, &bt), 0);
    TF_ASSERT_EQ(bt.nbrp, 11u);      /* 分频 12 -> 20MHz tq，20 tq/位 */
    TF_ASSERT_EQ(bt.ntseg1, 14u);
    TF_ASSERT_EQ(bt.ntseg2, 3u);
    TF_ASSERT_EQ(bt.nominal_bps, 1000000u);
    TF_ASSERT_EQ(bt.dbrp, 1u);       /* 分频 2 -> 120MHz tq，24 tq/位 */
    TF_ASSERT_EQ(bt.dtseg1, 17u);
    TF_ASSERT_EQ(bt.dtseg2, 4u);
    TF_ASSERT_EQ(bt.data_bps, 5000000u);
    TF_ASSERT_EQ(fdcan_nbtp_value(&bt), 0x060B0E03u);
    TF_ASSERT_EQ(fdcan_dbtp_value(&bt), 0x00811144u);
    tf_note("CAN2: NBTP=0x%08X DBTP=0x%08X 数据段采样点=%u permille",
            fdcan_nbtp_value(&bt), fdcan_dbtp_value(&bt), bt.data_sp_pm);

    /* 采样点必须落在 CAN FD 推荐的 75%~85% 之间 */
    TF_ASSERT_EQ(fdcan_bit_timing_calc(H750_FDCAN_KERNEL_HZ, H750_CAN1_NOMINAL_BPS,
                                       H750_CAN1_DATA_BPS, 800u, &bt), 0);
    TF_ASSERT(bt.nominal_sp_pm >= 750u && bt.nominal_sp_pm <= 850u);
    TF_ASSERT(bt.data_sp_pm >= 750u && bt.data_sp_pm <= 850u);
    /* 不可实现的速率必须报错 */
    TF_ASSERT(fdcan_bit_timing_calc(H750_FDCAN_KERNEL_HZ, 7000000u, 7000000u, 800u, &bt) != 0);
    TF_ASSERT(fdcan_bit_timing_calc(0u, 500000u, 2000000u, 800u, &bt) != 0);

    /* ---- 2. 发送延迟补偿与 DLC 换算 ---- */
    TF_ASSERT_EQ(fdcan_tdcr_value(240000000u, 5000000u, 200u, 4u), 0x00000104u);
    TF_ASSERT_EQ(fdcan_dlc_to_bytes(0u), 0u);
    TF_ASSERT_EQ(fdcan_dlc_to_bytes(8u), 8u);
    TF_ASSERT_EQ(fdcan_dlc_to_bytes(9u), 12u);
    TF_ASSERT_EQ(fdcan_dlc_to_bytes(12u), 24u);
    TF_ASSERT_EQ(fdcan_dlc_to_bytes(13u), 32u);
    TF_ASSERT_EQ(fdcan_dlc_to_bytes(14u), 48u);
    TF_ASSERT_EQ(fdcan_dlc_to_bytes(15u), 64u);
    TF_ASSERT_EQ(fdcan_dlc_to_elem_size_code(15u), 7u);
    TF_ASSERT_EQ(FDCAN_DATA_BYTES_TO_WORDS(64u), 16u);
    TF_ASSERT_EQ(FDCAN_ELEM_HDR_WORDS + FDCAN_DATA_BYTES_TO_WORDS(64u), 18u);

    /* ---- 3. 消息 RAM 分区 ---- */
    TF_ASSERT_EQ(fdcan_layout_compute(5u, 2u, 32u, 16u, 8u, 8u, 18u, &lay), 0);
    TF_ASSERT_EQ(lay.std_filter_words, 0u);
    TF_ASSERT_EQ(lay.ext_filter_words, 5u);
    TF_ASSERT_EQ(lay.rx0_words, 9u);
    TF_ASSERT_EQ(lay.rx1_words, 585u);
    TF_ASSERT_EQ(lay.txevt_words, 873u);
    TF_ASSERT_EQ(lay.txbuf_words, 889u);
    TF_ASSERT_EQ(lay.total_words, 1033u);
    TF_ASSERT(lay.total_words <= H750_FDCAN_MSGRAM_WORDS);
    TF_ASSERT_EQ(fdcan_layout_compute(28u, 16u, 32u, 16u, 8u, 8u, 18u, &lay), 0);
    TF_ASSERT_EQ(lay.total_words, 28u + 32u + 576u + 288u + 16u + 144u);
    tf_note("默认分区占用 %u word / %u word（剩余 %u word 预留扩展）",
            lay.total_words, H750_FDCAN_MSGRAM_WORDS,
            H750_FDCAN_MSGRAM_WORDS - lay.total_words);
    /* 超容量必须报错 */
    TF_ASSERT_EQ(fdcan_layout_compute(128u, 64u, 64u, 64u, 32u, 32u, 18u, &lay), -3);
    TF_ASSERT_EQ(fdcan_layout_compute(5u, 2u, 32u, 16u, 8u, 8u, 1u, &lay), -2);

    /* ---- 4. 初始化寄存器落地 ---- */
    hal_sim_reset();
    TF_ASSERT_EQ(fdcan_init(&h750_fdcan1_config, &lay), 0);
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_CCCR),
                 FDCAN_CCCR_INIT | FDCAN_CCCR_CCE | FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE);
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_NBTP), 0x06170E03u);
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_DBTP), 0x00850E33u);
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_TSCC), 1u);   /* TSS=1, TCP=0 */
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_RXESC), 0x777u);
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_RXF0C),
                 (9u << FDCAN_RXF0C_F0SA_Pos) | (32u << FDCAN_RXF0C_F0S_Pos) |
                 (16u << FDCAN_RXF0C_F0WM_Pos));
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_RXF1C),
                 (585u << FDCAN_RXF1C_F1SA_Pos) | (16u << FDCAN_RXF1C_F1S_Pos) |
                 (8u << FDCAN_RXF1C_F1WM_Pos));
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_TXBC),
                 (889u << FDCAN_TXBC_TBSA_Pos) | (8u << FDCAN_TXBC_NDTB_Pos));
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_TXEFC),
                 (873u << FDCAN_TXEFC_EFSA_Pos) | (8u << FDCAN_TXEFC_EFS_Pos));
    TF_ASSERT((H750_REG32(H750_FDCAN1_BASE + FDCAN_IE) & FDCAN_IR_RF0N) != 0u);
    TF_ASSERT((H750_REG32(H750_FDCAN1_BASE + FDCAN_IE) & FDCAN_IR_BO) != 0u);
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_ILE), 1u);

    /* 启动：清除 INIT */
    TF_ASSERT_EQ(fdcan_start(H750_FDCAN1_BASE), 0);
    TF_ASSERT((H750_REG32(H750_FDCAN1_BASE + FDCAN_CCCR) & FDCAN_CCCR_INIT) == 0u);

    /* ---- 5. 接收路径：FIFO0 / FIFO1 / 拒收 ---- */
    f = make_frame(0x0A0u, 0u, 8u, 0x10u);
    TF_ASSERT_EQ(hal_sim_fdcan_inject(H750_FDCAN1_BASE, &f), 0);     /* G0 -> FIFO0 */
    f = make_frame(0x400u, 0u, 10u, 0x20u);    /* DLC=10 -> 16 字节数据 */
    TF_ASSERT_EQ(hal_sim_fdcan_inject(H750_FDCAN1_BASE, &f), 1);     /* G3 -> FIFO1 */
    f = make_frame(0x480u, 0u, 8u, 0x30u);
    TF_ASSERT_EQ(hal_sim_fdcan_inject(H750_FDCAN1_BASE, &f), -1);    /* 拒收 */
    TF_ASSERT_EQ(hal_sim_fdcan_rejected(H750_FDCAN1_BASE), 1u);

    {
        uint32_t status = H750_REG32(H750_FDCAN1_BASE + FDCAN_RXF0S);
        TF_ASSERT_EQ(status & FDCAN_RXF0S_F0FL_Msk, 1u);             /* 水位 1 */
        TF_ASSERT_EQ((status & FDCAN_RXF0S_F0GI_Msk) >> FDCAN_RXF0S_F0GI_Pos, 0u);
    }

    TF_ASSERT_EQ(fdcan_read_rx(&h750_fdcan1_config, &lay, 0u, &f), 1);
    TF_ASSERT_EQ(f.can_id, 0x0A0u);
    TF_ASSERT_EQ(f.xtd, 0u);
    TF_ASSERT_EQ(f.dlc, 8u);
    TF_ASSERT_EQ(f.data[0], 0x10u);
    TF_ASSERT_EQ(f.data[7], 0x17u);
    TF_ASSERT(f.filter_index <= 4u);
    TF_ASSERT_EQ(f.anmf, 0u);
    TF_ASSERT_EQ(fdcan_read_rx(&h750_fdcan1_config, &lay, 0u, &f), 0);   /* 已应答，空 */

    TF_ASSERT_EQ(fdcan_read_rx(&h750_fdcan1_config, &lay, 1u, &f), 1);
    TF_ASSERT_EQ(f.can_id, 0x400u);
    TF_ASSERT_EQ(f.dlc, 10u);
    TF_ASSERT_EQ(f.data[0], 0x20u);
    TF_ASSERT_EQ(f.data[15], 0x2Fu);
    TF_ASSERT((H750_REG32(H750_FDCAN1_BASE + FDCAN_RXF1S) & FDCAN_RXF1S_F1FL_Msk) == 0u);

    /* 扩展帧与 64 字节数据 */
    f = make_frame(0x18FF1234u, 1u, 15u, 0xA0u);
    TF_ASSERT_EQ(hal_sim_fdcan_inject(H750_FDCAN1_BASE, &f), 0);
    TF_ASSERT_EQ(fdcan_read_rx(&h750_fdcan1_config, &lay, 0u, &f), 1);
    TF_ASSERT_EQ(f.can_id, 0x18FF1234u);
    TF_ASSERT_EQ(f.xtd, 1u);
    TF_ASSERT_EQ(f.dlc, 15u);
    TF_ASSERT_EQ(f.data[0], 0xA0u);
    TF_ASSERT_EQ(f.data[63], 0xA0u + 63u);

    /* ---- 6. 发送路径与超时重试 ---- */
    {
        fdcan_frame_t tx = make_frame(0x123u, 0u, 8u, 0x40u);
        uint32_t t0 = hal_time_ms();
        TF_ASSERT(fdcan_transmit(&h750_fdcan1_config, &lay, &tx) >= 0);
        TF_ASSERT_EQ(hal_sim_fdcan_tx_requests(H750_FDCAN1_BASE), 1u);
        (void)memset(&st, 0, sizeof(st));
        TF_ASSERT_EQ(fdcan_poll_tx(&h750_fdcan1_config, &lay, &st, t0), 1);   /* 已完成 */
        TF_ASSERT_EQ(st.tx_frames, 1u);
        TF_ASSERT_EQ(st.tx_timeouts, 0u);
    }
    {
        /* 总线无应答：超时 -> 重试 3 次 -> 最终失败 */
        fdcan_frame_t tx = make_frame(0x124u, 0u, 8u, 0x50u);
        uint32_t t = hal_time_ms();
        int rc = 0;

        hal_sim_fdcan_set_tx_stuck(H750_FDCAN1_BASE, 1);
        TF_ASSERT(fdcan_transmit(&h750_fdcan1_config, &lay, &tx) >= 0);
        (void)memset(&st, 0, sizeof(st));
        for (rc = 0; rc < 4; rc++) {
            t += H750_CAN_TX_TIMEOUT_MS;
            {
                int pr = fdcan_poll_tx(&h750_fdcan1_config, &lay, &st, t);
                if (rc < 3) {
                    TF_ASSERT_EQ(pr, -1);      /* 重试中 */
                } else {
                    TF_ASSERT_EQ(pr, -2);      /* 重试用尽 */
                }
            }
        }
        TF_ASSERT_EQ(st.tx_timeouts, 4u);
        TF_ASSERT_EQ(st.tx_retries, 3u);       /* 最多重试 3 次 */
        TF_ASSERT_EQ(st.tx_failures, 1u);
        hal_sim_fdcan_set_tx_stuck(H750_FDCAN1_BASE, 0);
        tf_note("发送超时重试：timeouts=%u retries=%u failures=%u",
                st.tx_timeouts, st.tx_retries, st.tx_failures);
    }

    /* ---- 7. 错误计数与总线恢复 ---- */
    hal_sim_reset();
    (void)fdcan_init(&h750_fdcan1_config, &lay);
    (void)fdcan_start(H750_FDCAN1_BASE);
    (void)memset(&st, 0, sizeof(st));

    hal_sim_fdcan_set_ecr(H750_FDCAN1_BASE, 110u, 40u, 3u);
    hal_sim_fdcan_set_psr(H750_FDCAN1_BASE, FDCAN_PSR_EW | (2u << FDCAN_PSR_ACT_Pos));
    TF_ASSERT_EQ(fdcan_handle_bus_error(&h750_fdcan1_config, &st, 0u), 0);
    TF_ASSERT_EQ(st.tec, 110u);
    TF_ASSERT_EQ(st.rec, 40u);
    TF_ASSERT_EQ(st.error_warning, 1u);
    TF_ASSERT_EQ(st.bus_off, 0u);

    /* 总线关闭：进入指数退避 */
    hal_sim_fdcan_set_ecr(H750_FDCAN1_BASE, 256u, 0u, 5u);
    hal_sim_fdcan_set_psr(H750_FDCAN1_BASE, FDCAN_PSR_BO | FDCAN_PSR_EP |
                          (0u << FDCAN_PSR_ACT_Pos) | 6u);
    TF_ASSERT_EQ(fdcan_handle_bus_error(&h750_fdcan1_config, &st, 1000u), 1);
    TF_ASSERT_EQ(st.busoff_events, 1u);
    TF_ASSERT_EQ(st.backoff_ms, H750_CAN_BUSOFF_BACKOFF_MIN_MS);
    TF_ASSERT_EQ(st.recovering, 1u);
    TF_ASSERT_EQ(st.lec, 6u);       /* CRC 错误 */
    /* 退避未到：继续等待 */
    TF_ASSERT_EQ(fdcan_bus_recovery_step(&h750_fdcan1_config, &st, 1500u), 2);
    /* 退避到期：恢复成功，硬件 BUS-OFF 被清除 */
    TF_ASSERT_EQ(fdcan_bus_recovery_step(&h750_fdcan1_config, &st, 2100u), 1);
    TF_ASSERT_EQ(st.recover_events, 1u);
    TF_ASSERT_EQ(st.recovering, 0u);
    TF_ASSERT((H750_REG32(H750_FDCAN1_BASE + FDCAN_PSR) & FDCAN_PSR_BO) == 0u);
    TF_ASSERT((H750_REG32(H750_FDCAN1_BASE + FDCAN_CCCR) & FDCAN_CCCR_INIT) == 0u);

    /* 1s 内再次总线关闭：退避翻倍（2s） */
    hal_sim_fdcan_set_psr(H750_FDCAN1_BASE, FDCAN_PSR_BO);
    TF_ASSERT_EQ(fdcan_handle_bus_error(&h750_fdcan1_config, &st, 3000u), 1);
    TF_ASSERT_EQ(st.consecutive_busoff, 2u);
    TF_ASSERT_EQ(st.backoff_ms, 2000u);
    (void)fdcan_bus_recovery_step(&h750_fdcan1_config, &st, 4000u);

    /* 连续达到上限次数：进入严重故障，退避在 30s 以内 */
    {
        uint32_t k;
        uint32_t t = 3000u;
        for (k = 0u; k < 4u; k++) {
            t += 100u;                       /* 与上次总线关闭间隔 100ms，退避阶梯持续升级 */
            hal_sim_fdcan_set_psr(H750_FDCAN1_BASE, FDCAN_PSR_BO);
            (void)fdcan_handle_bus_error(&h750_fdcan1_config, &st, t);
            /* 退避到期后立即恢复，保证下一轮能重新检测到总线关闭 */
            (void)fdcan_bus_recovery_step(&h750_fdcan1_config, &st, t + st.backoff_ms);
        }
        TF_ASSERT(st.consecutive_busoff >= H750_CAN_BUSOFF_MAX_RETRY);
        TF_ASSERT_EQ(st.severe_fault, 1u);
        TF_ASSERT(st.backoff_ms >= H750_CAN_BUSOFF_BACKOFF_MIN_MS);
        TF_ASSERT(st.backoff_ms <= H750_CAN_BUSOFF_BACKOFF_MAX_MS);
        hal_sim_fdcan_set_psr(H750_FDCAN1_BASE, FDCAN_PSR_BO);
        TF_ASSERT_EQ(fdcan_handle_bus_error(&h750_fdcan1_config, &st, 60000u), 3);
        tf_note("总线恢复：busoff=%u recovered=%u 末次退避=%u ms 严重故障=%u",
                st.busoff_events, st.recover_events, st.backoff_ms, st.severe_fault);
    }

    /* ---- 8. 时间戳换算与嗅探模式 ---- */
    TF_ASSERT_EQ(fdcan_timestamp_to_us(100u, 500000u), 200u);   /* 2us/位时间 */
    TF_ASSERT_EQ(fdcan_timestamp_to_us(100u, 1000000u), 100u);
    TF_ASSERT_EQ(fdcan_timestamp_to_us(0u, 500000u), 0u);
    TF_ASSERT_EQ(fdcan_timestamp_to_us(100u, 0u), 0u);

    hal_sim_reset();
    (void)fdcan_init(&h750_fdcan1_config, &lay);
    TF_ASSERT_EQ(fdcan_get_sniff_mode(&h750_fdcan1_config), 0);
    TF_ASSERT_EQ(fdcan_set_sniff_mode(&h750_fdcan1_config, 1u), 0);
    TF_ASSERT_EQ(fdcan_get_sniff_mode(&h750_fdcan1_config), 1);
    {
        uint32_t gfc = H750_REG32(H750_FDCAN1_BASE + FDCAN_GFC);
        TF_ASSERT_EQ(gfc & FDCAN_GFC_ANFS_Msk, 0u);
        TF_ASSERT_EQ(gfc & FDCAN_GFC_ANFE_Msk, 0u);
    }
    f = make_frame(0x6A5u, 0u, 8u, 0x11u);
    TF_ASSERT_EQ(hal_sim_fdcan_inject(H750_FDCAN1_BASE, &f), 0);
    TF_ASSERT_EQ(fdcan_read_rx(&h750_fdcan1_config, &lay, 0u, &f), 1);
    TF_ASSERT_EQ(f.can_id, 0x6A5u);
    f = make_frame(0x12345678u, 1u, 8u, 0x22u);
    TF_ASSERT_EQ(hal_sim_fdcan_inject(H750_FDCAN1_BASE, &f), 0);
    TF_ASSERT_EQ(fdcan_read_rx(&h750_fdcan1_config, &lay, 0u, &f), 1);
    TF_ASSERT_EQ(f.can_id, 0x12345678u);
    TF_ASSERT_EQ(fdcan_set_sniff_mode(&h750_fdcan1_config, 0u), 0);
    TF_ASSERT_EQ(fdcan_get_sniff_mode(&h750_fdcan1_config), 0);
    TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_GFC), 0x28u);
    TF_ASSERT_EQ(hal_sim_fdcan_inject(H750_FDCAN1_BASE, &f), -1);
    f = make_frame(0x6A5u, 0u, 8u, 0x11u);
    TF_ASSERT_EQ(hal_sim_fdcan_inject(H750_FDCAN1_BASE, &f), -1);

    /* ---- 9. 双实例配置互不干扰 ---- */
    TF_ASSERT_EQ(h750_fdcan2_config.base, H750_FDCAN2_BASE);
    TF_ASSERT_EQ(h750_fdcan2_config.msgram_base,
                 H750_FDCAN_MSGRAM_BASE + (H750_FDCAN_MSGRAM_WORDS * 4u));
    TF_ASSERT_EQ(h750_fdcan2_config.bus, 1u);
    TF_ASSERT(h750_fdcan1_config.filters != h750_fdcan2_config.filters);

    test_classic_and_remote_payload();
    test_classic_and_remote_tx_payload();
    test_config_write_protection();
    test_sniff_runtime_state();
    test_sniff_failures_restore_state();
    tf_suite_end();
}
