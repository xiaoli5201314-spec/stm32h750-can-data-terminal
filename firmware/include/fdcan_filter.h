/*
 * fdcan_filter.h
 * ---------------------------------------------------------------------------
 * FDCAN 过滤器分组设计与路由。按 Bosch M_CAN（STM32H7 FDCAN）的报文 RAM
 * 过滤器元素语义实现，纯逻辑部分与硬件解耦，便于表驱动测试与 Python 交叉验证。
 *
 * 标准过滤器元素（1 word）：
 *   W0 = SFT[31:30] | SFEC[29:27] | SFID1[26:16] | SFID2[15:0]
 * 扩展过滤器元素（2 word）：
 *   F0 = EFEC[31:29] | EFID1[28:0]
 *   F1 = EFT[31:30]  | EFID2[28:0]
 *
 * 过滤类型（SFT/EFT）语义：
 *   00 范围过滤（扩展帧时同时与 XIDAM 相与）
 *   01 双 ID 过滤（等于 ID1 或 ID2 即命中）
 *   10 经典过滤（ID1 = 过滤器，ID2 = 掩码）
 *   11 范围过滤（不应用 XIDAM）
 */
#ifndef H750_FDCAN_FILTER_H
#define H750_FDCAN_FILTER_H

#include <stdint.h>
#include "h750_config.h"

/* 过滤类型（数值与寄存器字段一致） */
#define FDCAN_FTYPE_RANGE          0u
#define FDCAN_FTYPE_DUAL           1u
#define FDCAN_FTYPE_MASK           2u
#define FDCAN_FTYPE_RANGE_NO_EIDM  3u

/* 元素配置（SFEC/EFEC，数值与寄存器字段一致） */
#define FDCAN_FECFG_DISABLE        0u
#define FDCAN_FECFG_FIFO0          1u
#define FDCAN_FECFG_FIFO1          2u
#define FDCAN_FECFG_REJECT         3u
#define FDCAN_FECFG_PRIORITY       4u
#define FDCAN_FECFG_FIFO0_PRIO     5u
#define FDCAN_FECFG_FIFO1_PRIO     6u
#define FDCAN_FECFG_RXBUFFER       7u

/* 路由结果 */
#define FDCAN_ROUTE_NONE   (-1)
#define FDCAN_ROUTE_FIFO0  0
#define FDCAN_ROUTE_FIFO1  1
#define FDCAN_ROUTE_REJECT 2
#define FDCAN_ROUTE_RXBUF  3

typedef struct {
    uint8_t     index;        /* 该类型过滤器内的索引（决定消息 RAM 中的位置） */
    uint8_t     id_type;      /* 0 = 标准帧，1 = 扩展帧 */
    uint8_t     filter_type;  /* FDCAN_FTYPE_* */
    uint8_t     fec;          /* FDCAN_FECFG_* */
    uint32_t    id1;
    uint32_t    id2;
    const char *group_name;   /* 分组名（文档/日志用） */
} fdcan_filter_rule_t;

typedef struct {
    const fdcan_filter_rule_t *std_rules;
    uint32_t                   std_count;
    const fdcan_filter_rule_t *ext_rules;
    uint32_t                   ext_count;
    uint32_t                   xidam;              /* 扩展 ID AND 掩码 */
    uint8_t                    anfs;               /* 标准帧不匹配时：0=FIFO0,1=FIFO1,2/3=拒收 */
    uint8_t                    anfe;               /* 扩展帧不匹配时同上 */
    uint8_t                    rrfs;               /* 拒收标准远程帧 */
    uint8_t                    rrfe;               /* 拒收扩展远程帧 */
} fdcan_filter_config_t;

/* ---- 纯逻辑匹配 / 路由 ------------------------------------------------ */
/* 单条规则是否命中 */
int fdcan_filter_rule_match(const fdcan_filter_rule_t *r, uint32_t id, uint8_t xtd);
/* 路由：返回 FDCAN_ROUTE_*；hp_out 置 1 表示命中高优先级过滤器 */
int fdcan_filter_route(const fdcan_filter_config_t *cfg, uint32_t id, uint8_t xtd,
                       uint8_t *hp_out);
/* 统计某条报文命中的过滤器索引（未命中返回 -1） */
int fdcan_filter_match_index(const fdcan_filter_config_t *cfg, uint32_t id,
                            uint8_t xtd, uint8_t *hp_out);

/* ---- 寄存器编码 ------------------------------------------------------- */
uint32_t fdcan_filter_encode_std(const fdcan_filter_rule_t *r);
void     fdcan_filter_encode_ext(const fdcan_filter_rule_t *r, uint32_t *f0, uint32_t *f1);
uint32_t fdcan_filter_gfc_value(const fdcan_filter_config_t *cfg);
uint32_t fdcan_filter_sidfc_value(uint32_t word_offset, uint32_t count);
uint32_t fdcan_filter_xidfc_value(uint32_t word_offset, uint32_t count);

/* ---- 消息 RAM 编程 ---------------------------------------------------- */
struct fdcan_msgram_layout;
int fdcan_filter_program(uint32_t base, uint32_t msgram_base,
                         const fdcan_filter_config_t *cfg,
                         const struct fdcan_msgram_layout *lay);

/* ---- 板级默认过滤器分组表 --------------------------------------------- */
extern const fdcan_filter_rule_t h750_can1_std_filters[];
extern const uint32_t            h750_can1_std_filter_count;
extern const fdcan_filter_rule_t h750_can1_ext_filters[];
extern const uint32_t            h750_can1_ext_filter_count;
extern const fdcan_filter_rule_t h750_can2_std_filters[];
extern const uint32_t            h750_can2_std_filter_count;
extern const fdcan_filter_rule_t h750_can2_ext_filters[];
extern const uint32_t            h750_can2_ext_filter_count;

const fdcan_filter_config_t *fdcan_filter_config_can1(void);
const fdcan_filter_config_t *fdcan_filter_config_can2(void);
extern const fdcan_filter_config_t h750_can1_filter_cfg;
extern const fdcan_filter_config_t h750_can2_filter_cfg;

#endif /* H750_FDCAN_FILTER_H */
