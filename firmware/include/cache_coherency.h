/*
 * cache_coherency.h
 * ---------------------------------------------------------------------------
 * Cortex-M7 Cache 与 DMA 一致性管理。
 *
 * 三层手段（本文件提供第 2、3 层的原语；第 1 层见 mpu_config.c）：
 *   第 1 层 —— MPU 内存属性划分：DMA 缓冲区放进 Normal Non-cacheable 区域；
 *   第 2 层 —— 方向性 Cache 维护：DMA 读内存前 Clean，DMA 写内存后 Invalidate；
 *   第 3 层 —— 缓冲描述与对齐约束：32 字节对齐，禁止跨行共享，杜绝误伤。
 *
 * PC 仿真构建（H750_PC_SIM）下，本文件内含一个"影子行"式 D-Cache 模型，
 * 可以真实复现 Cache/DMA 不一致现象，也能验证三种修复策略。
 */
#ifndef H750_CACHE_COHERENCY_H
#define H750_CACHE_COHERENCY_H

#include <stdint.h>

/* Cortex-M7 D-Cache 行大小固定 32 字节 */
#define CC_LINE_BYTES       32u
#define CC_LINE_SHIFT       5u

/* 32 字节对齐属性（仅主机编译时生效，用于仿真测试中的对齐缓冲） */
#if defined(__GNUC__)
#define CC_ALIGN32          __attribute__((aligned(CC_LINE_BYTES)))
#else
#define CC_ALIGN32
#endif

/* 内存属性（与 MPU 区域一一对应） */
typedef enum {
    CC_ATTR_NON_CACHEABLE = 0,   /* 非缓存区：DMA 与 CPU 看到同一份内存 */
    CC_ATTR_WRITE_THROUGH = 1,   /* 写通：CPU 写直达内存，无脏行          */
    CC_ATTR_WRITE_BACK    = 2    /* 写回：CPU 写只更新 Cache，需 Clean    */
} cc_attr_t;

/* DMA 缓冲描述 */
typedef struct {
    void       *addr;        /* 缓冲区起始地址（要求 32 字节对齐） */
    uint32_t    size;        /* 缓冲区大小（要求 32 字节整数倍）   */
    cc_attr_t   attr;        /* 内存属性                           */
    const char *name;        /* 用途名（日志）                     */
    uint32_t    tx_syncs;    /* TX 方向维护次数                    */
    uint32_t    rx_syncs;    /* RX 方向维护次数                    */
} cc_dma_buffer_t;

/* ---- 初始化与开关 ----------------------------------------------------- */
void     cc_init(void);
void     cc_dcache_enable(void);
void     cc_dcache_disable(void);
void     cc_dcache_invalidate_all(void);
uint32_t cc_dcache_enabled(void);

/* ---- Cache 维护原语（真实硬件写 SCB 寄存器；仿真下操作模型） ---------- */
void     cc_dcache_clean(void *addr, uint32_t len);
void     cc_dcache_invalidate(void *addr, uint32_t len);
void     cc_dcache_clean_invalidate(void *addr, uint32_t len);

/* ---- 对齐工具（地址用 uintptr_t，保证在 64 位主机上做仿真时不被截断） ---- */
uintptr_t cc_align_up32(uintptr_t addr);
uintptr_t cc_align_down32(uintptr_t addr);
int      cc_is_aligned32(uintptr_t addr);
/* 覆盖 [addr, addr+len) 所需的 cache 行数 */
uint32_t cc_line_count(uintptr_t addr, uint32_t len);
/* 该地址区间是否跨越完整的 cache 行边界（对齐检查） */
int      cc_range_is_line_aligned(uintptr_t addr, uint32_t len);

/* ---- 缓冲描述与方向性同步 --------------------------------------------- */
/* 返回 0 成功；-1 参数空；-2 未对齐；-3 大小为 0 或非行整数倍 */
int  cc_dma_buffer_setup(cc_dma_buffer_t *b, void *addr, uint32_t size,
                         cc_attr_t attr, const char *name);
/* CPU 写完、DMA 读之前调用：非缓存区免维护，写回区执行 Clean */
int  cc_dma_sync_tx(cc_dma_buffer_t *b);
/* DMA 写完、CPU 读之前调用：非缓存区免维护，写回/写通区执行 Invalidate */
int  cc_dma_sync_rx(cc_dma_buffer_t *b);
/* 双向：先 Clean 再 Invalidate */
int  cc_dma_sync_bidir(cc_dma_buffer_t *b);

/* ---- 一致性统计 ------------------------------------------------------- */
typedef struct {
    uint32_t clean_ops;
    uint32_t invalidate_ops;
    uint32_t clean_invalidate_ops;
    uint32_t lines_cleaned;
    uint32_t lines_invalidated;
    uint32_t lines_writeback;
    uint32_t dirty_discarded;     /* 被 Invalidate 直接丢弃的脏行数（数据丢失） */
    uint32_t cpu_loads;
    uint32_t cpu_stores;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t dirty_lines;
    uint32_t valid_lines;
} cc_stats_t;

void cc_get_stats(cc_stats_t *st);
void cc_reset_stats(void);

/* ==========================================================================
 * PC 仿真接口（仅在 H750_PC_SIM 下可用）
 * 说明：影子行模型把主机内存当作"物理内存"，Cache 保存行副本。
 *       CPU 访问走模型，DMA 访问直接操作主机内存（memcpy），
 *       从而可以精确复现"CPU 看到旧值 / DMA 读到旧值"的不一致现象。
 * ========================================================================== */
#ifdef H750_PC_SIM

typedef enum {
    CC_SIM_NC = 0,   /* 非缓存 */
    CC_SIM_WT = 1,   /* 写通 */
    CC_SIM_WB = 2    /* 写回 */
} cc_sim_attr_t;

void     cc_sim_reset(void);
/* 为 [addr, addr+size) 指定仿真内存属性；未指定的地址默认按写回处理 */
int      cc_sim_set_region(void *addr, uint32_t size, cc_sim_attr_t attr);
/* 依据 MPU 区域表给主机缓冲打上属性（验证"MPU 属性 -> Cache 行为"链路） */
int      cc_sim_set_region_from_mpu(void *host_addr, uint32_t h7_addr, uint32_t size);
cc_sim_attr_t cc_sim_get_region_attr(uintptr_t addr);

/* 模拟 CPU 访问（走 Cache 模型） */
int      cc_sim_cpu_write8(void *addr, uint8_t v);
int      cc_sim_cpu_write16(void *addr, uint16_t v);
int      cc_sim_cpu_write32(void *addr, uint32_t v);
uint8_t  cc_sim_cpu_read8(const void *addr);
uint16_t cc_sim_cpu_read16(const void *addr);
uint32_t cc_sim_cpu_read32(const void *addr);

/* 模拟 DMA 访问（绕过 Cache，直接命中主机内存） */
void     cc_sim_dma_write(void *dst_mem, const void *src, uint32_t len);
void     cc_sim_dma_read(const void *src_mem, void *dst, uint32_t len);

/* 直接观察/修改"物理内存"（等价于用调试器看 DMA 视角） */
uint32_t cc_sim_phys_read32(const void *addr);
void     cc_sim_phys_write32(void *addr, uint32_t v);
void     cc_sim_phys_read(const void *addr, void *dst, uint32_t len);

uint32_t cc_sim_total_lines(void);
uint32_t cc_sim_valid_lines(void);
uint32_t cc_sim_dirty_lines(void);
uint32_t cc_sim_dirty_discarded(void);

#endif /* H750_PC_SIM */

#endif /* H750_CACHE_COHERENCY_H */
