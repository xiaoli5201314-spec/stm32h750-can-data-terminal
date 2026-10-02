/*
 * hal_stub.h
 * ---------------------------------------------------------------------------
 * 平台抽象层（HAL）：把"驱动要用到但不属于驱动"的能力集中声明。
 *
 *   目标构建：由板级代码实现（时钟树、GPIO、TIM2 微秒时基、串口、看门狗）。
 *   仿真构建（H750_PC_SIM）：由 hal_stub.c 实现，包含寄存器文件、RAM 映射、
 *   时间仿真、FDCAN/PHY 外设模型与 Flash/SD 存储模型，使同一份驱动代码
 *   可以直接在 PC 上跑单元测试。
 */
#ifndef H750_HAL_STUB_H
#define H750_HAL_STUB_H

#include <stdint.h>
#include "h750_config.h"
#include "fdcan_driver.h"

/* ---- 时间基准 --------------------------------------------------------- */
uint32_t hal_time_ms(void);
uint32_t hal_time_us(void);
void     hal_delay_ms(uint32_t ms);
void     hal_delay_us(uint32_t us);

/* ---- 临界区 ----------------------------------------------------------- */
uint32_t hal_enter_critical(void);
void     hal_exit_critical(uint32_t state);

/* ---- 物理地址到可访问指针 ---------------------------------------------
 * 目标构建：直接把物理地址当指针（片内 SRAM/SDRAM 均可直接访问）。
 * 仿真构建：按已注册的内存区域查出主机缓冲首地址。
 */
void *hal_mem_ptr(uint32_t phys_addr, uint32_t size);
/* 仿真构建登记内存区域（目标构建为空实现） */
void  hal_mem_region_register(uint32_t base, uint32_t size, void *host, uint32_t host_size);

/* ---- 板级 I/O --------------------------------------------------------- */
void hal_gpio_init(uint32_t port, uint32_t pin, uint32_t mode, uint32_t af,
                   uint32_t otype, uint32_t ospeed, uint32_t pupd);
void hal_gpio_write(uint32_t port, uint32_t pin, int level);
int  hal_gpio_read(uint32_t port, uint32_t pin);
void hal_led_set(uint32_t port, uint32_t pin, int on);
void hal_uart_write(const char *s);
void hal_uart_putc(char c);
void hal_watchdog_feed(void);
void hal_reset(void);

/* ==========================================================================
 * 仿真接口（仅 H750_PC_SIM）
 * ========================================================================== */
#ifdef H750_PC_SIM

/* 全局仿真复位：清寄存器文件、内存区域与所有外设模型 */
void hal_sim_reset(void);
void hal_sim_advance_ms(uint32_t ms);
void hal_sim_advance_us(uint32_t us);
uint32_t hal_sim_elapsed_ms(void);

/* GPIO 输入注入（按键/卡检测/LINK 等） */
void hal_sim_gpio_set_input(uint32_t port, uint32_t pin, int level);

/* FDCAN 外设模型 */
void     hal_sim_fdcan_reset(uint32_t base);
void     hal_sim_fdcan_attach(uint32_t base, const fdcan_config_t *cfg,
                              const fdcan_msgram_layout_t *lay);
void     hal_sim_fdcan_reg_write(uint32_t base, uint32_t offset, uint32_t value);
uint32_t hal_sim_fdcan_reg_read(uint32_t base, uint32_t offset);
#define HAL_SIM_FDCAN_FAIL_INIT   (1u << 0)
#define HAL_SIM_FDCAN_FAIL_CCE    (1u << 1)
#define HAL_SIM_FDCAN_FAIL_GFC    (1u << 2)
#define HAL_SIM_FDCAN_FAIL_START  (1u << 3)
/* INIT acknowledgement delay in register reads; faults may be one-shot or sticky. */
void     hal_sim_fdcan_set_config_delay(uint32_t base, uint32_t reads);
void     hal_sim_fdcan_set_config_faults(uint32_t base, uint32_t faults, int once);
/* 向总线注入一帧：按已装载的过滤器路由，返回 FIFO 号；-1 表示被拒收，-2 未附着 */
int      hal_sim_fdcan_inject(uint32_t base, const fdcan_frame_t *f);
void     hal_sim_fdcan_set_ecr(uint32_t base, uint32_t tec, uint32_t rec, uint32_t cel);
void     hal_sim_fdcan_set_psr(uint32_t base, uint32_t psr);
/* 让发送请求一直挂起（模拟总线无应答），用于验证发送超时与重试 */
void     hal_sim_fdcan_set_tx_stuck(uint32_t base, int stuck);
void     hal_sim_fdcan_complete_tx(uint32_t base, int all);
uint32_t hal_sim_fdcan_tx_requests(uint32_t base);
uint32_t hal_sim_fdcan_injected(uint32_t base);
uint32_t hal_sim_fdcan_rejected(uint32_t base);
uint32_t hal_sim_fdcan_tx_completed(uint32_t base);

/* 以太网 PHY（MDIO）模型 */
void     hal_sim_mac_reg_write(uint32_t mac_base, uint32_t offset, uint32_t value);
void     hal_sim_phy_set(uint32_t phy_addr, uint32_t reg, uint16_t value);
uint16_t hal_sim_phy_get(uint32_t phy_addr, uint32_t reg);
uint32_t hal_sim_mdio_reads(void);
uint32_t hal_sim_mdio_writes(void);
void     hal_sim_eth_set_link(int up);

/* QSPI Flash 存储模型 */
void     hal_sim_flash_reset(void);
int      hal_sim_flash_read(uint32_t addr, void *dst, uint32_t len);
int      hal_sim_flash_program(uint32_t addr, const void *src, uint32_t len);
int      hal_sim_flash_erase_sector(uint32_t addr);
int      hal_sim_flash_erase_block(uint32_t addr);
int      hal_sim_flash_erase_chip(void);
uint32_t hal_sim_flash_erase_count(void);
uint32_t hal_sim_flash_program_count(void);
uint32_t hal_sim_flash_size(void);
/* 在指定地址预置数据（等价于出厂烧录内容） */
void     hal_sim_flash_preset(uint32_t addr, const void *src, uint32_t len);

/* SD 卡（块设备）模型 */
void     hal_sim_sd_reset(void);
void     hal_sim_sd_set_present(int present);
int      hal_sim_sd_present(void);
int      hal_sim_sd_read_sector(uint32_t sector, void *dst);
int      hal_sim_sd_write_sector(uint32_t sector, const void *src);
/* 设置写失败注入（验证文件系统断电恢复） */
void     hal_sim_sd_set_write_fail(int fail_after_writes);
uint32_t hal_sim_sd_write_count(void);
uint32_t hal_sim_sd_capacity_sectors(void);
int      hal_sim_sd_peek(uint32_t byte_offset, void *dst, uint32_t len);

#endif /* H750_PC_SIM */

#endif /* H750_HAL_STUB_H */
