/*
 * app_main.c
 * ---------------------------------------------------------------------------
 * 应用装配：把 BSP、驱动、协议解析、分层缓存、批量上报与诊断日志串起来。
 *
 * 线程划分（目标构建映射到 RT-Thread 线程，仿真下由 rtos_port 调度）：
 *   can_rx_thr(prio 8)      中断/轮询把 FDCAN FIFO 元素搬进 SPSC 环形队列
 *   parse_thr(prio 12)      出队 -> 协议解析 -> 时间戳补打 -> 分层缓存
 *   cache_flush_thr(prio 14) 水位/周期触发 -> TLV 打包 -> 以太网上报
 *   bus_monitor_thr(prio 16) 错误计数、总线恢复、发送超时重试
 *   diag_thr(prio 20)        LCD 状态页、日志快照、SD 导出
 */
#include <string.h>
#include "app.h"
#include "hal_stub.h"
#include "board_init.h"
#include "mpu_config.h"
#include "cache_coherency.h"
#include "ring_buffer.h"
#include "mem_pool.h"
#include "diag_log.h"
#include "rtos_port.h"
#include "sdr_bsp.h"
#include "qspi_bsp.h"
#include "eth_rmii.h"
#include "sd_file.h"
#include "lcd_diag.h"

/* ---------------------------------------------------------------------------
 * 静态资源（全部落在 MPU 非缓存区，避免 Cache 与 DMA 不一致）
 * ------------------------------------------------------------------------- */
static uint8_t     s_can_pool_mem[H750_POOL_CAN_BLOCKS * (H750_POOL_CAN_BLOCK + 32u)];
static uint8_t     s_evt_pool_mem[H750_POOL_EVT_BLOCKS * (H750_POOL_EVT_BLOCK + 32u)];
static uint8_t     s_report_pool_mem[H750_POOL_REPORT_BLOCKS * (H750_POOL_REPORT_BLOCK + 32u)];
static uint8_t     s_small_pool_mem[H750_POOL_SMALL_BLOCKS * (H750_POOL_SMALL_BLOCK + 32u)];

static can_report_rec_t s_cache_p0[H750_CACHE_P0_SLOTS];
static can_report_rec_t s_cache_p1[H750_CACHE_P1_SLOTS];
static can_report_rec_t s_cache_p2[H750_CACHE_P2_SLOTS];

static diag_log_entry_t s_log_ring[H750_DIAG_LOG_ENTRIES];

typedef struct {
    fdcan_frame_t frame;      /* 中断侧填充，解析线程消费（按值传递，避免共享状态） */
    uint32_t      rx_ms;
    uint8_t       bus;
} app_rxq_item_t;

static app_rxq_item_t s_rxq_mem[H750_RING_CAN_RX_SLOTS];

typedef struct {
    /* 资源 */
    can_cache_t     cache;
    mem_pool_t      pool_can;
    mem_pool_t      pool_evt;
    mem_pool_t      pool_report;
    mem_pool_t      pool_small;
    ring_buffer_t   rxq;
    rtos_event_t    evt_rx;
    rtos_event_t    evt_report;
    rtos_event_t    evt_bus;
    rtos_event_t    evt_diag;
    /* 驱动状态 */
    fdcan_msgram_layout_t lay1;
    fdcan_msgram_layout_t lay2;
    fdcan_status_t  st1;
    fdcan_status_t  st2;
    uint8_t         can_ready;
    uint8_t         sniff_mode;
    /* 统计 */
    app_stats_t     stats;
    uint32_t        last_report_ms;
    uint32_t        last_diag_ms;
    uint32_t        last_heartbeat_ms;
    uint32_t        report_seq;
    uint8_t         report_buf[H750_REPORT_MAX_BYTES];
} app_ctx_t;

static app_ctx_t s_app;

/* ---------------------------------------------------------------------------
 * 线程实现
 * ------------------------------------------------------------------------- */
static void app_rx_thread(void *arg)
{
    (void)arg;
    s_app.stats.cpu_steps[APP_STAGE_RX] += app_rx_step();
}

static void app_parse_thread(void *arg)
{
    (void)arg;
    s_app.stats.cpu_steps[APP_STAGE_PARSE] += app_parse_step();
}

static void app_report_thread(void *arg)
{
    (void)arg;
    s_app.stats.cpu_steps[APP_STAGE_REPORT] += app_report_step();
}

static void app_bus_thread(void *arg)
{
    (void)arg;
    s_app.stats.cpu_steps[APP_STAGE_BUS] += app_bus_step();
}

static void app_diag_thread(void *arg)
{
    (void)arg;
    s_app.stats.cpu_steps[APP_STAGE_DIAG] += app_diag_step();
}

/* ---------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */
int app_init(void)
{
    uint32_t i;
    int rc;

    (void)memset(&s_app, 0, sizeof(s_app));

    rc = board_init();
    if (rc != 0) {
        return rc;
    }

    /* 1) 先配 MPU 与 Cache 策略，再开 Cache —— 顺序错误会引入不一致窗口 */
    mpu_configure_all();
    mpu_enable(0u);
    cc_init();

    /* 2) 内存池（固定在非缓存区的静态数组上，地址与 MPU 区域一一对应） */
    (void)mp_init(&s_app.pool_can, s_can_pool_mem, H750_POOL_CAN_BLOCK,
                  H750_POOL_CAN_BLOCKS, "can_frame");
    (void)mp_init(&s_app.pool_evt, s_evt_pool_mem, H750_POOL_EVT_BLOCK,
                  H750_POOL_EVT_BLOCKS, "can_event");
    (void)mp_init(&s_app.pool_report, s_report_pool_mem, H750_POOL_REPORT_BLOCK,
                  H750_POOL_REPORT_BLOCKS, "report_pkt");
    (void)mp_init(&s_app.pool_small, s_small_pool_mem, H750_POOL_SMALL_BLOCK,
                  H750_POOL_SMALL_BLOCKS, "small");

    /* 3) 中断与线程之间的无锁环形队列 */
    (void)rb_init(&s_app.rxq, s_rxq_mem, sizeof(app_rxq_item_t), H750_RING_CAN_RX_SLOTS);

    /* 4) 分层缓存 */
    (void)can_cache_init(&s_app.cache,
                         s_cache_p0, H750_CACHE_P0_SLOTS,
                         s_cache_p1, H750_CACHE_P1_SLOTS,
                         s_cache_p2, H750_CACHE_P2_SLOTS);

    /* 5) 诊断日志 */
    (void)diag_log_init(s_log_ring, H750_DIAG_LOG_ENTRIES);
    diag_log_set_level(H750_DIAG_LOG_LEVEL);

    /* 6) 事件 */
    rtos_event_init(&s_app.evt_rx);
    rtos_event_init(&s_app.evt_report);
    rtos_event_init(&s_app.evt_bus);
    rtos_event_init(&s_app.evt_diag);

    /* 7) 外设：SDRAM -> QSPI -> SD -> 以太网 -> LCD */
    (void)sdr_init();
    (void)qspi_init();
    (void)sd_block_init();
    if (fs_mount() != 0) {
        (void)fs_format();      /* 首次上电或文件系统损坏时重新格式化 */
    }
    (void)eth_init();
    (void)lcd_init();

    /* 8) FDCAN 双实例：位时序 + 消息 RAM 分区 + 过滤器分组 */
    if (fdcan_init(&h750_fdcan1_config, &s_app.lay1) != 0) {
        diag_log_write(H750_LOG_ERROR, DIAG_MOD_CAN1, 0x0110u, 0, 0u);
    }
    if (fdcan_init(&h750_fdcan2_config, &s_app.lay2) != 0) {
        diag_log_write(H750_LOG_ERROR, DIAG_MOD_CAN2, 0x0110u, 0, 0u);
    }
    (void)fdcan_start(H750_FDCAN1_BASE);
    (void)fdcan_start(H750_FDCAN2_BASE);
    s_app.can_ready = 1u;

    /* 9) 线程注册（优先级与栈深见 docs 的线程表） */
    (void)rtos_init();
    (void)rtos_thread_create("can_rx_thr", app_rx_thread, 0, 8u, 2048u);
    (void)rtos_thread_create("parse_thr", app_parse_thread, 0, 12u, 3072u);
    (void)rtos_thread_create("cache_flush_thr", app_report_thread, 0, 14u, 2048u);
    (void)rtos_thread_create("bus_monitor_thr", app_bus_thread, 0, 16u, 1536u);
    (void)rtos_thread_create("diag_thr", app_diag_thread, 0, 20u, 3072u);
    rtos_start();

    s_app.last_report_ms = hal_time_ms();
    s_app.last_diag_ms = hal_time_ms();
    s_app.last_heartbeat_ms = hal_time_ms();

    {
        uint8_t boot = 1u;
        diag_log_write(H750_LOG_INFO, DIAG_MOD_APP, DIAG_EV_BOOT, &boot, 1u);
    }

    /* 10) SDRAM 上电自检（长时间运行稳定性的第一道门槛） */
    {
        uint32_t errs = sdr_selftest(H750_SDRAM_BASE, 64u * 1024u);
        if (errs != 0u) {
            diag_log_write(H750_LOG_ERROR, DIAG_MOD_BSP, DIAG_EV_SDRAM_LINIT, &errs, 4u);
        }
    }
    for (i = 0u; i < APP_STAGE_COUNT; i++) {
        s_app.stats.cpu_steps[i] = 0u;
    }
    return 0;
}

const app_stats_t *app_get_stats(void)
{
    return &s_app.stats;
}

can_cache_t *app_cache(void)
{
    return &s_app.cache;
}

/* ---------------------------------------------------------------------------
 * 阶段 1：把 FDCAN 接收 FIFO 的元素搬进环形队列（ISR/DMA 语义）
 * ------------------------------------------------------------------------- */
static uint32_t rx_from_instance(const fdcan_config_t *cfg,
                                 const fdcan_msgram_layout_t *lay,
                                 uint32_t *fifo0, uint32_t *fifo1)
{
    uint32_t n = 0u;
    uint8_t fifo;

    for (fifo = 0u; fifo < 2u; fifo++) {
        for (;;) {
            fdcan_frame_t f;
            app_rxq_item_t item;
            int got = fdcan_read_rx(cfg, lay, fifo, &f);
            if (got <= 0) {
                break;
            }
            f.fifo = fifo;
            item.frame = f;
            item.rx_ms = hal_time_ms();
            item.bus = cfg->bus;
            if (rb_push(&s_app.rxq, &item) != 0) {
                s_app.stats.rx_dropped_ring++;
            } else {
                n++;
                if (fifo == 0u) { (*fifo0)++; } else { (*fifo1)++; }
            }
        }
    }
    return n;
}

uint32_t app_rx_step(void)
{
    uint32_t n = 0u;
    uint32_t f0 = 0u;
    uint32_t f1 = 0u;

    if (s_app.can_ready == 0u) {
        return 0u;
    }
    n += rx_from_instance(&h750_fdcan1_config, &s_app.lay1, &f0, &f1);
    n += rx_from_instance(&h750_fdcan2_config, &s_app.lay2, &f0, &f1);
    s_app.stats.rx_frames += n;
    s_app.stats.rx_fifo0 += f0;
    s_app.stats.rx_fifo1 += f1;
    s_app.stats.ticks = rtos_ticks();
    if (n > 0u) {
        rtos_event_set(&s_app.evt_rx, 0x1u);
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * 阶段 2：解析 + 分层缓存
 * ------------------------------------------------------------------------- */
uint32_t app_parse_step(void)
{
    uint32_t n = 0u;
    app_rxq_item_t item;

    while (rb_pop(&s_app.rxq, &item) == 0) {
        fdcan_frame_t f;
        can_report_rec_t rec;
        can_prio_t prio;
        int prc;

        (void)memset(&rec, 0, sizeof(rec));
        f = item.frame;

        s_app.stats.parsed++;
        prio = can_cache_prio_of_id(f.can_id, f.xtd);

        rec.can_id = f.can_id;
        rec.ts_us = hal_time_us();
        rec.seq = (uint32_t)(s_app.stats.parsed);
        rec.dlc = (uint8_t)fdcan_frame_payload_bytes(&f);
        rec.prio = (uint8_t)prio;
        rec.bus = item.bus;
        if (f.xtd != 0u) { rec.flags |= CAN_REC_FLAG_XTD; }
        if (f.fdf != 0u) { rec.flags |= CAN_REC_FLAG_FDF; }
        if (f.brs != 0u) { rec.flags |= CAN_REC_FLAG_BRS; }
        if (f.esi != 0u) { rec.flags |= CAN_REC_FLAG_ESI; }
        if (f.rtr != 0u) { rec.flags |= CAN_REC_FLAG_RTR; }
        if (f.fifo != 0u) { rec.flags |= CAN_REC_FLAG_FIFO1; }
        (void)memcpy(rec.data, f.data, rec.dlc);

        prc = can_cache_push(&s_app.cache, &rec);
        s_app.stats.cached++;
        if (prc > 0) {
            s_app.stats.cache_evicted++;
        } else if (prc < 0) {
            if (prio == CAN_PRIO_HIGH) {
                s_app.stats.drop_high++;
            } else if (prio == CAN_PRIO_MID) {
                s_app.stats.drop_mid++;
            } else {
                s_app.stats.drop_low++;
            }
        }
        n++;
        if (n >= 64u) {
            break;   /* 单步上限：避免长时间占用 CPU */
        }
    }
    {
        uint32_t wm = can_cache_watermark_pm(&s_app.cache);
        if (wm > s_app.stats.cache_peak_pm) {
            s_app.stats.cache_peak_pm = wm;
        }
    }
    if (can_cache_should_flush(&s_app.cache) != 0) {
        rtos_event_set(&s_app.evt_report, 0x1u);
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * 阶段 3：批量上报
 * ------------------------------------------------------------------------- */
uint32_t app_report_step(void)
{
    uint32_t now = hal_time_ms();
    uint32_t len = 0u;
    uint32_t records;

    if ((can_cache_should_flush(&s_app.cache) == 0) &&
        ((now - s_app.last_report_ms) < H750_REPORT_PERIOD_MS)) {
        return 0u;
    }
    records = can_cache_pack_tlv(&s_app.cache, s_app.report_buf,
                                 sizeof(s_app.report_buf), &len);
    s_app.last_report_ms = now;
    if (records == 0u) {
        return 0u;
    }
    if (eth_send_report(s_app.report_buf, len) == 0) {
        s_app.stats.reports++;
        s_app.stats.report_bytes += len;
        s_app.stats.report_records += records;
        s_app.report_seq++;
        diag_log_write(H750_LOG_DEBUG, DIAG_MOD_ETH, DIAG_EV_REPORT_SENT, &records, 4u);
    }
    rtos_event_clear(&s_app.evt_report, 0x1u);
    return records;
}

/* ---------------------------------------------------------------------------
 * 阶段 4：错误计数、总线恢复、发送重试
 * ------------------------------------------------------------------------- */
uint32_t app_bus_step(void)
{
    uint32_t actions = 0u;

    if (s_app.can_ready == 0u) {
        return 0u;
    }
    if (fdcan_handle_bus_error(&h750_fdcan1_config, &s_app.st1, hal_time_ms()) == 1) {
        s_app.stats.busoff++;
        actions++;
    }
    if (fdcan_bus_recovery_step(&h750_fdcan1_config, &s_app.st1, hal_time_ms()) == 1) {
        s_app.stats.recovered++;
        actions++;
    }
    if (fdcan_handle_bus_error(&h750_fdcan2_config, &s_app.st2, hal_time_ms()) == 1) {
        s_app.stats.busoff++;
        actions++;
    }
    if (fdcan_bus_recovery_step(&h750_fdcan2_config, &s_app.st2, hal_time_ms()) == 1) {
        s_app.stats.recovered++;
        actions++;
    }
    if ((s_app.st1.severe_fault != 0u) || (s_app.st2.severe_fault != 0u)) {
        s_app.stats.severe_fault = 1u;
        hal_led_set(H750_LED_ERR_PORT, H750_LED_ERR_PIN, 1);
    }

    {
        int rc1 = fdcan_poll_tx(&h750_fdcan1_config, &s_app.lay1, &s_app.st1, hal_time_ms());
        int rc2 = fdcan_poll_tx(&h750_fdcan2_config, &s_app.lay2, &s_app.st2, hal_time_ms());
        if (rc1 == 1 || rc2 == 1) { s_app.stats.tx_ok++; }
        if (rc1 == -1 || rc2 == -1) { s_app.stats.tx_retry++; }
        if (rc1 == -2 || rc2 == -2) { s_app.stats.tx_fail++; }
    }
    if (actions > 0u) {
        rtos_event_set(&s_app.evt_bus, 0x1u);
    }
    return actions;
}

/* ---------------------------------------------------------------------------
 * 阶段 5：诊断（LCD + 日志快照）
 * ------------------------------------------------------------------------- */
uint32_t app_lcd_render(void)
{
    lcd_status_t st;
    diag_stats_t ds;
    fs_stats_t fs;
    eth_stats_t es;

    (void)memset(&st, 0, sizeof(st));
    diag_log_get_stats(&ds);
    fs_get_stats(&fs);
    eth_get_stats(&es);

    st.can1_fps = s_app.stats.rx_frames;
    st.can2_fps = s_app.stats.rx_fifo1;
    st.can1_errors = s_app.st1.tec;
    st.can2_errors = s_app.st2.tec;
    st.cache_watermark_pm = can_cache_watermark_pm(&s_app.cache);
    st.cache_dropped = s_app.cache.total_dropped;
    st.sd_files = fs.file_count;
    st.log_errors = ds.error;
    st.link_up = es.link_up;
    st.sd_present = (uint8_t)sd_card_present();
    st.bus_off_can1 = s_app.st1.bus_off;
    st.bus_off_can2 = s_app.st2.bus_off;
    st.recovering = (uint8_t)(s_app.st1.recovering | s_app.st2.recovering);
    st.sniff_mode = s_app.sniff_mode;
    s_app.stats.lcd_frames++;
    return lcd_render_status(&st);
}

uint32_t app_diag_step(void)
{
    uint32_t now = hal_time_ms();
    uint32_t lit = 0u;

    if ((now - s_app.last_diag_ms) < 100u) {
        return 0u;
    }
    s_app.last_diag_ms = now;
    lit = app_lcd_render();
    {
        diag_stats_t ds;
        diag_log_get_stats(&ds);
        s_app.stats.log_entries = ds.count;
    }
    return lit;
}

uint32_t app_heartbeat_step(void)
{
    uint32_t now = hal_time_ms();
    fdcan_frame_t f;

    if ((now - s_app.last_heartbeat_ms) < 1000u) {
        return 0u;
    }
    s_app.last_heartbeat_ms = now;
    (void)memset(&f, 0, sizeof(f));
    f.can_id = H750_HEARTBEAT_ID;
    f.dlc = 8u;
    f.fdf = 1u;
    f.brs = 1u;
    f.data[0] = (uint8_t)(s_app.stats.rx_frames & 0xFFu);
    f.data[1] = (uint8_t)((s_app.stats.rx_frames >> 8) & 0xFFu);
    f.data[2] = (uint8_t)(s_app.stats.reports & 0xFFu);
    f.data[3] = 0x01u;   /* 心跳标志 */
    (void)fdcan_transmit(&h750_fdcan1_config, &s_app.lay1, &f);
    return 1u;
}

uint32_t app_run_ms(uint32_t ms)
{
    uint32_t n = 0u;
    while (n < ms) {
        rtos_run_ticks(1u);
        (void)app_heartbeat_step();
        n++;
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * 现场运维
 * ------------------------------------------------------------------------- */
int app_export_log_to_sd(void)
{
    static char text[4096];
    fs_file_t f;
    uint32_t len;

    if (sd_card_present() == 0) {
        return -1;
    }
    len = diag_log_dump_text(text, sizeof(text), 64u);
    if (len == 0u) {
        return -2;
    }
    if (fs_create("diag.log", &f) != 0) {
        if (fs_open("diag.log", &f) != 0) {
            return -3;
        }
    }
    if (fs_append(&f, text, len) != 0) {
        return -4;
    }
    (void)fs_close(&f);
    s_app.stats.sd_exports++;
    diag_log_write(H750_LOG_INFO, DIAG_MOD_SD, DIAG_EV_SD_EXPORT_OK, &len, 4u);
    return 0;
}

int app_export_offline_to_sd(void)
{
    /* 把分层缓存中的剩余条目导出为可读文本，便于现场离线取证 */
    static char text[2048];
    fs_file_t f;
    uint32_t n = 0u;
    uint32_t pos = 0u;
    can_report_rec_t rec;

    while (can_cache_pop(&s_app.cache, &rec) > 0) {
        if (pos > (sizeof(text) - 40u)) {
            break;
        }
        text[pos++] = (char)('0' + (rec.prio % 10u));
        text[pos++] = ',';
        {
            uint32_t v = rec.can_id;
            char tmp[10];
            uint32_t m = 0u;
            if (v == 0u) { tmp[m++] = '0'; }
            while (v > 0u) { tmp[m++] = (char)('0' + (v % 10u)); v /= 10u; }
            while ((m > 0u) && (pos < (sizeof(text) - 2u))) { text[pos++] = tmp[--m]; }
        }
        text[pos++] = '\n';
        n++;
    }
    if (n == 0u) {
        return -1;
    }
    if (fs_create("offline.csv", &f) != 0) {
        if (fs_open("offline.csv", &f) != 0) {
            return -2;
        }
    }
    if (fs_append(&f, text, pos) != 0) {
        return -3;
    }
    (void)fs_close(&f);
    s_app.stats.sd_exports++;
    return (int)n;
}

int app_set_sniff_mode(uint8_t enable)
{
    int previous_can1 = fdcan_get_sniff_mode(&h750_fdcan1_config);
    int rc = fdcan_set_sniff_mode(&h750_fdcan1_config, enable);
    if (rc != 0) {
        return rc;
    }
    rc = fdcan_set_sniff_mode(&h750_fdcan2_config, enable);
    if (rc != 0) {
        if (fdcan_set_sniff_mode(&h750_fdcan1_config, (uint8_t)previous_can1) != 0) {
            return -3;
        }
        return rc;
    }
    s_app.sniff_mode = (enable != 0u) ? 1u : 0u;
    diag_log_write(H750_LOG_WARN, DIAG_MOD_CAN1, 0x0120u, &s_app.sniff_mode, 1u);
    return 0;
}

int app_get_sniff_mode(void)
{
    return (int)s_app.sniff_mode;
}

int app_inject_frame(uint8_t bus, const fdcan_frame_t *f)
{
    uint32_t base = (bus == 0u) ? H750_FDCAN1_BASE : H750_FDCAN2_BASE;
#ifdef H750_PC_SIM
    return hal_sim_fdcan_inject(base, f);
#else
    (void)base;
    (void)f;
    return -1;
#endif
}

/* 目标构建的入口 */
#ifndef H750_PC_SIM
int main(void)
{
    if (app_init() != 0) {
        return -1;
    }
    for (;;) {
        (void)app_run_ms(1u);
        hal_watchdog_feed();
    }
}
#endif
