/*
 * board_init.h
 * ---------------------------------------------------------------------------
 * 板级初始化：时钟树配置、电源域与等待周期、引脚复用表。
 *
 * 引脚表是本控制板"硬件资源规划"的可执行表达：文档 docs/HARDWARE.md 中的
 * 引脚分配表与这里的表一一对应，单元测试会校验其唯一性与数量。
 */
#ifndef H750_BOARD_INIT_H
#define H750_BOARD_INIT_H

#include <stdint.h>
#include "h750_config.h"

typedef struct {
    uint32_t port;       /* GPIO 基地址 */
    uint8_t  pin;        /* 引脚号 0..15 */
    uint8_t  mode;       /* GPIO_MODE_* */
    uint8_t  af;         /* 复用功能号（AF0..AF15） */
    uint8_t  otype;      /* 0 = 推挽，1 = 开漏 */
    uint8_t  ospeed;     /* GPIO_OSPEED_* */
    uint8_t  pupd;       /* 0 = 无，1 = 上拉，2 = 下拉 */
    const char *signal;  /* 网络名/功能名 */
} h750_pin_cfg_t;

typedef struct {
    uint32_t hse_hz;
    uint32_t sysclk_hz;
    uint32_t cpuclk_hz;
    uint32_t hclk_hz;
    uint32_t apb_hz;
    uint32_t pll1_vco_hz;
    uint32_t pll1q_hz;
    uint32_t fdcan_kernel_hz;
    uint32_t sdram_clk_hz;
    uint32_t qspi_clk_hz;
    uint32_t sdmmc_clk_hz;
    uint32_t ltdc_pixclk_hz;
} board_clock_t;

extern const h750_pin_cfg_t h750_pin_table[];
extern const uint32_t       h750_pin_table_size;

/* 时钟树：HSE 25MHz -> PLL1 -> SYSCLK 480MHz，各域分频 */
int  board_clock_init(void);
const board_clock_t *board_clock_get(void);
/* 计算 SYSCLK（由 HSE 与 PLL 参数推出，用于自检与文档一致性） */
uint32_t board_calc_sysclk(uint32_t hse_hz, uint32_t m, uint32_t n, uint32_t p);
uint32_t board_calc_pll_vco(uint32_t hse_hz, uint32_t m, uint32_t n);
/* 计算 Flash 等待周期（按参考手册的 VOS0 频率-等待周期表） */
uint32_t board_flash_latency_for(uint32_t cpuclk_hz, uint8_t vos);
/* SDRAM 时钟分频编码（FMC_SDCR.SDCLK） */
uint32_t board_sdclk_div_code(uint32_t hclk_hz, uint32_t sdram_clk_hz);

/* 引脚复用与板级外设初始化 */
void board_gpio_init(void);
int  board_init(void);
/* 引脚表自检：返回 0 表示无重复/数量正确 */
int  board_pin_table_check(void);
uint32_t board_pin_count_by_af(uint8_t af);

#endif /* H750_BOARD_INIT_H */
