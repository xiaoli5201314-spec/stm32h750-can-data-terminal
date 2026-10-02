/*
 * app.h
 * ---------------------------------------------------------------------------
 * 应用层装配与流水线接口（采集 -> 解析 -> 分层缓存 -> 批量上报 -> 诊断）。
 * 单元测试与集成测试都通过这些接口驱动整条链路。
 */
#ifndef H750_APP_H
#define H750_APP_H

#include <stdint.h>
#include "h750_config.h"
#include "can_cache.h"
#include "fdcan_driver.h"

#define APP_STAGE_RX        0u
#define APP_STAGE_PARSE     1u
#define APP_STAGE_REPORT    2u
#define APP_STAGE_BUS       3u
#define APP_STAGE_DIAG      4u
#define APP_STAGE_COUNT     5u

typedef struct {
    uint32_t rx_frames;
    uint32_t rx_fifo0;
    uint32_t rx_fifo1;
    uint32_t rx_dropped_ring;
    uint32_t parsed;
    uint32_t cached;
    uint32_t cache_evicted;
    uint32_t reports;
    uint32_t report_bytes;
    uint32_t report_records;
    uint32_t drop_low;
    uint32_t drop_mid;
    uint32_t drop_high;
    uint32_t tx_ok;
    uint32_t tx_retry;
    uint32_t tx_fail;
    uint32_t busoff;
    uint32_t recovered;
    uint32_t severe_fault;
    uint32_t pool_exhausted;
    uint32_t sd_exports;
    uint32_t log_entries;
    uint32_t lcd_frames;
    uint32_t cpu_steps[APP_STAGE_COUNT];
    uint32_t cache_peak_pm;
    uint32_t ticks;
} app_stats_t;

int      app_init(void);
const app_stats_t *app_get_stats(void);
can_cache_t *app_cache(void);

/* 流水线各阶段（每个调度 tick 调用一次，返回本步处理的条目数） */
uint32_t app_rx_step(void);
uint32_t app_parse_step(void);
uint32_t app_report_step(void);
uint32_t app_bus_step(void);
uint32_t app_diag_step(void);
uint32_t app_heartbeat_step(void);

/* 运行流水线若干毫秒（仿真下按 tick 推进时间） */
uint32_t app_run_ms(uint32_t ms);

/* 现场运维动作 */
int      app_export_log_to_sd(void);
int      app_export_offline_to_sd(void);
int      app_set_sniff_mode(uint8_t enable);
int      app_get_sniff_mode(void);
uint32_t app_lcd_render(void);

/* 测试注入：等价于总线收到一帧 */
int      app_inject_frame(uint8_t bus, const fdcan_frame_t *f);

#endif /* H750_APP_H */
