/*
 * can_cache.h
 * ---------------------------------------------------------------------------
 * 高频报文分层缓存与批量上报。
 *
 * 三层缓存（按业务优先级分层，而不是按 ID 简单排队）：
 *   P0(HIGH) 64 槽  —— 报警/事件/时间同步：原则上不可丢
 *   P1(MID)  256 槽 —— 周期关键状态：尽量保留
 *   P2(LOW)  512 槽 —— 高频观测数据：允许丢弃
 *
 * 溢出策略（"丢低优先级保高优先级"）：
 *   1) 低优先级条目先被淘汰；
 *   2) 中优先级入队而 P1 满时，优先牺牲一条 P2 老条目腾位置；
 *   3) 高优先级入队而 P0 满时，依次牺牲 P2 老条目、P1 老条目；
 *   4) 只有三层都被高优先级占满时，才丢弃最老的 P0 条目，并置严重标志。
 * 所有丢弃都分方向计数（dropped_low / dropped_mid / dropped_high），可观测。
 */
#ifndef H750_CAN_CACHE_H
#define H750_CAN_CACHE_H

#include <stdint.h>
#include "h750_config.h"

typedef enum {
    CAN_PRIO_LOW  = 0,
    CAN_PRIO_MID  = 1,
    CAN_PRIO_HIGH = 2,
    CAN_PRIO_COUNT = 3
} can_prio_t;

/* 上报记录标志位 */
#define CAN_REC_FLAG_XTD    (1u << 0)
#define CAN_REC_FLAG_FDF    (1u << 1)
#define CAN_REC_FLAG_BRS    (1u << 2)
#define CAN_REC_FLAG_ESI    (1u << 3)
#define CAN_REC_FLAG_RTR    (1u << 4)
#define CAN_REC_FLAG_FIFO1  (1u << 5)

/* 统一紧凑上报记录：16 字节头 + 64 字节数据 = 80 字节 */
typedef struct {
    uint32_t can_id;
    uint32_t ts_us;
    uint32_t seq;
    uint8_t  dlc;
    uint8_t  flags;
    uint8_t  prio;
    uint8_t  bus;          /* 0 = FDCAN1, 1 = FDCAN2 */
    uint8_t  data[H750_CAN_MAX_DATA];
} can_report_rec_t;

typedef struct {
    can_report_rec_t *slots;      /* 槽数组 */
    uint32_t          capacity;
    uint32_t          head;       /* 环形写指针 */
    uint32_t          count;
    uint32_t          enqueued;
    uint32_t          dequeued;
    uint32_t          dropped_low;
    uint32_t          dropped_mid;
    uint32_t          dropped_high;
    uint32_t          evicted_low_for_higher;   /* 因高优先级入队而牺牲的低优先级条目数 */
    uint32_t          peak;
    uint32_t          enqueue_fail;
} can_cache_layer_t;

typedef struct {
    can_cache_layer_t p0;
    can_cache_layer_t p1;
    can_cache_layer_t p2;
    uint32_t          total_enqueued;
    uint32_t          total_dropped;
    uint32_t          pack_calls;
    uint32_t          pack_bytes;
    uint32_t          last_pack_records;
    uint8_t           high_overflow_alarm;   /* P0 被迫丢弃时置 1 */
} can_cache_t;

#define CAN_TLV_TYPE_RECORD   0x01u
#define CAN_TLV_TYPE_STATS    0x02u
#define CAN_PACK_HEADER_BYTES 8u
#define CAN_TLV_HEADER_BYTES  2u
#define CAN_PACK_RECORD_BYTES 16u   /* 打包后的记录头：id4+flags1+dlc1+rsv2+ts4+seq4 */

int      can_cache_init(can_cache_t *c,
                        can_report_rec_t *p0, uint32_t p0_slots,
                        can_report_rec_t *p1, uint32_t p1_slots,
                        can_report_rec_t *p2, uint32_t p2_slots);
/* 入队：返回 0 正常入队，1 入队成功但牺牲了低优先级条目，-1 被丢弃 */
int      can_cache_push(can_cache_t *c, const can_report_rec_t *rec);
/* 按优先级 P0->P1->P2 出队一条：返回 1/2/3 表示来自 P0/P1/P2，0 表示空 */
int      can_cache_pop(can_cache_t *c, can_report_rec_t *out);
/* 只看不取（打包时先算长度再决定是否取走） */
int      can_cache_peek(const can_cache_t *c, can_report_rec_t *out);
uint32_t can_cache_count(const can_cache_t *c);
uint32_t can_cache_capacity(const can_cache_t *c);
uint32_t can_cache_capacity_of(const can_cache_t *c, can_prio_t prio);
uint32_t can_cache_count_of(const can_cache_t *c, can_prio_t prio);
uint32_t can_cache_watermark_pm(const can_cache_t *c);
/* 是否需要立即上报：水位 >= 阈值 或 P0 非空 */
int      can_cache_should_flush(const can_cache_t *c);
/* 批量打包为 TLV 报文，返回打包的记录条数，*out_len 为字节数 */
uint32_t can_cache_pack_tlv(can_cache_t *c, uint8_t *buf, uint32_t cap, uint32_t *out_len);
/* 解包（用于联调自检与单元测试） */
uint32_t can_cache_unpack_tlv(const uint8_t *buf, uint32_t len,
                              can_report_rec_t *out, uint32_t max_out);
/* 由 CAN ID 判定优先级（与报文 ID 规划表一致） */
can_prio_t can_cache_prio_of_id(uint32_t can_id, uint8_t xtd);
void     can_cache_reset(can_cache_t *c);

#endif /* H750_CAN_CACHE_H */
