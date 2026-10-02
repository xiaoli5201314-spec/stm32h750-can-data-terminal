/*
 * mpu_config.c
 * ---------------------------------------------------------------------------
 * Cortex-M7 MPU 区域规划与 RBAR/RASR 编码实现。
 *
 * 编码规则（ARMv7-M 架构参考手册 B3.5）：
 *   MPU_RBAR = ADDR[31:5] | VALID(bit4) | REGION[3:0]
 *   MPU_RASR = XN(bit28) | AP[2:0](bit26:24) | TEX[2:0](bit21:19) |
 *              S(bit18) | C(bit17) | B(bit16) | SRD[7:0](bit15:8) |
 *              SIZE[4:0](bit5:1) | ENABLE(bit0)
 *   SIZE 字段 = log2(region_size) - 1，区域最小 32 字节。
 *
 * 内存类型由 TEX/C/B 组合决定（ARMv7-M 表 B3-13）：
 *   TEX=000 C=0 B=0 -> Strongly-ordered
 *   TEX=000 C=0 B=1 -> Device
 *   TEX=000 C=1 B=0 -> Normal, Write-Through, no Write-Allocate
 *   TEX=000 C=1 B=1 -> Normal, Write-Back,  Write-Allocate
 *   TEX=001 C=0 B=0 -> Normal, Non-cacheable
 *   TEX=001 C=0 B=1 -> Normal, Write-Back, Write-Allocate, no Read-Allocate
 *   TEX=001 C=1 B=0 -> Normal, Write-Through
 *   TEX=001 C=1 B=1 -> Normal, Write-Back, no Write-Allocate
 */
#include "mpu_config.h"

/* DSB/ISB：保证 MPU 配置在后续取指/取数前生效 */
#if defined(__GNUC__) && !defined(H750_PC_SIM)
#define H750_DSB()   __asm volatile ("dsb 0xF" ::: "memory")
#define H750_ISB()   __asm volatile ("isb 0xF" ::: "memory")
#else
#define H750_DSB()   do { } while (0)
#define H750_ISB()   do { } while (0)
#endif

/* ---------------------------------------------------------------------------
 * 板级 MPU 区域表
 *
 * 设计要点：
 *   1) 所有 DMA 直接访问的缓冲区都落在"Normal Non-cacheable"区域（区域 4/5/6/10），
 *      由 MPU 属性强制旁路 D-Cache，不需要任何 cache 维护；
 *   2) 大容量数据（分层报文缓存、LCD 帧缓冲、离线数据）落在 SDRAM 可缓存区
 *      （区域 9，Write-Back, no Write-Allocate），提高 CPU 吞吐；
 *   3) 外设寄存器区（区域 11）为 Device 类型，禁止取指，避免对 FIFO 的投机访问；
 *   4) SDRAM 非缓存窗口（区域 10）与可缓存区（区域 9）首尾相接，不重叠。
 * ------------------------------------------------------------------------- */
const mpu_region_t h750_mpu_table[] = {
    /* 0: ITCM —— 中断向量与实时 ISR */
    { 0, H750_ITCM_BASE, H750_ITCM_SIZE, 1u, 0u, 0u, 1u,
      MPU_AP_FULL_ACCESS, 0u, 0u, 1u, "ITCM",
      "中断向量表、FDCAN ISR、时间戳临界代码（TCM 不经 Cache）" },

    /* 1: 内部 Flash —— 上电启动与本工程精简固件 */
    { 1, H750_FLASH_BASE, H750_FLASH_SIZE, 0u, 1u, 0u, 0u,
      MPU_AP_PRIV_RO_URO, 0u, 0u, 1u, "FLASH",
      "128KB 内部 Flash，只读可缓存（Write-Through）" },

    /* 2: DTCM —— 栈与实时变量 */
    { 2, H750_DTCM_BASE, H750_DTCM_SIZE, 1u, 0u, 0u, 1u,
      MPU_AP_FULL_ACCESS, 0u, 0u, 1u, "DTCM",
      "线程栈、协议解析临时变量（TCM 不经 Cache，天然一致）" },

    /* 3: AXI SRAM —— 主堆与帧缓冲 */
    { 3, H750_AXI_SRAM_BASE, H750_AXI_SRAM_SIZE, 0u, 1u, 1u, 1u,
      MPU_AP_FULL_ACCESS, 0u, 0u, 1u, "AXI_SRAM",
      "主堆、协议解析缓冲、LCD 帧缓冲 A/B（Write-Back + Write-Allocate）" },

    /* 4: SRAM1+SRAM2 —— 非缓存 DMA 池 */
    { 4, H750_SRAM1_BASE, H750_SRAM1_SIZE + H750_SRAM2_SIZE, 1u, 0u, 0u, 1u,
      MPU_AP_FULL_ACCESS, 1u, 0u, 1u, "SRAM12_NC",
      "以太网收发缓冲、批量上报缓冲（非缓存，DMA 直访）" },

    /* 5: SRAM3 —— FDCAN 元素层与事件池 */
    { 5, H750_SRAM3_BASE, H750_SRAM3_SIZE, 1u, 0u, 0u, 1u,
      MPU_AP_FULL_ACCESS, 1u, 0u, 1u, "SRAM3_NC",
      "CAN 帧内存池、高优先级事件池（非缓存，DMA 直访）" },

    /* 6: SRAM4 —— ETH 描述符环 */
    { 6, H750_SRAM4_BASE, H750_SRAM4_SIZE, 1u, 0u, 0u, 1u,
      MPU_AP_FULL_ACCESS, 1u, 0u, 1u, "SRAM4_NC",
      "以太网 DMA 描述符环、低功耗域日志（非缓存）" },

    /* 7: Backup SRAM —— 掉电保持 */
    { 7, H750_BKPSRAM_BASE, H750_BKPSRAM_SIZE, 1u, 0u, 0u, 1u,
      MPU_AP_FULL_ACCESS, 1u, 0u, 1u, "BKPSRAM",
      "掉电保持：错误计数、日志序号、运行小时（非缓存）" },

    /* 8: QSPI XIP —— 字库/图标/配置 */
    { 8, H750_QSPI_XIP_BASE, 64u * 1024u * 1024u, 0u, 1u, 0u, 0u,
      MPU_AP_PRIV_RO_URO, 0u, 0u, 1u, "QSPI_XIP",
      "QSPI Flash 内存映射读（Write-Through，只读，可执行）" },

    /* 9: SDRAM 可缓存区 0xC0000000 + 16MB
     *    TEX=001,C=1,B=1 = Normal Write-Back，no Write-Allocate：
     *    大块流式数据（报文缓存/日志）不做写分配，避免污染 D-Cache。 */
    { 9, H750_SDRAM_CACHE_BASE, 16u * 1024u * 1024u, 1u, 1u, 1u, 0u,
      MPU_AP_FULL_ACCESS, 0u, 0u, 1u, "SDRAM_WB",
      "分层报文缓存、离线数据环形区、诊断日志（Write-Back, no Write-Allocate）" },

    /* 10: SDRAM 非缓存窗口 0xC1000000 + 1MB */
    { 10, H750_SDRAM_NC_BASE, H750_SDRAM_NC_SIZE, 1u, 0u, 0u, 1u,
      MPU_AP_FULL_ACCESS, 1u, 0u, 1u, "SDRAM_NC",
      "SDMMC/以太网大块 DMA 直写缓冲（非缓存，1MB 窗口）" },

    /* 11: 外设寄存器区 */
    { 11, H750_PERIPH_BASE, 512u * 1024u * 1024u, 0u, 0u, 1u, 1u,
      MPU_AP_FULL_ACCESS, 1u, 0u, 1u, "PERIPH",
      "全部外设寄存器（Device，禁止取指，避免 FIFO 被投机读）" },
};

const uint32_t h750_mpu_table_size =
    (uint32_t)(sizeof(h750_mpu_table) / sizeof(h750_mpu_table[0]));

/* ---------------------------------------------------------------------------
 * 编码
 * ------------------------------------------------------------------------- */
int mpu_encode_size(uint32_t size_bytes, uint32_t *size_field)
{
    uint32_t n = 0u;
    uint32_t v = size_bytes;

    if ((size_field == 0) || (size_bytes < 32u)) {
        return -1;
    }
    /* 必须是 2 的幂 */
    if ((size_bytes & (size_bytes - 1u)) != 0u) {
        return -2;
    }
    while (v > 1u) {
        v >>= 1;
        n++;
    }
    /* n = log2(size)，SIZE 字段 = n - 1 */
    *size_field = n - 1u;
    return 0;
}

uint32_t mpu_rasr_size_bits(uint32_t size_bytes)
{
    uint32_t field = 0u;
    if (mpu_encode_size(size_bytes, &field) != 0) {
        return 0u;
    }
    return (field << MPU_RASR_SIZE_Pos) & MPU_RASR_SIZE_Msk;
}

uint32_t mpu_rbar_value(const mpu_region_t *r)
{
    uint32_t v;
    if (r == 0) {
        return 0u;
    }
    v = r->base & MPU_RBAR_ADDR_Msk;
    v |= MPU_RBAR_VALID;
    v |= ((uint32_t)r->number) & MPU_RBAR_REGION_Msk;
    return v;
}

uint32_t mpu_rasr_value(const mpu_region_t *r)
{
    uint32_t v = 0u;
    if (r == 0) {
        return 0u;
    }
    if (r->xn != 0u) {
        v |= MPU_RASR_XN;
    }
    v |= (((uint32_t)r->ap) << MPU_RASR_AP_Pos) & MPU_RASR_AP_Msk;
    v |= (((uint32_t)r->tex) << MPU_RASR_TEX_Pos) & MPU_RASR_TEX_Msk;
    if (r->s != 0u) { v |= MPU_RASR_S; }
    if (r->c != 0u) { v |= MPU_RASR_C; }
    if (r->b != 0u) { v |= MPU_RASR_B; }
    v |= (((uint32_t)r->srd) << MPU_RASR_SRD_Pos) & MPU_RASR_SRD_Msk;
    v |= mpu_rasr_size_bits(r->size);
    if (r->enable != 0u) { v |= MPU_RASR_ENABLE; }
    return v;
}

int mpu_region_attrs_valid(const mpu_region_t *r)
{
    if (r == 0) {
        return -1;
    }
    if ((r->tex > 7u) || (r->c > 1u) || (r->b > 1u) || (r->s > 1u) || (r->xn > 1u)) {
        return -2;
    }
    /* TEX=000 且 C=0 且 B=0 是 Strongly-ordered，仅允许共享 */
    if ((r->tex == 0u) && (r->c == 0u) && (r->b == 0u) && (r->s == 0u)) {
        return -3;   /* 该组合保留，不可用于 Normal 内存 */
    }
    /* Device 类型必须共享（ARMv7-M 要求 Device 为 Shareable） */
    if ((r->tex == 0u) && (r->c == 0u) && (r->b == 1u) && (r->s == 0u)) {
        return -4;
    }
    /* TEX=010 的 Device 类型必须 S=0；TEX=001/1xx 的 Normal 类型允许 S=0/1 */
    if ((r->tex >= 0x4u) && (r->s != 0u)) {
        return -5;   /* TEX>=100 为保留 */
    }
    return 0;
}

int mpu_region_validate(const mpu_region_t *r)
{
    uint32_t field = 0u;
    int rc;

    if (r == 0) {
        return -1;
    }
    if (r->number > 15u) {
        return -2;
    }
    if (mpu_encode_size(r->size, &field) != 0) {
        return -3;
    }
    if ((r->base & (r->size - 1u)) != 0u) {
        return -4;   /* 基地址未按区域大小对齐 */
    }
    if ((r->size < 256u) && (r->srd != 0u)) {
        return -5;   /* 小于 256 字节的区域不支持子区域 */
    }
    rc = mpu_region_attrs_valid(r);
    if (rc != 0) {
        return rc;
    }
    return 0;
}

const char *mpu_memory_type_str(const mpu_region_t *r)
{
    static const char *const nc = "Normal Non-cacheable";
    if (r == 0) {
        return "?";
    }
    if (r->tex == 0u) {
        if ((r->c == 0u) && (r->b == 0u)) { return "Strongly-ordered"; }
        if ((r->c == 0u) && (r->b == 1u)) { return "Device"; }
        if ((r->c == 1u) && (r->b == 0u)) { return "Normal WT no WA"; }
        return "Normal WB-WA";
    }
    if (r->tex == 1u) {
        if ((r->c == 0u) && (r->b == 0u)) { return nc; }
        if ((r->c == 0u) && (r->b == 1u)) { return "Normal WB-WA no RA"; }
        if ((r->c == 1u) && (r->b == 0u)) { return "Normal WT"; }
        return "Normal WB no WA";
    }
    return "Device/Reserved";
}

const char *mpu_ap_str(uint8_t ap)
{
    switch (ap) {
    case MPU_AP_NO_ACCESS:   return "---";
    case MPU_AP_PRIV_RW:     return "PRIV-RW";
    case MPU_AP_PRIV_RW_URO: return "PRIV-RW/URO";
    case MPU_AP_FULL_ACCESS: return "RW/RW";
    case MPU_AP_PRIV_RO:     return "PRIV-RO";
    case MPU_AP_PRIV_RO_URO: return "RO/RO";
    default:                 return "RSVD";
    }
}

/* ---------------------------------------------------------------------------
 * 硬件操作
 * ------------------------------------------------------------------------- */
void mpu_load_region(const mpu_region_t *r)
{
    if (r == 0) {
        return;
    }
    H750_REG32(H750_MPU_RNR)  = (uint32_t)r->number;
    H750_REG32(H750_MPU_RBAR) = mpu_rbar_value(r);
    H750_REG32(H750_MPU_RASR) = mpu_rasr_value(r);
    H750_DSB();
    H750_ISB();
}

void mpu_configure_all(void)
{
    uint32_t i;
    for (i = 0u; i < h750_mpu_table_size; i++) {
        mpu_load_region(&h750_mpu_table[i]);
    }
    H750_DSB();
    H750_ISB();
}

void mpu_enable(uint8_t privdefena)
{
    uint32_t ctrl = MPU_CTRL_ENABLE;
    if (privdefena != 0u) {
        ctrl |= MPU_CTRL_PRIVDEFENA;
    }
    H750_DSB();
    H750_REG32(H750_MPU_CTRL) = ctrl;
    H750_DSB();
    H750_ISB();
}

void mpu_disable(void)
{
    H750_DSB();
    H750_REG32(H750_MPU_CTRL) = 0u;
    H750_DSB();
    H750_ISB();
}

uint32_t mpu_ctrl_read(void)
{
    return H750_REG32(H750_MPU_CTRL);
}

uint32_t mpu_hw_region_count(void)
{
    uint32_t type = H750_REG32(H750_MPU_TYPE);
    return (type & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;
}

/* ---------------------------------------------------------------------------
 * 查询
 * ------------------------------------------------------------------------- */
const mpu_region_t *mpu_region_lookup(uint32_t addr)
{
    uint32_t i = h750_mpu_table_size;
    /* MPU 重叠时区域号大的优先，因此从高位向低位扫描 */
    while (i > 0u) {
        const mpu_region_t *r = &h750_mpu_table[i - 1u];
        i--;
        if ((r->enable != 0u) && (addr >= r->base) && (addr < (r->base + r->size))) {
            /* 子区域禁用检查（仅 >=256 字节区域有效） */
            if ((r->size >= 256u) && (r->srd != 0u)) {
                uint32_t sub = r->size / 8u;
                uint32_t idx = (addr - r->base) / sub;
                if (((r->srd >> idx) & 0x1u) != 0u) {
                    continue;
                }
            }
            return r;
        }
    }
    return 0;
}

int mpu_region_is_cacheable(uint32_t addr)
{
    const mpu_region_t *r = mpu_region_lookup(addr);
    if (r == 0) {
        return -1;
    }
    /* C 位是 Normal 内存的 Cache 使能位：Device / Strongly-ordered /
     * Normal Non-cacheable（TEX=001,C=0,B=0）/ WB-WA-no-RA（TEX=001,C=0,B=1）
     * 都不经过 D-Cache，因此以 C 位作为唯一判据。 */
    return (r->c != 0u) ? 1 : 0;
}

int mpu_table_find_overlaps(void)
{
    uint32_t i;
    uint32_t j;
    for (i = 0u; i < h750_mpu_table_size; i++) {
        const mpu_region_t *a = &h750_mpu_table[i];
        if (a->enable == 0u) {
            continue;
        }
        for (j = i + 1u; j < h750_mpu_table_size; j++) {
            const mpu_region_t *b = &h750_mpu_table[j];
            uint32_t a_end;
            uint32_t b_end;
            if (b->enable == 0u) {
                continue;
            }
            a_end = a->base + a->size;
            b_end = b->base + b->size;
            if ((a->base < b_end) && (b->base < a_end)) {
                return (int)(i * 100u + j);
            }
        }
    }
    return 0;
}

void mpu_get_stats(mpu_stats_t *st)
{
    uint32_t i;
    if (st == 0) {
        return;
    }
    st->regions_total = h750_mpu_table_size;
    st->regions_enabled = 0u;
    st->regions_cacheable = 0u;
    st->regions_non_cacheable = 0u;
    st->bytes_cacheable = 0u;
    st->bytes_non_cacheable = 0u;
    st->hw_regions = mpu_hw_region_count();
    st->table_overlaps = (uint8_t)(mpu_table_find_overlaps() != 0);

    for (i = 0u; i < h750_mpu_table_size; i++) {
        const mpu_region_t *r = &h750_mpu_table[i];
        if (r->enable == 0u) {
            continue;
        }
        st->regions_enabled++;
        if (r->c != 0u) {
            st->regions_cacheable++;
            st->bytes_cacheable += r->size;
        } else {
            st->regions_non_cacheable++;
            st->bytes_non_cacheable += r->size;
        }
    }
}
