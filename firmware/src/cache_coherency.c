/*
 * cache_coherency.c
 * ---------------------------------------------------------------------------
 * Cortex-M7 Cache 与 DMA 一致性：维护原语 + 缓冲描述 + 一致性统计，
 * 以及在 PC 上可复现不一致现象的 D-Cache 影子行模型。
 *
 * 目标构建：直接操作 SCB 的 Cache 维护寄存器（ARMv7-M 架构定义）
 *   DCCMVAC  0xE000EF68  Clean D-Cache by MVA（写回并保留）
 *   DCIMVAC  0xE000EF5C  Invalidate D-Cache by MVA
 *   DCCIMVAC 0xE000EF70  Clean & Invalidate D-Cache by MVA
 *   CCR      0xE000ED14   bit16 D-Cache 使能，bit17 I-Cache 使能
 */
#include <string.h>
#include "cache_coherency.h"
#include "mpu_config.h"

#if defined(__GNUC__) && !defined(H750_PC_SIM)
#define CC_DSB()   __asm volatile ("dsb 0xF" ::: "memory")
#define CC_ISB()   __asm volatile ("isb 0xF" ::: "memory")
#else
#define CC_DSB()   do { } while (0)
#define CC_ISB()   do { } while (0)
#endif

/* ==========================================================================
 * 公共：对齐工具与缓冲管理
 * ========================================================================== */
uintptr_t cc_align_down32(uintptr_t addr)
{
    return addr & ~(uintptr_t)(CC_LINE_BYTES - 1u);
}

uintptr_t cc_align_up32(uintptr_t addr)
{
    return (addr + (uintptr_t)(CC_LINE_BYTES - 1u)) & ~(uintptr_t)(CC_LINE_BYTES - 1u);
}

int cc_is_aligned32(uintptr_t addr)
{
    return ((addr & (uintptr_t)(CC_LINE_BYTES - 1u)) == 0u) ? 1 : 0;
}

uint32_t cc_line_count(uintptr_t addr, uint32_t len)
{
    uintptr_t first;
    uintptr_t last;
    if (len == 0u) {
        return 0u;
    }
    first = cc_align_down32(addr);
    last = cc_align_down32(addr + (uintptr_t)len - 1u);
    return (uint32_t)(((last - first) >> CC_LINE_SHIFT) + 1u);
}

int cc_range_is_line_aligned(uintptr_t addr, uint32_t len)
{
    if ((cc_is_aligned32(addr) != 0) && ((len & (uint32_t)(CC_LINE_BYTES - 1u)) == 0u)) {
        return 1;
    }
    return 0;
}

int cc_dma_buffer_setup(cc_dma_buffer_t *b, void *addr, uint32_t size,
                        cc_attr_t attr, const char *name)
{
    if ((b == 0) || (addr == 0)) {
        return -1;
    }
    if (cc_is_aligned32((uintptr_t)addr) == 0) {
        return -2;
    }
    if ((size == 0u) || ((size & (uint32_t)(CC_LINE_BYTES - 1u)) != 0u)) {
        return -3;
    }
    b->addr = addr;
    b->size = size;
    b->attr = attr;
    b->name = name;
    b->tx_syncs = 0u;
    b->rx_syncs = 0u;
    return 0;
}

int cc_dma_sync_tx(cc_dma_buffer_t *b)
{
    if (b == 0) {
        return -1;
    }
    if (b->attr == CC_ATTR_NON_CACHEABLE) {
        return 0;   /* 非缓存区：CPU 写直达内存，无需维护 */
    }
    cc_dcache_clean(b->addr, b->size);
    b->tx_syncs++;
    return 1;
}

int cc_dma_sync_rx(cc_dma_buffer_t *b)
{
    if (b == 0) {
        return -1;
    }
    if (b->attr == CC_ATTR_NON_CACHEABLE) {
        return 0;   /* 非缓存区：CPU 读直达内存，无需维护 */
    }
    cc_dcache_invalidate(b->addr, b->size);
    b->rx_syncs++;
    return 1;
}

int cc_dma_sync_bidir(cc_dma_buffer_t *b)
{
    if (b == 0) {
        return -1;
    }
    if (b->attr == CC_ATTR_NON_CACHEABLE) {
        return 0;
    }
    cc_dcache_clean_invalidate(b->addr, b->size);
    b->tx_syncs++;
    b->rx_syncs++;
    return 1;
}

/* ==========================================================================
 * PC 仿真：影子行 D-Cache 模型
 * ========================================================================== */
#ifdef H750_PC_SIM

#define CC_SIM_LINE_MAX     128u
#define CC_SIM_REGION_MAX   16u

typedef struct {
    uintptr_t tag;              /* 行起始物理地址（主机仿真下为 64 位指针值） */
    uint8_t  valid;
    uint8_t  dirty;
    uint8_t  attr;              /* cc_sim_attr_t */
    uint8_t  used;
    uint32_t lru;
    uint8_t  data[CC_LINE_BYTES];
} cc_sim_line_t;

typedef struct {
    uintptr_t     base;
    uintptr_t     end;
    cc_sim_attr_t attr;
} cc_sim_region_t;

static cc_sim_line_t   s_lines[CC_SIM_LINE_MAX];
static cc_sim_region_t s_regions[CC_SIM_REGION_MAX];
static uint32_t        s_region_count;
static uint32_t        s_lru_clock;
static cc_stats_t      s_stats;

/* 主机地址直接读写（等价于“物理内存”访问） */
static void host_read(const void *addr, void *dst, uint32_t len)
{
    (void)memcpy(dst, addr, (size_t)len);
}

static void host_write(void *addr, const void *src, uint32_t len)
{
    (void)memcpy(addr, src, (size_t)len);
}

static uint32_t host_read32(const void *addr)
{
    uint32_t v;
    (void)memcpy(&v, addr, sizeof(v));
    return v;
}

void cc_sim_reset(void)
{
    uint32_t i;
    for (i = 0u; i < CC_SIM_LINE_MAX; i++) {
        s_lines[i].valid = 0u;
        s_lines[i].dirty = 0u;
        s_lines[i].used = 0u;
        s_lines[i].lru = 0u;
        s_lines[i].tag = 0u;
        s_lines[i].attr = (uint8_t)CC_SIM_WB;
        (void)memset(s_lines[i].data, 0, sizeof(s_lines[i].data));
    }
    s_region_count = 0u;
    s_lru_clock = 0u;
    (void)memset(&s_stats, 0, sizeof(s_stats));
}

int cc_sim_set_region(void *addr, uint32_t size, cc_sim_attr_t attr)
{
    uintptr_t base;
    if ((addr == 0) || (size == 0u)) {
        return -1;
    }
    base = cc_align_down32((uintptr_t)addr);
    if (s_region_count >= CC_SIM_REGION_MAX) {
        /* 复用最早的一条，保持模型有限状态 */
        uint32_t i;
        for (i = 1u; i < CC_SIM_REGION_MAX; i++) {
            s_regions[i - 1u] = s_regions[i];
        }
        s_region_count = CC_SIM_REGION_MAX - 1u;
    }
    s_regions[s_region_count].base = base;
    s_regions[s_region_count].end  = base + size;
    s_regions[s_region_count].attr = attr;
    s_region_count++;
    return 0;
}

int cc_sim_set_region_from_mpu(void *host_addr, uint32_t h7_addr, uint32_t size)
{
    const mpu_region_t *r = mpu_region_lookup(h7_addr);
    cc_sim_attr_t attr;

    if (r == 0) {
        return -1;
    }
    if ((r->c == 0u) && (r->b == 0u)) {
        attr = CC_SIM_NC;                 /* Normal Non-cacheable */
    } else if (r->tex == 1u) {
        attr = CC_SIM_WB;                 /* WB no WA */
    } else if ((r->c == 1u) && (r->b == 0u)) {
        attr = CC_SIM_WT;                 /* Write-Through */
    } else {
        attr = CC_SIM_WB;
    }
    return cc_sim_set_region(host_addr, size, attr);
}

cc_sim_attr_t cc_sim_get_region_attr(uintptr_t addr)
{
    uint32_t i = s_region_count;
    while (i > 0u) {
        const cc_sim_region_t *rg = &s_regions[i - 1u];
        i--;
        if ((addr >= rg->base) && (addr < rg->end)) {
            return rg->attr;
        }
    }
    return CC_SIM_WB;   /* 未配置 MPU 时的默认行为：可缓存写回 */
}

static int line_find(uintptr_t tag)
{
    uint32_t i;
    for (i = 0u; i < CC_SIM_LINE_MAX; i++) {
        if ((s_lines[i].valid != 0u) && (s_lines[i].tag == tag)) {
            s_lines[i].lru = ++s_lru_clock;
            return (int)i;
        }
    }
    return -1;
}

static int line_alloc(uintptr_t tag, cc_sim_attr_t attr)
{
    uint32_t i;
    int victim = -1;
    uint32_t oldest = 0xFFFFFFFFu;

    for (i = 0u; i < CC_SIM_LINE_MAX; i++) {
        if (s_lines[i].valid == 0u) {
            victim = (int)i;
            break;
        }
    }
    if (victim < 0) {
        for (i = 0u; i < CC_SIM_LINE_MAX; i++) {
            if (s_lines[i].lru < oldest) {
                oldest = s_lines[i].lru;
                victim = (int)i;
            }
        }
        /* 淘汰：写回脏行 */
        if ((victim >= 0) && (s_lines[victim].dirty != 0u)) {
            host_write((void *)s_lines[victim].tag, s_lines[victim].data, CC_LINE_BYTES);
            s_stats.lines_writeback++;
        }
    }
    if (victim < 0) {
        return -1;
    }
    s_lines[victim].tag = tag;
    s_lines[victim].valid = 1u;
    s_lines[victim].dirty = 0u;
    s_lines[victim].used = 1u;
    s_lines[victim].attr = (uint8_t)attr;
    s_lines[victim].lru = ++s_lru_clock;
    host_read((const void *)tag, s_lines[victim].data, CC_LINE_BYTES);
    s_stats.cache_misses++;
    return victim;
}

static int cpu_store(uintptr_t addr, const void *src, uint32_t width)
{
    cc_sim_attr_t attr = cc_sim_get_region_attr(addr);
    uintptr_t tag = cc_align_down32(addr);
    uint32_t off = (uint32_t)(addr & (uintptr_t)(CC_LINE_BYTES - 1u));
    int idx;

    s_stats.cpu_stores++;

    if (attr == CC_SIM_NC) {
        host_write((void *)addr, src, width);   /* 旁路 Cache */
        return 0;
    }

    idx = line_find(tag);
    if (idx < 0) {
        idx = line_alloc(tag, attr);
        if (idx < 0) {
            return -1;
        }
    } else {
        s_stats.cache_hits++;
    }
    host_write(&s_lines[idx].data[off], src, width);

    if (attr == CC_SIM_WT) {
        /* 写通：同时更新内存，行保持干净 */
        host_write((void *)addr, src, width);
        s_lines[idx].dirty = 0u;
    } else {
        s_lines[idx].dirty = 1u;
    }
    return 1;
}

static void cpu_load(uintptr_t addr, void *dst, uint32_t width)
{
    cc_sim_attr_t attr = cc_sim_get_region_attr(addr);
    uintptr_t tag = cc_align_down32(addr);
    uint32_t off = (uint32_t)(addr & (uintptr_t)(CC_LINE_BYTES - 1u));
    int idx;

    s_stats.cpu_loads++;

    if (attr == CC_SIM_NC) {
        host_read((const void *)addr, dst, width);   /* 旁路 Cache */
        return;
    }

    idx = line_find(tag);
    if (idx < 0) {
        idx = line_alloc(tag, attr);
        if (idx < 0) {
            host_read((const void *)addr, dst, width);
            return;
        }
    } else {
        s_stats.cache_hits++;
    }
    host_read(&s_lines[idx].data[off], dst, width);
}

int cc_sim_cpu_write8(void *addr, uint8_t v)
{
    return cpu_store((uintptr_t)addr, &v, 1u);
}

int cc_sim_cpu_write16(void *addr, uint16_t v)
{
    return cpu_store((uintptr_t)addr, &v, 2u);
}

int cc_sim_cpu_write32(void *addr, uint32_t v)
{
    return cpu_store((uintptr_t)addr, &v, 4u);
}

uint8_t cc_sim_cpu_read8(const void *addr)
{
    uint8_t v = 0u;
    cpu_load((uintptr_t)addr, &v, 1u);
    return v;
}

uint16_t cc_sim_cpu_read16(const void *addr)
{
    uint16_t v = 0u;
    cpu_load((uintptr_t)addr, &v, 2u);
    return v;
}

uint32_t cc_sim_cpu_read32(const void *addr)
{
    uint32_t v = 0u;
    cpu_load((uintptr_t)addr, &v, 4u);
    return v;
}

void cc_sim_dma_write(void *dst_mem, const void *src, uint32_t len)
{
    /* DMA 引擎不经过 Cache，直接写内存 */
    host_write(dst_mem, src, len);
}

void cc_sim_dma_read(const void *src_mem, void *dst, uint32_t len)
{
    host_read(src_mem, dst, len);
}

uint32_t cc_sim_phys_read32(const void *addr)
{
    return host_read32(addr);
}

void cc_sim_phys_write32(void *addr, uint32_t v)
{
    host_write(addr, &v, sizeof(v));
}

void cc_sim_phys_read(const void *addr, void *dst, uint32_t len)
{
    host_read(addr, dst, len);
}

static void sim_clean(uintptr_t addr, uint32_t len)
{
    uintptr_t first = cc_align_down32(addr);
    uintptr_t last  = cc_align_down32(addr + (uintptr_t)len - 1u);
    uintptr_t tag;
    uint32_t n = 0u;

    for (tag = first; tag <= last; tag += CC_LINE_BYTES) {
        int idx = line_find(tag);
        if (idx >= 0) {
            if (s_lines[idx].dirty != 0u) {
                host_write((void *)tag, s_lines[idx].data, CC_LINE_BYTES);
                s_lines[idx].dirty = 0u;
                s_stats.lines_writeback++;
            }
            n++;
        }
    }
    s_stats.clean_ops++;
    s_stats.lines_cleaned += n;
}

static void sim_invalidate(uintptr_t addr, uint32_t len)
{
    uintptr_t first = cc_align_down32(addr);
    uintptr_t last  = cc_align_down32(addr + (uintptr_t)len - 1u);
    uintptr_t tag;
    uint32_t n = 0u;

    for (tag = first; tag <= last; tag += CC_LINE_BYTES) {
        int idx = line_find(tag);
        if (idx >= 0) {
            if (s_lines[idx].dirty != 0u) {
                /* 与真实 Cortex-M7 行为一致：脏数据直接丢弃，不回写 */
                s_lines[idx].dirty = 0u;
                s_stats.dirty_discarded++;
            }
            s_lines[idx].valid = 0u;
            n++;
        }
    }
    s_stats.invalidate_ops++;
    s_stats.lines_invalidated += n;
}

static void sim_clean_invalidate(uintptr_t addr, uint32_t len)
{
    sim_clean(addr, len);
    sim_invalidate(addr, len);
    s_stats.clean_invalidate_ops++;
}

uint32_t cc_sim_total_lines(void)
{
    return CC_SIM_LINE_MAX;
}

uint32_t cc_sim_valid_lines(void)
{
    uint32_t i;
    uint32_t n = 0u;
    for (i = 0u; i < CC_SIM_LINE_MAX; i++) {
        if (s_lines[i].valid != 0u) {
            n++;
        }
    }
    return n;
}

uint32_t cc_sim_dirty_lines(void)
{
    uint32_t i;
    uint32_t n = 0u;
    for (i = 0u; i < CC_SIM_LINE_MAX; i++) {
        if ((s_lines[i].valid != 0u) && (s_lines[i].dirty != 0u)) {
            n++;
        }
    }
    return n;
}

uint32_t cc_sim_dirty_discarded(void)
{
    return s_stats.dirty_discarded;
}

void cc_get_stats(cc_stats_t *st)
{
    if (st == 0) {
        return;
    }
    *st = s_stats;
    st->dirty_lines = cc_sim_dirty_lines();
    st->valid_lines = cc_sim_valid_lines();
}

void cc_reset_stats(void)
{
    (void)memset(&s_stats, 0, sizeof(s_stats));
}

void cc_dcache_clean(void *addr, uint32_t len)
{
    if ((addr == 0) || (len == 0u)) {
        return;
    }
    CC_DSB();
    sim_clean((uintptr_t)addr, len);
    CC_DSB();
}

void cc_dcache_invalidate(void *addr, uint32_t len)
{
    if ((addr == 0) || (len == 0u)) {
        return;
    }
    CC_DSB();
    sim_invalidate((uintptr_t)addr, len);
    CC_DSB();
    CC_ISB();
}

void cc_dcache_clean_invalidate(void *addr, uint32_t len)
{
    if ((addr == 0) || (len == 0u)) {
        return;
    }
    CC_DSB();
    sim_clean_invalidate((uintptr_t)addr, len);
    CC_DSB();
    CC_ISB();
}

void cc_dcache_invalidate_all(void)
{
    cc_sim_reset();
}

void cc_dcache_enable(void)
{
    s_stats.clean_ops += 0u;
}

void cc_dcache_disable(void)
{
}

uint32_t cc_dcache_enabled(void)
{
    return 1u;
}

void cc_init(void)
{
    cc_sim_reset();
}

#else  /* ---------------- 目标构建：真实 SCB 寄存器操作 ---------------- */

static uint32_t s_enabled;

void cc_dcache_clean(void *addr, uint32_t len)
{
    uint32_t a;
    uint32_t end;
    if ((addr == 0) || (len == 0u)) {
        return;
    }
    a = cc_align_down32((uint32_t)(uintptr_t)addr);
    end = (uint32_t)(uintptr_t)addr + len;
    CC_DSB();
    while (a < end) {
        H750_REG32(H750_SCB_DCCMVAC) = a;
        a += CC_LINE_BYTES;
    }
    CC_DSB();
}

void cc_dcache_invalidate(void *addr, uint32_t len)
{
    uint32_t a;
    uint32_t end;
    if ((addr == 0) || (len == 0u)) {
        return;
    }
    a = cc_align_down32((uint32_t)(uintptr_t)addr);
    end = (uint32_t)(uintptr_t)addr + len;
    CC_DSB();
    while (a < end) {
        H750_REG32(H750_SCB_DCIMVAC) = a;
        a += CC_LINE_BYTES;
    }
    CC_DSB();
    CC_ISB();
}

void cc_dcache_clean_invalidate(void *addr, uint32_t len)
{
    uint32_t a;
    uint32_t end;
    if ((addr == 0) || (len == 0u)) {
        return;
    }
    a = cc_align_down32((uint32_t)(uintptr_t)addr);
    end = (uint32_t)(uintptr_t)addr + len;
    CC_DSB();
    while (a < end) {
        H750_REG32(H750_SCB_DCCIMVAC) = a;
        a += CC_LINE_BYTES;
    }
    CC_DSB();
    CC_ISB();
}

void cc_dcache_invalidate_all(void)
{
    uint32_t csselr = H750_REG32(H750_SCB_CCSIDR);
    uint32_t line_log = csselr & SCB_CCSIDR_LINESIZE_Msk;
    uint32_t ways = ((csselr >> 3) & 0x3FFu) + 1u;   /* CCSIDR.Associativity + 1 */
    uint32_t sets = ((csselr >> 13) & 0x7FFFu) + 1u; /* CCSIDR.NumSets + 1      */
    uint32_t line = 16u << line_log;                 /* 行字节数 */
    uint32_t way;
    uint32_t set;

    CC_DSB();
    for (way = 0u; way < ways; way++) {
        for (set = 0u; set < sets; set++) {
            /* DCISW 的格式：way[31:30] | set[26:5] | 0[4:0] */
            H750_REG32(H750_SCB_DCISW) = (way << 30) | (set << 5);
        }
    }
    (void)line;
    CC_DSB();
    CC_ISB();
}

void cc_dcache_enable(void)
{
    uint32_t ccr = H750_REG32(H750_SCB_CCR);
    H750_REG32(H750_SCB_CCR) = ccr | SCB_CCR_DC | SCB_CCR_IC;
    CC_DSB();
    CC_ISB();
    s_enabled = 1u;
}

void cc_dcache_disable(void)
{
    uint32_t ccr = H750_REG32(H750_SCB_CCR);
    H750_REG32(H750_SCB_CCR) = ccr & ~SCB_CCR_DC;
    CC_DSB();
    CC_ISB();
    s_enabled = 0u;
}

uint32_t cc_dcache_enabled(void)
{
    return (H750_REG32(H750_SCB_CCR) & SCB_CCR_DC) != 0u ? 1u : 0u;
}

void cc_get_stats(cc_stats_t *st)
{
    if (st == 0) {
        return;
    }
    (void)memset(st, 0, sizeof(*st));
    st->dirty_lines = 0u;   /* 目标上不通过模型统计 */
    st->valid_lines = 0u;
}

void cc_reset_stats(void)
{
    s_enabled = s_enabled;   /* 目标构建无软件计数器 */
}

void cc_init(void)
{
    cc_dcache_invalidate_all();   /* 先无效化，避免上电残留脏行 */
    cc_dcache_enable();
}

#endif /* H750_PC_SIM */
