/*
 * test_system_integration.c
 * ---------------------------------------------------------------------------
 * 整机流水线集成测试：
 *   在 PC 仿真环境下把"总线收帧 -> 过滤器路由 -> FIFO 搬运 -> 解析 ->
 *   分层缓存 -> TLV 批量上报 -> 以太网发送 -> LCD 诊断 -> SD 日志导出"
 *   整条链路跑起来，并输出主机仿真统计（不代表板端硬件实测）。
 */
#include <string.h>
#include "test_framework.h"
#include "app.h"
#include "hal_stub.h"
#include "diag_log.h"
#include "sd_file.h"
#include "lcd_diag.h"
#include "rtos_port.h"
#include "can_cache.h"

#define INJECT_FRAMES 240u

/* 字节数 -> CAN FD 的 DLC 编码（0..8 / 12 / 16 / 20 / 24 / 32 / 48 / 64） */
static uint8_t dlc_of_bytes(uint32_t bytes)
{
    if (bytes <= 8u) { return (uint8_t)bytes; }
    if (bytes <= 12u) { return 9u; }
    if (bytes <= 16u) { return 10u; }
    if (bytes <= 20u) { return 11u; }
    if (bytes <= 24u) { return 12u; }
    if (bytes <= 32u) { return 13u; }
    if (bytes <= 48u) { return 14u; }
    return 15u;
}

static uint32_t bytes_of_dlc(uint8_t dlc)
{
    if (dlc <= 8u) { return dlc; }
    switch (dlc) {
    case 9u:  return 12u;
    case 10u: return 16u;
    case 11u: return 20u;
    case 12u: return 24u;
    case 13u: return 32u;
    case 14u: return 48u;
    default:  return 64u;
    }
}

static fdcan_frame_t mk(uint32_t id, uint8_t xtd, uint32_t nbytes, uint8_t seed)
{
    fdcan_frame_t f;
    uint32_t i;
    (void)memset(&f, 0, sizeof(f));
    f.can_id = id;
    f.xtd = xtd;
    f.dlc = dlc_of_bytes(nbytes);
    f.fdf = 1u;
    f.brs = 1u;
    for (i = 0u; i < bytes_of_dlc(f.dlc); i++) {
        f.data[i] = (uint8_t)(seed + i);
    }
    return f;
}

static void test_can2_severe_alert(void)
{
    uint32_t k;
    uint32_t recovered;

    hal_sim_reset();
    TF_ASSERT_EQ(app_init(), 0);
    H750_REG32(H750_LED_ERR_PORT + GPIO_BSRR) = 0u;
    TF_ASSERT_EQ(app_get_stats()->severe_fault, 0u);
    for (k = 0u; k < H750_CAN_BUSOFF_MAX_RETRY; k++) {
        hal_sim_fdcan_set_psr(H750_FDCAN2_BASE, FDCAN_PSR_BO);
        (void)app_bus_step();
        if (k + 1u < H750_CAN_BUSOFF_MAX_RETRY) {
            TF_ASSERT_EQ(app_get_stats()->severe_fault, 0u);
            hal_sim_advance_ms(H750_CAN_BUSOFF_BACKOFF_MIN_MS << k);
            (void)app_bus_step();
        }
    }
    TF_ASSERT_EQ(app_get_stats()->severe_fault, 1u);
    TF_ASSERT_NE(H750_REG32(H750_LED_ERR_PORT + GPIO_BSRR) &
                 (1u << H750_LED_ERR_PIN), 0u);
    recovered = app_get_stats()->recovered;
    hal_sim_advance_ms(H750_CAN_BUSOFF_BACKOFF_MAX_MS);
    (void)app_bus_step();
    TF_ASSERT_EQ(app_get_stats()->recovered, recovered);
    TF_ASSERT_NE(H750_REG32(H750_FDCAN2_BASE + FDCAN_PSR) & FDCAN_PSR_BO, 0u);
    tf_note("CAN2-only severe BUS-OFF propagates to the global alert and LED");
}

static void test_classic_and_remote_serialization(void)
{
    uint32_t dlc;
    uint32_t rtr;
    uint32_t bus;
    hal_sim_reset();
    TF_ASSERT_EQ(app_init(), 0);
    for (bus = 0u; bus <= 1u; bus++) {
        for (rtr = 0u; rtr <= 1u; rtr++) {
            for (dlc = 0u; dlc <= 15u; dlc++) {
                fdcan_frame_t f = mk(0x080u, 0u, 64u, 0x31u);
                can_report_rec_t rec;
                uint8_t buf[128];
                uint32_t len = 0u;
                uint32_t i;
                uint32_t want = rtr ? 0u : ((dlc > 8u) ? 8u : dlc);
                f.dlc = (uint8_t)dlc;
                f.fdf = 0u;
                f.brs = 0u;
                f.rtr = (uint8_t)rtr;
                TF_ASSERT_EQ(app_inject_frame((uint8_t)bus, &f), 0);
                TF_ASSERT_EQ(app_rx_step(), 1u);
                TF_ASSERT_EQ(app_parse_step(), 1u);
                TF_ASSERT_EQ(can_cache_pack_tlv(app_cache(), buf, sizeof(buf), &len), 1u);
                TF_ASSERT_EQ(len, 26u + want); /* Packet8 + TLV2 + record16 + payload. */
                TF_ASSERT_EQ(buf[9], 16u + want);
                TF_ASSERT_EQ(can_cache_unpack_tlv(buf, len, &rec, 1u), 1u);
                TF_ASSERT_EQ(rec.dlc, want);
                TF_ASSERT_EQ(rec.bus, bus);
                TF_ASSERT_EQ(rec.flags & CAN_REC_FLAG_RTR, rtr ? CAN_REC_FLAG_RTR : 0u);
                TF_ASSERT_EQ(rec.flags & CAN_REC_FLAG_FDF, 0u);
                for (i = 0u; i < sizeof(rec.data); i++) {
                    TF_ASSERT_EQ(rec.data[i], (i < want) ? f.data[i] : 0u);
                }
            }
        }
    }
    tf_note("Both buses serialize classic frames as <=8 bytes and RTR as zero payload");
}

static void test_sniff_failure_propagation(void)
{
    static const uint32_t faults[] = {
        HAL_SIM_FDCAN_FAIL_INIT, HAL_SIM_FDCAN_FAIL_CCE,
        HAL_SIM_FDCAN_FAIL_GFC, HAL_SIM_FDCAN_FAIL_START
    };
    uint32_t bus;
    uint32_t k;
    uint32_t enabled;
    for (enabled = 0u; enabled <= 1u; enabled++) {
        for (bus = 0u; bus <= 1u; bus++) {
            for (k = 0u; k < sizeof(faults) / sizeof(faults[0]); k++) {
                diag_stats_t before;
                diag_stats_t after;
                uint32_t base = bus ? H750_FDCAN2_BASE : H750_FDCAN1_BASE;
                hal_sim_reset();
                TF_ASSERT_EQ(app_init(), 0);
                if (enabled != 0u) {
                    TF_ASSERT_EQ(app_set_sniff_mode(1u), 0);
                }
                diag_log_get_stats(&before);
                hal_sim_fdcan_set_config_faults(base, faults[k], 0);
                TF_ASSERT_EQ(app_set_sniff_mode((uint8_t)!enabled),
                             (faults[k] == HAL_SIM_FDCAN_FAIL_START) ? -3 : -2);
                TF_ASSERT_EQ(app_get_sniff_mode(), enabled);
                TF_ASSERT_EQ(fdcan_get_sniff_mode(&h750_fdcan1_config), enabled);
                TF_ASSERT_EQ(fdcan_get_sniff_mode(&h750_fdcan2_config), enabled);
                TF_ASSERT_EQ(H750_REG32(H750_FDCAN1_BASE + FDCAN_CCCR) & FDCAN_CCCR_INIT,
                    (bus == 0u && faults[k] == HAL_SIM_FDCAN_FAIL_START) ? FDCAN_CCCR_INIT : 0u);
                TF_ASSERT_EQ(H750_REG32(H750_FDCAN2_BASE + FDCAN_CCCR) & FDCAN_CCCR_INIT,
                    (bus == 1u && faults[k] == HAL_SIM_FDCAN_FAIL_START) ? FDCAN_CCCR_INIT : 0u);
                diag_log_get_stats(&after);
                TF_ASSERT_EQ(after.total, before.total); /* No success log on a failed switch. */
            }
        }
    }
    tf_note("Either bus failing a sniff transaction propagates error without software success");
}

void test_system_integration(void)
{
    const app_stats_t *st;
    uint32_t i;
    uint32_t injected = 0u;
    uint32_t rejected = 0u;
    uint32_t steps;

    tf_suite_begin("整机流水线集成（收帧->解析->缓存->上报->诊断->导出）");

    hal_sim_reset();
    TF_ASSERT_EQ(app_init(), 0);
    {
        fdcan_frame_t f = mk(0x300u, 0u, 64u, 0x31u);
        can_report_rec_t rec;
        TF_ASSERT_EQ(app_inject_frame(1u, &f), 0);
        TF_ASSERT_EQ(app_rx_step(), 1u);
        TF_ASSERT_EQ(app_parse_step(), 1u);
        TF_ASSERT(can_cache_pop(app_cache(), &rec) > 0);
        TF_ASSERT_EQ(rec.bus, 1u);
        TF_ASSERT_EQ(rec.dlc, 64u);
        TF_ASSERT_EQ(memcmp(rec.data, f.data, 64u), 0);
    }
    hal_sim_reset();
    TF_ASSERT_EQ(app_init(), 0);

    /* ---- 1. 向两条总线注入混合优先级报文（边注入边运行，避免 FIFO 溢出） ---- */
    for (i = 0u; i < INJECT_FRAMES; i++) {
        fdcan_frame_t f;
        uint8_t bus = (uint8_t)(i & 1u);
        int rc;

        if (bus == 0u) {
            /* CAN1：伺服状态（中）/ 报警（高）/ J1939（中）混合 */
            switch (i % 3u) {
            case 0u: f = mk((uint32_t)(0x080u + (i % 0x40u)), 0u, 8u, (uint8_t)i); break;
            case 1u: f = mk((uint32_t)(0x400u + (i % 0x40u)), 0u, 16u, (uint8_t)i); break;
            default: f = mk((uint32_t)(0x18FF0000u + i), 1u, 32u, (uint8_t)i); break;
            }
        } else {
            switch (i % 3u) {
            case 0u: f = mk((uint32_t)(0x300u + (i % 0x40u)), 0u, 32u, (uint8_t)i); break;
            case 1u: f = mk((uint32_t)(0x580u + (i % 2u)), 0u, 8u, (uint8_t)i); break;
            default: f = mk((uint32_t)(0x400u + (i % 2u)), 0u, 16u, (uint8_t)i); break;
            }
        }
        rc = app_inject_frame(bus, &f);
        if (rc >= 0) {
            injected++;
        } else {
            rejected++;
        }
        if (((i + 1u) % 12u) == 0u) {
            (void)app_run_ms(2u);     /* 每 12 帧运行 2ms，让接收线程及时搬走 */
        }
    }
    TF_ASSERT_EQ(injected, INJECT_FRAMES);
    TF_ASSERT_EQ(rejected, 0u);
    tf_note("总线注入 %u 帧（CAN1/CAN2 各半），被过滤器拒收 %u 帧", injected, rejected);

    /* 额外注入一帧不属于任何过滤器分组的报文，验证拒收 */
    {
        fdcan_frame_t f = mk(0x480u, 0u, 8u, 0x55u);
        TF_ASSERT_EQ(app_inject_frame(0u, &f), -1);
    }

    /* ---- 2. 运行流水线直到排空 ---- */
    steps = app_run_ms(200u);
    TF_ASSERT_EQ(steps, 200u);

    st = app_get_stats();
    TF_ASSERT_EQ(st->rx_frames, INJECT_FRAMES);
    TF_ASSERT_EQ(st->parsed, INJECT_FRAMES);
    TF_ASSERT_EQ(st->rx_dropped_ring, 0u);
    TF_ASSERT(st->reports > 0u);
    TF_ASSERT_EQ(st->report_records, INJECT_FRAMES);
    TF_ASSERT_EQ(st->drop_high, 0u);          /* 高优先级一条都没丢 */
    TF_ASSERT_EQ(can_cache_count(app_cache()), 0u);   /* 全部上报完毕 */
    TF_ASSERT(st->lcd_frames > 0u);
    TF_ASSERT(st->log_entries > 0u);

    tf_note("流水线：收到 %u 帧，解析 %u 帧，上报 %u 包 / %u 条记录 / %u 字节",
            st->rx_frames, st->parsed, st->reports, st->report_records, st->report_bytes);
    tf_note("优先级丢弃：low=%u mid=%u high=%u；缓存峰值水位 %u permille",
            st->drop_low, st->drop_mid, st->drop_high, st->cache_peak_pm);

    /* ---- 3. 总线错误与恢复（由流水线自动处理） ---- */
    hal_sim_fdcan_set_ecr(H750_FDCAN1_BASE, 256u, 0u, 9u);
    hal_sim_fdcan_set_psr(H750_FDCAN1_BASE, FDCAN_PSR_BO);
    (void)app_run_ms(5u);
    st = app_get_stats();
    TF_ASSERT(st->busoff >= 1u);
    (void)app_run_ms(1500u);              /* 越过 1s 退避 */
    st = app_get_stats();
    TF_ASSERT(st->recovered >= 1u);
    tf_note("总线恢复：BUS-OFF %u 次，恢复成功 %u 次（退避后自动重入总线）",
            st->busoff, st->recovered);

    /* ---- 4. 心跳发送（1s 周期） ---- */
    (void)app_run_ms(1100u);
    st = app_get_stats();
    TF_ASSERT(st->tx_ok >= 1u);
    tf_note("心跳发送成功 %u 帧（FDCAN1 0x700，1s 周期）", st->tx_ok);

    /* ---- 5. 日志导出到 SD 卡 ---- */
    TF_ASSERT_EQ(sd_card_present(), 1);
    TF_ASSERT_EQ(fs_mount(), 0);
    TF_ASSERT_EQ(app_export_log_to_sd(), 0);
    {
        fs_file_t file;
        static char buf[512];
        TF_ASSERT_EQ(fs_open("diag.log", &file), 0);
        TF_ASSERT(file.bytes > 0u);
        TF_ASSERT(fs_read(&file, 0u, buf, sizeof(buf) - 1u) > 0);
        buf[sizeof(buf) - 1u] = '\0';
        TF_ASSERT(strncmp(buf, "# H750 CAN-DAQ diag log", 23u) == 0);
        tf_note("SD 导出：diag.log %u 字节，首行为日志头", file.bytes);
        (void)fs_close(&file);
    }

    /* ---- 6. 离线数据导出（暂停上报线程，让缓存里留有待导出条目） ---- */
    (void)rtos_thread_set_ready(2u, 0u);      /* 2 = cache_flush_thr */
    for (i = 0u; i < 30u; i++) {
        fdcan_frame_t f = mk((uint32_t)(0x400u + (i % 0x40u)), 0u, 16u, (uint8_t)i);
        TF_ASSERT(app_inject_frame(0u, &f) >= 0);
        if (((i + 1u) % 8u) == 0u) {
            (void)app_run_ms(1u);     /* 及时搬走，避免 RX FIFO 溢出 */
        }
    }
    (void)app_run_ms(5u);
    TF_ASSERT(can_cache_count(app_cache()) > 0u);   /* 上报线程暂停，数据留在缓存中 */
    {
        int n = app_export_offline_to_sd();
        TF_ASSERT(n > 0);
        tf_note("离线导出：offline.csv 写入 %d 条记录（上报线程暂停期间）", n);
    }
    (void)rtos_thread_set_ready(2u, 1u);
    (void)app_run_ms(30u);
    st = app_get_stats();
    TF_ASSERT(st->parsed >= INJECT_FRAMES + 30u);

    /* ---- 7. 嗅探模式：不匹配帧也能收上来（现场排障） ---- */
    TF_ASSERT_EQ(app_set_sniff_mode(1u), 0);
    TF_ASSERT_EQ(app_get_sniff_mode(), 1);
    {
        fdcan_frame_t f = mk(0x6A5u, 0u, 8u, 0x11u);   /* 不属于任何分组 */
        TF_ASSERT(app_inject_frame(0u, &f) >= 0);
        (void)app_run_ms(5u);
    }
    TF_ASSERT_EQ(app_set_sniff_mode(0u), 0);

    /* ---- 8. 各线程工作量占比与线程配置 ---- */
    {
        uint32_t total = 0u;
        uint32_t k;
        const char *names[APP_STAGE_COUNT] = { "can_rx_thr", "parse_thr",
                                               "cache_flush_thr", "bus_monitor_thr",
                                               "diag_thr" };
        for (k = 0u; k < APP_STAGE_COUNT; k++) {
            total += st->cpu_steps[k];
        }
        TF_ASSERT(rtos_thread_count() >= 5u);
        for (k = 0u; k < APP_STAGE_COUNT; k++) {
            uint32_t pm = (total == 0u) ? 0u : (st->cpu_steps[k] * 1000u) / total;
            tf_note("线程 %-15s 处理条目占比 %u.%u%%（栈 %u 字节，执行 %u 次）",
                    names[k], pm / 10u, pm % 10u,
                    rtos_thread_at(k)->stack_bytes, rtos_thread_at(k)->runs);
        }
        tf_note("运行 %u 个 tick，线程总执行 %u 次", rtos_ticks(), rtos_total_steps());
    }

    /* ---- 9. 双缓冲 LCD 与日志统计复核 ---- */
    {
        lcd_ctx_t ctx;
        diag_stats_t ds;
        TF_ASSERT_EQ(lcd_get_ctx(&ctx), 0);
        TF_ASSERT(ctx.frames > 0u);
        TF_ASSERT(ctx.lit_pixels > 0u);
        diag_log_get_stats(&ds);
        TF_ASSERT(ds.total > 0u);
        tf_note("LCD 渲染 %u 帧，最近一帧点亮像素 %u；日志条目 %u / 覆盖 %u 次",
                ctx.frames, ctx.lit_pixels, ds.count, ds.wraps);
    }

    /* Fill the entire RX queue without a consumer, then verify its full boundary. */
    hal_sim_reset();
    TF_ASSERT_EQ(app_init(), 0);
    for (i = 0u; i < H750_RING_CAN_RX_SLOTS; i++) {
        fdcan_frame_t f = mk(0x080u, 0u, 64u, (uint8_t)i);
        TF_ASSERT_EQ(app_inject_frame(0u, &f), 0);
        TF_ASSERT_EQ(app_rx_step(), 1u);
    }
    {
        fdcan_frame_t f = mk(0x080u, 0u, 64u, 0x55u);
        TF_ASSERT_EQ(app_inject_frame(0u, &f), 0);
        TF_ASSERT_EQ(app_rx_step(), 0u);
        TF_ASSERT_EQ(app_get_stats()->rx_dropped_ring, 1u);
        uint32_t parsed = 0u;
        for (i = 0u; i < H750_RING_CAN_RX_SLOTS / 64u; i++) {
            parsed += app_parse_step();
        }
        TF_ASSERT_EQ(parsed, H750_RING_CAN_RX_SLOTS);
        TF_ASSERT_EQ(app_get_stats()->parsed, H750_RING_CAN_RX_SLOTS);
    }

    test_can2_severe_alert();
    test_classic_and_remote_serialization();
    test_sniff_failure_propagation();
    tf_suite_end();
}
