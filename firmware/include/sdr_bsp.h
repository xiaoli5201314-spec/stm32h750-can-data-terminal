/*
 * sdr_bsp.h
 * ---------------------------------------------------------------------------
 * SDRAM（FMC Bank1，16bit，32MB）BSP：按数据手册时序参数计算 FMC 寄存器、
 * 上电初始化命令序列、刷新周期配置与全片自检。
 *
 * 关键点：FMC_SDTR1 的所有字段都是"周期数 - 1"，因此必须先把 ns 级参数
 * 按当前 SDCLK 频率向上取整成周期数，再减 1 写入；否则在高温/低压角
 * 会出现 tRCD/tRP 不足导致的偶发读写错误。
 */
#ifndef H750_SDR_BSP_H
#define H750_SDR_BSP_H

#include <stdint.h>
#include "h750_config.h"

/* 器件时序参数（ns） */
typedef struct {
    uint32_t trcd_ns;   /* ACTIVE 到 READ/WRITE 命令延迟 */
    uint32_t trp_ns;    /* PRECHARGE 命令周期           */
    uint32_t tras_ns;   /* ACTIVE 到 PRECHARGE 最小延迟 */
    uint32_t trc_ns;    /* ACTIVE 到 ACTIVE 最小延迟    */
    uint32_t twr_ns;    /* 写恢复时间                   */
    uint32_t trfc_ns;   /* 自动刷新周期                 */
    uint32_t txsr_ns;   /* 退出自刷新到任意命令         */
    uint32_t tmrd_ns;   /* 模式寄存器设置到任意命令     */
} sdr_timing_ns_t;

/* 结构参数 */
typedef struct {
    uint32_t bank_bits;
    uint32_t row_bits;
    uint32_t col_bits;
    uint32_t width_bits;
    uint32_t cas_latency;
    uint32_t refresh_ms;
    uint32_t rows;
} sdr_geometry_t;

typedef struct {
    uint32_t sdtr;          /* FMC_SDTR1 值 */
    uint32_t tras_cycles;
    uint32_t trc_cycles;
    uint32_t trcd_cycles;
    uint32_t trp_cycles;
    uint32_t twr_cycles;
    uint32_t txsr_cycles;
    uint32_t tmrd_cycles;
    uint32_t trfc_cycles;
    uint32_t clk_hz;
    uint32_t worst_case_ns; /* 取整前的最大需求（用于余量核算） */
} sdr_timing_result_t;

/* ns -> 周期数：向上取整 */
uint32_t sdr_ns_to_cycles(uint32_t ns, uint32_t clk_hz);
/* 计算 FMC_SDTR1 */
int  sdr_calc_sdtr(const sdr_timing_ns_t *t, uint32_t clk_hz, sdr_timing_result_t *out);
/* 计算 FMC_SDRTR（刷新计数器） */
uint32_t sdr_calc_sdrtr(uint32_t clk_hz, uint32_t refresh_ms, uint32_t rows);
/* 计算 FMC_SDCR1 */
uint32_t sdr_calc_sdcr(const sdr_geometry_t *g, uint32_t hclk_hz, uint32_t sdram_clk_hz);
/* 时序余量核算：返回最小余量（千分比），< 100 表示余量不足 */
uint32_t sdr_timing_margin_pm(const sdr_timing_result_t *r, const sdr_timing_ns_t *t);

const sdr_timing_ns_t  *sdr_default_timing(void);
const sdr_geometry_t   *sdr_default_geometry(void);

/* 初始化：配置 FMC 并执行上电命令序列 */
int  sdr_init(void);
/* 全片自检：多种图案写入/读回比对，返回错误字节数（0 = 通过） */
uint32_t sdr_selftest(uint32_t base, uint32_t size);
/* 简单的读写带宽粗测（以 CPU 时钟周期估算，用于优化验证） */
uint32_t sdr_bandwidth_probe(uint32_t base, uint32_t size);

#endif /* H750_SDR_BSP_H */
