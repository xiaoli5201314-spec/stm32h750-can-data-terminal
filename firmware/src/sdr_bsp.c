/*
 * sdr_bsp.c
 * ---------------------------------------------------------------------------
 * SDRAM BSP 实现。所有时序参数按 W9825G6KH-6 等级（166MHz）在 120MHz 下
 * 使用，计算过程见 docs/HARDWARE.md 的时序核算表。
 */
#include <string.h>
#include "sdr_bsp.h"
#include "board_init.h"
#include "hal_stub.h"

static const sdr_timing_ns_t s_timing = {
    H750_SDRAM_TRCD_NS,
    H750_SDRAM_TRP_NS,
    H750_SDRAM_TRAS_NS,
    H750_SDRAM_TRC_NS,
    H750_SDRAM_TWR_NS,
    H750_SDRAM_TRFC_NS,
    H750_SDRAM_TXSR_NS,
    H750_SDRAM_TMRD_NS
};

static const sdr_geometry_t s_geometry = {
    H750_SDRAM_BANK_BITS,
    H750_SDRAM_ROW_BITS,
    H750_SDRAM_COL_BITS,
    H750_SDRAM_WIDTH_BITS,
    H750_SDRAM_CAS_LATENCY,
    H750_SDRAM_REFRESH_MS,
    H750_SDRAM_ROWS
};

const sdr_timing_ns_t *sdr_default_timing(void)
{
    return &s_timing;
}

const sdr_geometry_t *sdr_default_geometry(void)
{
    return &s_geometry;
}

uint32_t sdr_ns_to_cycles(uint32_t ns, uint32_t clk_hz)
{
    uint64_t prod;
    if (clk_hz == 0u) {
        return 0u;
    }
    prod = (uint64_t)ns * (uint64_t)clk_hz;
    return (uint32_t)((prod + 999999999ull) / 1000000000ull);   /* 向上取整 */
}

static uint32_t field_from_cycles(uint32_t cycles)
{
    if (cycles == 0u) {
        return 0u;
    }
    return (cycles - 1u) & 0xFu;
}

int sdr_calc_sdtr(const sdr_timing_ns_t *t, uint32_t clk_hz, sdr_timing_result_t *out)
{
    uint32_t trcd;
    uint32_t trp;
    uint32_t tras;
    uint32_t trc;
    uint32_t twr;
    uint32_t txsr;
    uint32_t tmrd;
    uint32_t trfc;
    uint32_t v = 0u;

    if ((t == 0) || (out == 0)) {
        return -1;
    }
    trcd = sdr_ns_to_cycles(t->trcd_ns, clk_hz);
    trp  = sdr_ns_to_cycles(t->trp_ns, clk_hz);
    tras = sdr_ns_to_cycles(t->tras_ns, clk_hz);
    trc  = sdr_ns_to_cycles(t->trc_ns, clk_hz);
    twr  = sdr_ns_to_cycles(t->twr_ns, clk_hz);
    txsr = sdr_ns_to_cycles(t->txsr_ns, clk_hz);
    tmrd = sdr_ns_to_cycles(t->tmrd_ns, clk_hz);
    trfc = sdr_ns_to_cycles(t->trfc_ns, clk_hz);

    (void)memset(out, 0, sizeof(*out));
    out->clk_hz = clk_hz;
    out->trcd_cycles = trcd;
    out->trp_cycles = trp;
    out->tras_cycles = tras;
    out->trc_cycles = trc;
    out->twr_cycles = twr;
    out->txsr_cycles = txsr;
    out->tmrd_cycles = tmrd;
    out->trfc_cycles = trfc;

    v |= (field_from_cycles(tmrd) << FMC_SDTR_TMRD_Pos);
    v |= (field_from_cycles(txsr) << FMC_SDTR_TXSR_Pos);
    v |= (field_from_cycles(tras) << FMC_SDTR_TRAS_Pos);
    v |= (field_from_cycles(trc)  << FMC_SDTR_TRC_Pos);
    v |= (field_from_cycles(twr)  << FMC_SDTR_TWR_Pos);
    v |= (field_from_cycles(trp)  << FMC_SDTR_TRP_Pos);
    v |= (field_from_cycles(trcd) << FMC_SDTR_TRCD_Pos);
    out->sdtr = v;

    out->worst_case_ns = t->tras_ns;
    if (t->trc_ns > out->worst_case_ns) { out->worst_case_ns = t->trc_ns; }
    if (t->txsr_ns > out->worst_case_ns) { out->worst_case_ns = t->txsr_ns; }
    return 0;
}

uint32_t sdr_calc_sdrtr(uint32_t clk_hz, uint32_t refresh_ms, uint32_t rows)
{
    uint64_t period_ns;
    uint64_t count;

    if ((clk_hz == 0u) || (rows == 0u) || (refresh_ms == 0u)) {
        return 0u;
    }
    /* 每行刷新间隔（ns） = refresh_ms * 1e6 / rows */
    period_ns = ((uint64_t)refresh_ms * 1000000ull) / (uint64_t)rows;
    count = ((uint64_t)clk_hz * period_ns) / 1000000000ull;
    if (count > 20ull) {
        count -= 20ull;      /* 手册要求减去 20 个 SDCLK 周期的安全余量 */
    } else {
        count = 0ull;
    }
    if (count > 0x1FFFull) {
        count = 0x1FFFull;
    }
    return FMC_SDRTR_CRE | (((uint32_t)count << FMC_SDRTR_COUNT_Pos) & FMC_SDRTR_COUNT_Msk);
}

uint32_t sdr_calc_sdcr(const sdr_geometry_t *g, uint32_t hclk_hz, uint32_t sdram_clk_hz)
{
    uint32_t v = 0u;
    uint32_t nc;
    uint32_t nr;

    if (g == 0) {
        return 0u;
    }
    nc = (g->col_bits > 8u) ? (g->col_bits - 8u) : 0u;
    nr = (g->row_bits > 11u) ? (g->row_bits - 11u) : 0u;
    if (nc > 3u) { nc = 3u; }
    if (nr > 3u) { nr = 3u; }
    v |= (nc << FMC_SDCR_NC_Pos);
    v |= (nr << FMC_SDCR_NR_Pos);
    v |= ((g->width_bits == 8u) ? FMC_SDCR_MWID_8
         : (g->width_bits == 16u) ? FMC_SDCR_MWID_16
         : FMC_SDCR_MWID_32) << FMC_SDCR_MWID_Pos;
    if (g->bank_bits == 2u) {
        v |= FMC_SDCR_NB;                    /* 4 个内部 bank */
    }
    v |= (((g->cas_latency > 0u) ? (g->cas_latency - 1u) : 0u) << FMC_SDCR_CAS_Pos) & FMC_SDCR_CAS_Msk;
    v |= (board_sdclk_div_code(hclk_hz, sdram_clk_hz) << FMC_SDCR_SDCLK_Pos) & FMC_SDCR_SDCLK_Msk;
    /* HCLK > 100MHz 时读管道建议开启 1 个 HCLK 延迟 */
    if (hclk_hz > 100000000u) {
        v |= (1u << FMC_SDCR_RPIPE_Pos);
    }
    return v;
}

uint32_t sdr_timing_margin_pm(const sdr_timing_result_t *r, const sdr_timing_ns_t *t)
{
    uint32_t worst = 1000u;   /* 千分比 */
    uint64_t need_ns;
    uint64_t got_ns;

    if ((r == 0) || (t == 0) || (r->clk_hz == 0u)) {
        return 0u;
    }
    /* 逐项核算：实际配置周期数换算回 ns，与需求比较 */
#define SDR_MARGIN_ONE(cycles, req_ns)                                        \
    do {                                                                      \
        got_ns = ((uint64_t)(cycles) * 1000000000ull) / (uint64_t)r->clk_hz;   \
        need_ns = (uint64_t)(req_ns);                                         \
        if (need_ns > 0ull) {                                                 \
            uint32_t pm = (uint32_t)((got_ns * 1000ull) / need_ns);           \
            if (pm < worst) { worst = pm; }                                   \
        }                                                                     \
    } while (0)

    SDR_MARGIN_ONE(r->trcd_cycles, t->trcd_ns);
    SDR_MARGIN_ONE(r->trp_cycles,  t->trp_ns);
    SDR_MARGIN_ONE(r->tras_cycles, t->tras_ns);
    SDR_MARGIN_ONE(r->trc_cycles,  t->trc_ns);
    SDR_MARGIN_ONE(r->twr_cycles,  t->twr_ns);
    SDR_MARGIN_ONE(r->txsr_cycles, t->txsr_ns);
    SDR_MARGIN_ONE(r->tmrd_cycles, t->tmrd_ns);
#undef SDR_MARGIN_ONE
    return worst;
}

/* ---------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */
static void fmc_wait_ready(void)
{
    uint32_t guard = 0u;
    while (((H750_REG32(H750_FMC_BASE + FMC_SDSR) & FMC_SDSR_BUSY) != 0u) && (guard < 100000u)) {
        guard++;
    }
}

int sdr_init(void)
{
    sdr_timing_result_t tr;
    uint32_t sdcr;
    uint32_t sdtr;
    uint32_t sdrtr;
    uint32_t cmr;

    if (sdr_calc_sdtr(&s_timing, H750_SDRAM_CLK_HZ, &tr) != 0) {
        return -1;
    }
    sdcr = sdr_calc_sdcr(&s_geometry, H750_HCLK_HZ, H750_SDRAM_CLK_HZ);
    sdtr = tr.sdtr;
    sdrtr = sdr_calc_sdrtr(H750_SDRAM_CLK_HZ, s_geometry.refresh_ms, s_geometry.rows);

    H750_REG32(H750_FMC_BASE + FMC_SDCR1) = sdcr;
    H750_REG32(H750_FMC_BASE + FMC_SDTR1) = sdtr;

    /* 1) 时钟使能命令：等待 tMRD 以上 */
    cmr = (FMC_SDCMR_MODE_CLK_ENABLE << FMC_SDCMR_MODE_Pos) | FMC_SDCMR_CTB1;
    H750_REG32(H750_FMC_BASE + FMC_SDCMR) = cmr;
    hal_delay_us(1);

    /* 2) 预充电所有 bank */
    cmr = (FMC_SDCMR_MODE_PALL << FMC_SDCMR_MODE_Pos) | FMC_SDCMR_CTB1;
    H750_REG32(H750_FMC_BASE + FMC_SDCMR) = cmr;
    fmc_wait_ready();

    /* 3) 连续 8 次自动刷新（上电初始化要求至少 8 次） */
    cmr = (FMC_SDCMR_MODE_AUTOREFRESH << FMC_SDCMR_MODE_Pos) |
          FMC_SDCMR_CTB1 | (7u << FMC_SDCMR_NRFS_Pos);
    H750_REG32(H750_FMC_BASE + FMC_SDCMR) = cmr;
    fmc_wait_ready();

    /* 4) 装载模式寄存器：突发长度 1、顺序、CAS 潜伏期、写突发 */
    cmr = (FMC_SDCMR_MODE_LOAD_MODE << FMC_SDCMR_MODE_Pos) |
          FMC_SDCMR_CTB1 |
          (((uint32_t)SDR_MODE_BURST_LEN_1 |
            (s_geometry.cas_latency << SDR_MODE_CAS_Pos) |
            SDR_MODE_WB) << FMC_SDCMR_MRD_Pos);
    H750_REG32(H750_FMC_BASE + FMC_SDCMR) = cmr;
    fmc_wait_ready();

    /* 5) 使能自动刷新计数 */
    H750_REG32(H750_FMC_BASE + FMC_SDRTR) = sdrtr;

    return 0;
}

uint32_t sdr_selftest(uint32_t base, uint32_t size)
{
    volatile uint32_t *p = (volatile uint32_t *)hal_mem_ptr(base, size);
    uint32_t words;
    uint32_t i;
    uint32_t errors = 0u;
    uint32_t patterns[4];

    if ((p == 0) || (size < 64u)) {
        return 0xFFFFFFFFu;
    }
    patterns[0] = 0x55555555u;
    patterns[1] = 0xAAAAAAAAu;
    patterns[2] = 0x00000000u;
    patterns[3] = 0xFFFFFFFFu;
    words = size / 4u;

    /* 图案写入 + 读回；再按地址相关图案检查地址线 */
    {
        uint32_t k;
        for (k = 0u; k < 4u; k++) {
            for (i = 0u; i < words; i++) {
                p[i] = patterns[k];
            }
            for (i = 0u; i < words; i++) {
                if (p[i] != patterns[k]) {
                    errors++;
                }
            }
        }
        for (i = 0u; i < words; i++) {
            p[i] = i ^ 0xA5A5A5A5u;
        }
        for (i = 0u; i < words; i++) {
            if (p[i] != (i ^ 0xA5A5A5A5u)) {
                errors++;
            }
        }
    }
    return errors;
}

uint32_t sdr_bandwidth_probe(uint32_t base, uint32_t size)
{
    volatile uint32_t *p = (volatile uint32_t *)hal_mem_ptr(base, size);
    uint32_t words;
    uint32_t i;
    uint32_t acc = 0u;

    if ((p == 0) || (size < 64u)) {
        return 0u;
    }
    words = size / 4u;
    for (i = 0u; i < words; i++) {
        p[i] = i;
    }
    for (i = 0u; i < words; i++) {
        acc ^= p[i];
    }
    return acc;   /* 供调用方校验数据通路，不参与耗时统计 */
}
