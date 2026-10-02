/*
 * mpu_config.h
 * ---------------------------------------------------------------------------
 * Cortex-M7 MPU 区域规划：把"业务用途"映射为 MPU 属性，并生成 MPU_RBAR /
 * MPU_RASR 寄存器值（按 ARMv7-M 架构参考手册的编码规则）。
 *
 * 这是本项目解决 Cache 与 DMA 一致性问题的第一层手段：
 * 通过内存属性划分，让 DMA 缓冲区天然不经过 D-Cache，从根上消除维护负担。
 */
#ifndef H750_MPU_CONFIG_H
#define H750_MPU_CONFIG_H

#include <stdint.h>
#include "h750_config.h"

/* 一个 MPU 区域的完整描述（属性语义 + 编码字段） */
typedef struct {
    uint8_t     number;     /* 区域号 0..15                        */
    uint32_t    base;       /* 起始地址，必须按 size 对齐            */
    uint32_t    size;       /* 区域大小（字节，2 的幂，>=32）        */
    uint8_t     tex;        /* TEX[2:0]                            */
    uint8_t     c;          /* C 位（可缓存）                       */
    uint8_t     b;          /* B 位（可缓冲）                       */
    uint8_t     s;          /* S 位（可共享）                       */
    uint8_t     ap;         /* AP[2:0] 访问权限                    */
    uint8_t     xn;         /* 1 = 禁止取指                         */
    uint8_t     srd;        /* 子区域禁用位（仅 size>=256B 有效）    */
    uint8_t     enable;     /* 1 = 使能该区域                       */
    const char *name;       /* 区域短名（日志/诊断界面使用）          */
    const char *usage;      /* 用途说明                             */
} mpu_region_t;

/* 汇总统计 */
typedef struct {
    uint32_t regions_total;
    uint32_t regions_enabled;
    uint32_t regions_cacheable;
    uint32_t regions_non_cacheable;
    uint32_t bytes_cacheable;
    uint32_t bytes_non_cacheable;
    uint32_t hw_regions;        /* MPU_TYPE.DREGION 读回值 */
    uint8_t  table_overlaps;    /* 是否存在地址重叠（0 为正常） */
} mpu_stats_t;

/* 板级 MPU 区域表（唯一数据源） */
extern const mpu_region_t h750_mpu_table[];
extern const uint32_t     h750_mpu_table_size;

/* ---- 编码 ------------------------------------------------------------- */
/* 把区域大小编码为 MPU_RASR.SIZE 字段：SIZE = log2(size) - 1 */
int      mpu_encode_size(uint32_t size_bytes, uint32_t *size_field);
/* 直接给出 RASR 中的 SIZE 位（已左移到 bit[5:1]） */
uint32_t mpu_rasr_size_bits(uint32_t size_bytes);
/* MPU_RBAR：base(对齐) | VALID | REGION */
uint32_t mpu_rbar_value(const mpu_region_t *r);
/* MPU_RASR：XN|AP|TEX|S|C|B|SRD|SIZE|ENABLE */
uint32_t mpu_rasr_value(const mpu_region_t *r);
/* 校验区域描述是否合法，返回 0 表示合法，负值表示错误码 */
int      mpu_region_validate(const mpu_region_t *r);

/* ---- 硬件操作 --------------------------------------------------------- */
void     mpu_load_region(const mpu_region_t *r);
void     mpu_configure_all(void);
void     mpu_enable(uint8_t privdefena);
void     mpu_disable(void);
uint32_t mpu_hw_region_count(void);
uint32_t mpu_ctrl_read(void);

/* ---- 查询 ------------------------------------------------------------- */
/* 返回覆盖 addr 的区域中区域号最大者（MPU 重叠时高区域号优先）；无则 NULL */
const mpu_region_t *mpu_region_lookup(uint32_t addr);
/* 1 = 可缓存（走 D-Cache），0 = 非缓存，-1 = 未被任何区域覆盖 */
int      mpu_region_is_cacheable(uint32_t addr);
/* 检查属性组合是否自洽（如 Device 类型不允许 C=1） */
int      mpu_region_attrs_valid(const mpu_region_t *r);
const char *mpu_memory_type_str(const mpu_region_t *r);
const char *mpu_ap_str(uint8_t ap);
int      mpu_table_find_overlaps(void);
void     mpu_get_stats(mpu_stats_t *st);

#endif /* H750_MPU_CONFIG_H */
