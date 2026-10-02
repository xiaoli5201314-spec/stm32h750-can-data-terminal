/*
 * test_board_clock_sdram.c
 * ---------------------------------------------------------------------------
 * 板级测试：时钟树寄存器落地、Flash 等待周期、引脚资源规划表、
 * SDRAM 时序参数计算与上电自检。
 */
#include <string.h>
#include "test_framework.h"
#include "board_init.h"
#include "sdr_bsp.h"
#include "mpu_config.h"
#include "hal_stub.h"

void test_board_clock_sdram(void)
{
    sdr_timing_result_t tr;
    const board_clock_t *ck;

    tf_suite_begin("时钟树 / 引脚资源规划 / SDRAM 时序");

    /* ---- 1. 时钟计算 ---- */
    TF_ASSERT_EQ(board_calc_pll_vco(25000000u, 5u, 192u), 960000000u);
    TF_ASSERT_EQ(board_calc_sysclk(25000000u, 5u, 192u, 2u), 480000000u);
    TF_ASSERT_EQ(board_calc_sysclk(25000000u, 5u, 192u, 0u), 0u);
    TF_ASSERT_EQ(board_calc_pll_vco(25000000u, 0u, 192u), 0u);
    TF_ASSERT_EQ(board_flash_latency_for(480000000u, 0u), 4u);
    TF_ASSERT_EQ(board_flash_latency_for(100000000u, 0u), 0u);
    TF_ASSERT_EQ(board_sdclk_div_code(240000000u, 120000000u), FMC_SDCR_SDCLK_DIV2);
    TF_ASSERT_EQ(board_sdclk_div_code(240000000u, 240000000u), FMC_SDCR_SDCLK_DIV1);
    TF_ASSERT_EQ(board_sdclk_div_code(240000000u, 80000000u), FMC_SDCR_SDCLK_DIV3);

    /* ---- 2. 时钟树寄存器落地 ---- */
    hal_sim_reset();
    TF_ASSERT_EQ(board_clock_init(), 0);
    TF_ASSERT_EQ(H750_REG32(H750_RCC_BASE + RCC_PLLCKSELR),
                 (RCC_PLLCKSELR_PLLSRC_HSE << RCC_PLLCKSELR_PLLSRC_Pos) |
                 ((H750_PLL1_M << RCC_PLLCKSELR_DIVM1_Pos) & RCC_PLLCKSELR_DIVM1_Msk));
    TF_ASSERT_EQ(H750_REG32(H750_RCC_BASE + RCC_PLL1DIVR),
                 ((H750_PLL1_N << RCC_PLL1DIVR_DIVN_Pos) & RCC_PLL1DIVR_DIVN_Msk) |
                 ((H750_PLL1_P << RCC_PLL1DIVR_DIVP_Pos) & RCC_PLL1DIVR_DIVP_Msk) |
                 ((H750_PLL1_Q << RCC_PLL1DIVR_DIVQ_Pos) & RCC_PLL1DIVR_DIVQ_Msk) |
                 ((H750_PLL1_R << RCC_PLL1DIVR_DIVR_Pos) & RCC_PLL1DIVR_DIVR_Msk));
    TF_ASSERT_EQ(H750_REG32(H750_RCC_BASE + RCC_PLLCFGR),
                 RCC_PLLCFGR_DIVP1EN | RCC_PLLCFGR_DIVQ1EN | RCC_PLLCFGR_DIVR1EN);
    TF_ASSERT_EQ((H750_REG32(H750_RCC_BASE + RCC_CFGR) & RCC_CFGR_SW_Msk) >> RCC_CFGR_SW_Pos,
                 RCC_CFGR_SW_PLL1);
    TF_ASSERT_EQ((H750_REG32(H750_RCC_BASE + RCC_D1CFGR) & RCC_D1CFGR_HPRE_Msk) >> RCC_D1CFGR_HPRE_Pos,
                 RCC_PRESC_DIV2);
    TF_ASSERT_EQ((H750_REG32(H750_RCC_BASE + RCC_D1CFGR) & RCC_D1CFGR_D1CPRE_Msk) >> RCC_D1CFGR_D1CPRE_Pos,
                 RCC_PRESC_DIV1);
    TF_ASSERT_EQ((H750_REG32(H750_RCC_BASE + RCC_D2CFGR) & RCC_D2CFGR_D2PPRE1_Msk) >> RCC_D2CFGR_D2PPRE1_Pos,
                 RCC_PPRE_DIV2);
    TF_ASSERT_EQ((H750_REG32(H750_RCC_BASE + RCC_D3CFGR) & RCC_D3CFGR_D3PPRE_Msk) >> RCC_D3CFGR_D3PPRE_Pos,
                 RCC_PPRE_DIV2);
    TF_ASSERT_EQ((H750_REG32(H750_RCC_BASE + RCC_D1CFGR) & RCC_D1CFGR_D1PPRE_Msk) >> RCC_D1CFGR_D1PPRE_Pos,
                 RCC_PPRE_DIV2);
    TF_ASSERT_EQ((H750_REG32(H750_RCC_BASE + RCC_D2CCIP1R) & RCC_D2CCIP1R_FDCANSEL_Msk)
                 >> RCC_D2CCIP1R_FDCANSEL_Pos, RCC_D2CCIP1R_FDCANSEL_PLL1Q);
    TF_ASSERT_EQ((H750_REG32(H750_RCC_BASE + RCC_D1CCIPR) & RCC_D1CCIPR_QSPISEL_Msk)
                 >> RCC_D1CCIPR_QSPISEL_Pos, RCC_D1CCIPR_QSPISEL_PLL1Q);
    TF_ASSERT_EQ(H750_REG32(H750_RCC_BASE + RCC_D1CCIPR) & RCC_D1CCIPR_SDMMCSEL, 0u);
    TF_ASSERT_EQ(H750_REG32(H750_FLASH_REG_BASE + FLASH_ACR) & FLASH_ACR_LATENCY_Msk,
                 H750_FLASH_LATENCY);
    TF_ASSERT_EQ((H750_REG32(H750_PWR_BASE + PWR_D3CR) & PWR_D3CR_VOS_Msk) >> PWR_D3CR_VOS_Pos, 3u);
    TF_ASSERT((H750_REG32(H750_RCC_BASE + RCC_AHB3ENR) & RCC_AHB3ENR_FMCEN) != 0u);
    TF_ASSERT((H750_REG32(H750_RCC_BASE + RCC_AHB3ENR) & RCC_AHB3ENR_QSPIEN) != 0u);
    TF_ASSERT((H750_REG32(H750_RCC_BASE + RCC_AHB1ENR) & RCC_AHB1ENR_ETH1MACEN) != 0u);
    TF_ASSERT((H750_REG32(H750_RCC_BASE + RCC_APB1HENR) & RCC_APB1HENR_FDCANEN) != 0u);

    ck = board_clock_get();
    TF_ASSERT_EQ(ck->sysclk_hz, 480000000u);
    TF_ASSERT_EQ(ck->hclk_hz, 240000000u);
    TF_ASSERT_EQ(ck->apb_hz, 120000000u);
    TF_ASSERT_EQ(ck->fdcan_kernel_hz, 240000000u);
    TF_ASSERT_EQ(ck->sdram_clk_hz, 120000000u);
    TF_ASSERT_EQ(ck->qspi_clk_hz, 60000000u);
    tf_note("时钟：SYSCLK=%u MHz CPU=%u MHz HCLK=%u MHz APB=%u MHz FDCAN=%u MHz SDCLK=%u MHz QSPI=%u MHz",
            ck->sysclk_hz / 1000000u, ck->cpuclk_hz / 1000000u, ck->hclk_hz / 1000000u,
            ck->apb_hz / 1000000u, ck->fdcan_kernel_hz / 1000000u,
            ck->sdram_clk_hz / 1000000u, ck->qspi_clk_hz / 1000000u);

    /* ---- 3. 引脚资源规划 ---- */
    TF_ASSERT_EQ(h750_pin_table_size, 104u);            /* LQFP144 的 114 个 IO 中用 104 个 */
    TF_ASSERT_EQ(board_pin_table_check(), 0);
    TF_ASSERT_EQ(board_pin_count_by_af(12u), 39u + 6u);  /* FMC 39 + SDMMC1 6（都是 AF12） */
    TF_ASSERT_EQ(board_pin_count_by_af(14u), 20u);       /* LTDC RGB565 16 位 + 4 同步 */
    TF_ASSERT_EQ(board_pin_count_by_af(11u), 9u);        /* RMII */
    TF_ASSERT_EQ(board_pin_count_by_af(9u), 6u + 4u);    /* QSPI 6 + FDCAN 4（都是 AF9） */
    TF_ASSERT_EQ(board_pin_count_by_af(7u), 2u);         /* USART1 */
    TF_ASSERT_EQ(board_pin_count_by_af(4u), 2u);         /* I2C4 */
    tf_note("引脚：共 %u 个（LQFP144 可用 114，占用率 %u.%u%%）",
            h750_pin_table_size, (h750_pin_table_size * 100u) / 114u,
            ((h750_pin_table_size * 1000u) / 114u) % 10u);

    /* 逐引脚检查复用寄存器落地 */
    board_gpio_init();
    {
        uint32_t i;
        for (i = 0u; i < h750_pin_table_size; i++) {
            const h750_pin_cfg_t *p = &h750_pin_table[i];
            uint32_t moder = H750_REG32(p->port + GPIO_MODER);
            uint32_t mode = (moder >> (p->pin * 2u)) & 0x3u;
            TF_ASSERT_EQ(mode, p->mode);
            if (p->mode == GPIO_MODE_AF) {
                if (p->pin < 8u) {
                    uint32_t afr = H750_REG32(p->port + GPIO_AFRL);
                    TF_ASSERT_EQ((afr >> (p->pin * 4u)) & 0xFu, (uint32_t)p->af);
                } else {
                    uint32_t afr = H750_REG32(p->port + GPIO_AFRH);
                    TF_ASSERT_EQ((afr >> ((p->pin - 8u) * 4u)) & 0xFu, (uint32_t)p->af);
                }
            }
        }
    }

    /* ---- 4. SDRAM 时序参数计算（120MHz，W9825G6KH-6 等级） ---- */
    TF_ASSERT_EQ(sdr_ns_to_cycles(18u, 120000000u), 3u);
    TF_ASSERT_EQ(sdr_ns_to_cycles(42u, 120000000u), 6u);
    TF_ASSERT_EQ(sdr_ns_to_cycles(60u, 120000000u), 8u);
    TF_ASSERT_EQ(sdr_ns_to_cycles(1u, 120000000u), 1u);      /* 向上取整 */
    TF_ASSERT_EQ(sdr_ns_to_cycles(0u, 120000000u), 0u);

    TF_ASSERT_EQ(sdr_calc_sdtr(sdr_default_timing(), H750_SDRAM_CLK_HZ, &tr), 0);
    TF_ASSERT_EQ(tr.trcd_cycles, 3u);
    TF_ASSERT_EQ(tr.trp_cycles, 3u);
    TF_ASSERT_EQ(tr.tras_cycles, 6u);
    TF_ASSERT_EQ(tr.trc_cycles, 8u);
    TF_ASSERT_EQ(tr.twr_cycles, 2u);
    TF_ASSERT_EQ(tr.txsr_cycles, 9u);
    TF_ASSERT_EQ(tr.tmrd_cycles, 2u);
    TF_ASSERT_EQ(tr.sdtr, 0x02217581u);
    tf_note("SDRAM SDTR=0x%08X（tRCD=%u tRP=%u tRAS=%u tRC=%u tWR=%u tXSR=%u tMRD=%u 周期）",
            tr.sdtr, tr.trcd_cycles, tr.trp_cycles, tr.tras_cycles, tr.trc_cycles,
            tr.twr_cycles, tr.txsr_cycles, tr.tmrd_cycles);

    {
        uint32_t margin = sdr_timing_margin_pm(&tr, sdr_default_timing());
        TF_ASSERT(margin >= 1000u);      /* 每一项都满足器件要求 */
        tf_note("时序余量最小值 %u permille（即 %.1f%% 裕量）", margin,
                ((double)margin - 1000.0) / 10.0);
    }

    TF_ASSERT_EQ(sdr_calc_sdrtr(H750_SDRAM_CLK_HZ, 64u, 4096u), 0x00000E7Fu);
    TF_ASSERT_EQ(sdr_calc_sdrtr(0u, 64u, 4096u), 0u);
    TF_ASSERT_EQ(sdr_calc_sdcr(sdr_default_geometry(), H750_HCLK_HZ, H750_SDRAM_CLK_HZ),
                 0x000024D5u);
    tf_note("SDRAM SDCR=0x%08X SDRTR=0x%08X（刷新周期 %.3f us/行）",
            sdr_calc_sdcr(sdr_default_geometry(), H750_HCLK_HZ, H750_SDRAM_CLK_HZ),
            sdr_calc_sdrtr(H750_SDRAM_CLK_HZ, 64u, 4096u),
            (64.0 * 1000.0) / 4096.0);

    /* ---- 5. SDRAM 初始化序列与自检 ---- */
    hal_sim_reset();
    TF_ASSERT_EQ(sdr_init(), 0);
    TF_ASSERT_EQ(H750_REG32(H750_FMC_BASE + FMC_SDCR1), 0x000024D5u);
    TF_ASSERT_EQ(H750_REG32(H750_FMC_BASE + FMC_SDTR1), 0x02217581u);
    TF_ASSERT_EQ(H750_REG32(H750_FMC_BASE + FMC_SDRTR), 0x00000E7Fu);
    TF_ASSERT_EQ(H750_REG32(H750_FMC_BASE + FMC_SDCMR) & FMC_SDCMR_MODE_Msk,
                 FMC_SDCMR_MODE_LOAD_MODE << FMC_SDCMR_MODE_Pos);

    TF_ASSERT_EQ(sdr_selftest(H750_SDRAM_BASE, 64u * 1024u), 0u);
    TF_ASSERT_EQ(sdr_selftest(H750_SDRAM_NC_BASE, 65536u), 0u);
    tf_note("SDRAM 自检：64KB 可缓存区 + 64KB 非缓存窗口，错误字节数 0");

    /* 非缓存窗口属于可 DMA 直写的区域 */
    TF_ASSERT_EQ(mpu_region_is_cacheable(0xC1000000u), 0);
    TF_ASSERT((int)mpu_region_is_cacheable(0xC0000000u) == 1);

    tf_suite_end();
}
