/*
 * fdcan_driver.h
 * ---------------------------------------------------------------------------
 * FDCAN 驱动：位时序计算、消息 RAM 分区、过滤器装载、接收/发送、
 * 时间戳、错误计数与总线恢复状态机、发送超时重试。
 *
 * 位时序（CAN FD）：
 *   标称段与数据段分别用 NBTP / DBTP 配置；本工程目标采样点 80%。
 *   NBRP/DBRP 为"分频值 - 1"，NTSEG1/DTSEG1 等均为"时间段 - 1"。
 *   每个位的 tq 总数 = 1(SYNC) + (TSEG1 + 1) + (TSEG2 + 1)
 *                    = NTSEG1 + NTSEG2 + 3
 *   采样点 = (1 + NTSEG1 + 1) / tq 总数
 */
#ifndef H750_FDCAN_DRIVER_H
#define H750_FDCAN_DRIVER_H

#include <stdint.h>
#include "h750_config.h"
#include "fdcan_filter.h"

/* ---- 消息 RAM 分区 ---------------------------------------------------- */
typedef struct fdcan_msgram_layout {
    uint32_t std_filter_words;   /* 标准过滤器区起始 word 偏移 */
    uint32_t ext_filter_words;
    uint32_t rx0_words;
    uint32_t rx1_words;
    uint32_t txevt_words;
    uint32_t txbuf_words;
    uint32_t std_filter_count;
    uint32_t ext_filter_count;
    uint32_t rx0_count;
    uint32_t rx1_count;
    uint32_t txevt_count;
    uint32_t txbuf_count;
    uint32_t elem_words;         /* 每个报文元素占用的 word 数 */
    uint32_t total_words;
} fdcan_msgram_layout_t;

int fdcan_layout_compute(uint32_t std_filters, uint32_t ext_filters,
                         uint32_t rx0_elems, uint32_t rx1_elems,
                         uint32_t tx_elems, uint32_t txevt_elems,
                         uint32_t elem_words, fdcan_msgram_layout_t *out);
/* 由 DLC 得到元素数据区尺寸编码（RXESC/TXESC） */
uint32_t fdcan_dlc_to_elem_size_code(uint8_t dlc);
uint32_t fdcan_dlc_to_bytes(uint8_t dlc);

/* ---- 位时序 ----------------------------------------------------------- */
typedef struct {
    uint32_t nbrp;                  /* 写入 NBTP.NBRP（分频 - 1） */
    uint32_t ntseg1;
    uint32_t ntseg2;
    uint32_t nsjw;
    uint32_t dbrp;
    uint32_t dtseg1;
    uint32_t dtseg2;
    uint32_t dsjw;
    uint32_t nominal_bps;           /* 实际可达标称速率 */
    uint32_t data_bps;
    uint32_t nominal_sp_pm;         /* 实际采样点（千分比） */
    uint32_t data_sp_pm;
    uint32_t nominal_tq;            /* 标称段每比特 tq 数 */
    uint32_t data_tq;
    uint8_t  tdc_enable;            /* 数据段速率 > 1Mbps 时建议使能 TDC */
} fdcan_bit_timing_t;

int      fdcan_bit_timing_calc(uint32_t kernel_hz, uint32_t nominal_bps,
                               uint32_t data_bps, uint32_t sp_target_pm,
                               fdcan_bit_timing_t *out);
uint32_t fdcan_nbtp_value(const fdcan_bit_timing_t *bt);
uint32_t fdcan_dbtp_value(const fdcan_bit_timing_t *bt);
uint32_t fdcan_tdcr_value(uint32_t kernel_hz, uint32_t data_bps,
                          uint32_t loop_delay_ns, uint32_t tdcf_min);

/* ---- 报文 ------------------------------------------------------------- */
typedef struct {
    uint32_t can_id;
    uint8_t  xtd;
    uint8_t  rtr;
    uint8_t  fdf;      /* 1 = CAN FD 格式 */
    uint8_t  brs;      /* 1 = 数据段变速 */
    uint8_t  esi;
    uint8_t  dlc;
    uint8_t  data[H750_CAN_MAX_DATA];
    uint16_t rx_ts;    /* 硬件时间戳（位时间计数） */
    uint8_t  fifo;     /* 0/1 */
    uint8_t  filter_index;
    uint8_t  anmf;     /* 1 = 未匹配任何过滤器（嗅探模式） */
    uint8_t  hp;       /* 1 = 命中高优先级过滤器 */
} fdcan_frame_t;

/* Actual payload bytes: RTR = 0, classic = min(DLC, 8), FD uses the DLC map. */
uint32_t fdcan_frame_payload_bytes(const fdcan_frame_t *frame);

/* ---- 配置与状态 ------------------------------------------------------- */
typedef struct {
    uint32_t base;                  /* H750_FDCAN1_BASE / H750_FDCAN2_BASE */
    uint32_t msgram_base;           /* 该实例消息 RAM 基地址 */
    uint32_t kernel_hz;
    uint32_t nominal_bps;
    uint32_t data_bps;
    uint32_t sp_target_pm;
    uint8_t  bus;                   /* 0/1，用于日志与缓存分层 */
    uint8_t  fd_enable;
    uint8_t  brs_enable;
    uint8_t  loopback;
    uint8_t  tx_retry_max;
    uint32_t tx_timeout_ms;
    const fdcan_filter_config_t *filters;
} fdcan_config_t;

typedef struct {
    uint32_t tec;
    uint32_t rec;
    uint32_t cel;
    uint32_t lec;
    uint32_t dlec;
    uint32_t psr;
    uint8_t  bus_off;
    uint8_t  error_passive;
    uint8_t  error_warning;
    uint8_t  activity;
    uint32_t rx_frames;
    uint32_t rx_fifo0;
    uint32_t rx_fifo1;
    uint32_t rx_rejected;
    uint32_t tx_frames;
    uint32_t tx_timeouts;
    uint32_t tx_retries;
    uint32_t tx_failures;
    uint32_t busoff_events;
    uint32_t recover_events;
    uint32_t consecutive_busoff;
    uint32_t backoff_ms;            /* 当前退避时长 */
    uint8_t  recovering;            /* 恢复流程进行中 */
    uint8_t  severe_fault;          /* 连续多次恢复失败 */
    uint32_t recovery_started_ms;
    uint32_t last_busoff_ms;
} fdcan_status_t;

/* ---- 接口 ------------------------------------------------------------- */
int  fdcan_init(const fdcan_config_t *cfg, fdcan_msgram_layout_t *layout_out);
int  fdcan_start(uint32_t base);
int  fdcan_stop(uint32_t base);
/* 从 FIFO 读取一帧并应答：返回 1 = 读到，0 = 空，-1 = 参数错误 */
int  fdcan_read_rx(const fdcan_config_t *cfg, const fdcan_msgram_layout_t *lay,
                   uint8_t fifo, fdcan_frame_t *out);
/* 发送一帧：写入 TX 缓冲并挂起请求 */
int  fdcan_transmit(const fdcan_config_t *cfg, const fdcan_msgram_layout_t *lay,
                    const fdcan_frame_t *f);
/* 轮询发送完成状态与超时重试：返回 0 空闲，1 完成，-1 超时重试中，-2 最终失败 */
int  fdcan_poll_tx(const fdcan_config_t *cfg, const fdcan_msgram_layout_t *lay,
                   fdcan_status_t *st, uint32_t now_ms);
void fdcan_get_status(uint32_t base, fdcan_status_t *st);
/* 处理错误计数变化与总线关闭事件（在错误中断/周期任务中调用） */
int  fdcan_handle_bus_error(const fdcan_config_t *cfg, fdcan_status_t *st,
                            uint32_t now_ms);
/* 总线恢复状态机推进：返回 0 空闲，1 已完成一次恢复，2 退避等待中，3 严重故障 */
int  fdcan_bus_recovery_step(const fdcan_config_t *cfg, fdcan_status_t *st,
                             uint32_t now_ms);
/* 硬件位时间计数 -> 微秒（用于时间戳） */
uint32_t fdcan_timestamp_to_us(uint16_t rx_ts, uint32_t nominal_bps);
/* Unmatched frames -> FIFO0; preserves prior INIT/CCE state.
 * 0 success, -1 invalid config, -2 switch failed but restored, -3 restoration failed.
 * Caller must serialize controller configuration and recovery operations.
 */
int  fdcan_set_sniff_mode(const fdcan_config_t *cfg, uint8_t enable);
int  fdcan_get_sniff_mode(const fdcan_config_t *cfg);

extern const fdcan_config_t h750_fdcan1_config;
extern const fdcan_config_t h750_fdcan2_config;

#endif /* H750_FDCAN_DRIVER_H */
