/*
 * diag_log.h
 * ---------------------------------------------------------------------------
 * 诊断日志与报文追踪：环形日志 + 分级过滤 + 统计快照 + 文本导出。
 *
 *   - 存储介质由调用方提供（本项目放在 SDRAM 日志环/非缓存区），
 *     掉电前可整体导出到 SD 卡，实现"无上位机条件下的现场状态查看与日志导出"；
 *   - 条目固定 24 字节，写入路径无动态分配、无格式化开销；
 *   - diag_log_trace() 为每条关键报文登记追踪序号，用于事后定位丢包/超时。
 */
#ifndef H750_DIAG_LOG_H
#define H750_DIAG_LOG_H

#include <stdint.h>
#include "h750_config.h"

/* 模块编号 */
#define DIAG_MOD_APP     0u
#define DIAG_MOD_CAN1    1u
#define DIAG_MOD_CAN2    2u
#define DIAG_MOD_ETH     3u
#define DIAG_MOD_SD      4u
#define DIAG_MOD_LCD     5u
#define DIAG_MOD_MPU     6u
#define DIAG_MOD_CACHE   7u
#define DIAG_MOD_POOL    8u
#define DIAG_MOD_BSP     9u
#define DIAG_MOD_MAX     10u

typedef struct {
    uint32_t ts_us;      /* 微秒时间戳 */
    uint32_t seq;        /* 全局递增序号（用于判断覆盖） */
    uint16_t module;     /* 模块号 */
    uint16_t code;       /* 事件码 */
    uint8_t  level;      /* 日志级别 */
    uint8_t  len;        /* payload 有效字节 */
    uint8_t  payload[10];/* 事件附加数据 */
} diag_log_entry_t;

typedef struct {
    uint32_t total;
    uint32_t error;
    uint32_t warn;
    uint32_t info;
    uint32_t debug;
    uint32_t dropped;      /* 因级别过滤而丢弃 */
    uint32_t capacity;
    uint32_t count;        /* 当前有效条目 */
    uint32_t wraps;        /* 环形覆盖次数 */
    uint32_t last_error_ts;
    uint16_t last_error_code;
    uint16_t last_error_module;
} diag_stats_t;

/* 常用事件码 */
#define DIAG_EV_BOOT            0x0001u
#define DIAG_EV_HEARTBEAT       0x0002u
#define DIAG_EV_CAN_BUSOFF      0x0100u
#define DIAG_EV_CAN_RECOVERED   0x0101u
#define DIAG_EV_CAN_TX_TIMEOUT  0x0102u
#define DIAG_EV_CAN_TX_RETRY    0x0103u
#define DIAG_EV_CAN_RX_OVF      0x0104u
#define DIAG_EV_CACHE_MISS_FIX  0x0200u
#define DIAG_EV_CACHE_DIRTY_LOST 0x0201u
#define DIAG_EV_MPU_FAULT       0x0202u
#define DIAG_EV_ETH_LINK_UP     0x0300u
#define DIAG_EV_ETH_LINK_DOWN   0x0301u
#define DIAG_EV_ETH_TX_FAIL     0x0302u
#define DIAG_EV_SD_MOUNT_OK     0x0400u
#define DIAG_EV_SD_MOUNT_FAIL   0x0401u
#define DIAG_EV_SD_EXPORT_OK    0x0402u
#define DIAG_EV_POOL_EXHAUSTED  0x0500u
#define DIAG_EV_CACHE_DROP_LOW  0x0600u
#define DIAG_EV_CACHE_DROP_HIGH 0x0601u
#define DIAG_EV_REPORT_SENT     0x0700u
#define DIAG_EV_SDRAM_LINIT     0x0800u

int      diag_log_init(void *storage, uint32_t entries);
void     diag_log_set_level(uint8_t level);
uint8_t  diag_log_get_level(void);
void     diag_log_write(uint8_t level, uint16_t module, uint16_t code,
                        const void *payload, uint8_t len);
/* 报文追踪：seq / can_id / fifo / 结果码 */
void     diag_log_trace(uint16_t module, uint32_t trace_seq, uint32_t can_id,
                        uint8_t fifo, uint8_t result);
uint32_t diag_log_count(void);
uint32_t diag_log_capacity(void);
uint32_t diag_log_total(void);
uint32_t diag_log_dropped(void);
/* idx = 0 表示最旧条目 */
int      diag_log_get(uint32_t idx, diag_log_entry_t *out);
void     diag_log_get_stats(diag_stats_t *st);
void     diag_log_reset(void);
/* 导出为可读文本（用于串口/ SD 卡导出），返回写入字节数 */
uint32_t diag_log_dump_text(char *buf, uint32_t cap, uint32_t max_entries);
const char *diag_module_name(uint16_t module);
const char *diag_level_name(uint8_t level);

#endif /* H750_DIAG_LOG_H */
