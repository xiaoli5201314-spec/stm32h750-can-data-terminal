/*
 * qspi_bsp.h
 * ---------------------------------------------------------------------------
 * QSPI Flash（8MB SPI NOR，四线）BSP：初始化、JEDEC ID 读取、
 * 间接读写、扇区/块擦除、内存映射（XIP）模式切换。
 */
#ifndef H750_QSPI_BSP_H
#define H750_QSPI_BSP_H

#include <stdint.h>
#include "h750_config.h"

typedef struct {
    uint32_t cr;      /* 控制寄存器 */
    uint32_t dcr;     /* 器件配置寄存器 */
    uint32_t prescaler;
    uint32_t fsize;
    uint32_t clk_hz;
} qspi_cfg_t;

int      qspi_init(void);
const qspi_cfg_t *qspi_get_cfg(void);
/* 读 JEDEC ID（3 字节），返回 0 成功 */
int      qspi_read_jedec_id(uint8_t id[3]);
/* 读状态寄存器 */
int      qspi_read_status(uint8_t *sr);
int      qspi_write_enable(void);
/* 间接读/写 */
int      qspi_indirect_read(uint32_t addr, void *dst, uint32_t len);
int      qspi_indirect_write(uint32_t addr, const void *src, uint32_t len);
/* 擦除 */
int      qspi_erase_sector(uint32_t addr);
int      qspi_erase_block(uint32_t addr);
int      qspi_erase_chip(void);
/* 内存映射模式开关（XIP） */
int      qspi_memory_mapped_enable(void);
int      qspi_memory_mapped_disable(void);
/* 等待器件空闲（轮询状态寄存器 WIP 位） */
int      qspi_wait_ready(uint32_t timeout_ms);
/* 计算 FSIZE 字段：器件容量 2^(FSIZE+1) 字节 */
uint32_t qspi_fsize_field(uint32_t bytes);
uint32_t qspi_prescaler_for(uint32_t kernel_hz, uint32_t target_hz);

#endif /* H750_QSPI_BSP_H */
