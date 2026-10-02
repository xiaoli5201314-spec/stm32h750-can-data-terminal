/*
 * board_init.c
 * ---------------------------------------------------------------------------
 * 板级初始化实现：时钟树、电源域、等待周期、引脚复用表。
 *
 * 时钟方案（HSE 25MHz）：
 *   PLL1: M=5 -> 5MHz 参考, N=192 -> VCO 960MHz, P=2 -> SYSCLK 480MHz,
 *         Q=4 -> 240MHz（FDCAN / QSPI / SDMMC 内核时钟）, R=2 -> 480MHz
 *   D1CPRE = /1  -> CPU 480MHz（需 VOS0）
 *   HPRE   = /2  -> HCLK3 240MHz
 *   D1/D2/D3 PPRE = /2 -> APB 120MHz
 *   FMC 内核 = HCLK3 240MHz，SDCLK = HCLK3/2 = 120MHz
 *   QSPI 内核 = PLL1Q 240MHz，分频 /4 -> 60MHz
 *   SDMMC 内核 = PLL1Q 240MHz，分频 /5 -> 48MHz（3.3V 下 SD 高速模式上限 50MHz）
 *   FDCAN 内核 = PLL1Q 240MHz
 */
#include "board_init.h"
#include "hal_stub.h"

/* 引脚表宏：把"资源规划"写成一张可直接初始化硬件的表 */
#define P_AF(port, pin, af, spd, name) \
    { port, pin, GPIO_MODE_AF, af, 0u, spd, 0u, name }
#define P_OUT(port, pin, spd, pupd, name) \
    { port, pin, GPIO_MODE_OUTPUT, 0u, 0u, spd, pupd, name }
#define P_IN(port, pin, pupd, name) \
    { port, pin, GPIO_MODE_INPUT, 0u, 0u, GPIO_OSPEED_LOW, pupd, name }
#define P_OSC(port, pin, name) \
    { port, pin, GPIO_MODE_ANALOG, 0u, 0u, GPIO_OSPEED_LOW, 0u, name }

/* 复用功能号（按器件数据手册的复用功能表） */
#define AF_FMC      12u
#define AF_LTDC     14u
#define AF_QSPI      9u
#define AF_SDMMC1   12u
#define AF_ETH      11u
#define AF_FDCAN     9u
#define AF_USART1    7u
#define AF_I2C4      4u
#define AF_TIM2      1u
#define AF_SWD       0u

const h750_pin_cfg_t h750_pin_table[] = {
    /* ------ FMC SDRAM（Bank1，16bit，39 根） ------ */
    P_AF(H750_GPIOF_BASE,  0u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A0/SDRAM_A0"),
    P_AF(H750_GPIOF_BASE,  1u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A1/SDRAM_A1"),
    P_AF(H750_GPIOF_BASE,  2u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A2/SDRAM_A2"),
    P_AF(H750_GPIOF_BASE,  3u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A3/SDRAM_A3"),
    P_AF(H750_GPIOF_BASE,  4u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A4/SDRAM_A4"),
    P_AF(H750_GPIOF_BASE,  5u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A5/SDRAM_A5"),
    P_AF(H750_GPIOF_BASE, 12u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A6/SDRAM_A6"),
    P_AF(H750_GPIOF_BASE, 13u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A7/SDRAM_A7"),
    P_AF(H750_GPIOF_BASE, 14u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A8/SDRAM_A8"),
    P_AF(H750_GPIOF_BASE, 15u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A9/SDRAM_A9"),
    P_AF(H750_GPIOG_BASE,  0u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A10/SDRAM_A10"),
    P_AF(H750_GPIOG_BASE,  1u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A11/SDRAM_A11"),
    P_AF(H750_GPIOG_BASE,  2u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_A12/SDRAM_A12"),
    P_AF(H750_GPIOG_BASE,  4u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_BA0/SDRAM_BA0"),
    P_AF(H750_GPIOG_BASE,  5u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_BA1/SDRAM_BA1"),
    P_AF(H750_GPIOD_BASE, 14u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D0/SDRAM_DQ0"),
    P_AF(H750_GPIOD_BASE, 15u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D1/SDRAM_DQ1"),
    P_AF(H750_GPIOD_BASE,  0u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D2/SDRAM_DQ2"),
    P_AF(H750_GPIOD_BASE,  1u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D3/SDRAM_DQ3"),
    P_AF(H750_GPIOE_BASE,  7u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D4/SDRAM_DQ4"),
    P_AF(H750_GPIOE_BASE,  8u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D5/SDRAM_DQ5"),
    P_AF(H750_GPIOE_BASE,  9u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D6/SDRAM_DQ6"),
    P_AF(H750_GPIOE_BASE, 10u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D7/SDRAM_DQ7"),
    P_AF(H750_GPIOE_BASE, 11u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D8/SDRAM_DQ8"),
    P_AF(H750_GPIOE_BASE, 12u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D9/SDRAM_DQ9"),
    P_AF(H750_GPIOE_BASE, 13u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D10/SDRAM_DQ10"),
    P_AF(H750_GPIOE_BASE, 14u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D11/SDRAM_DQ11"),
    P_AF(H750_GPIOE_BASE, 15u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D12/SDRAM_DQ12"),
    P_AF(H750_GPIOD_BASE,  8u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D13/SDRAM_DQ13"),
    P_AF(H750_GPIOD_BASE,  9u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D14/SDRAM_DQ14"),
    P_AF(H750_GPIOD_BASE, 10u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_D15/SDRAM_DQ15"),
    P_AF(H750_GPIOE_BASE,  0u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_NBL0/SDRAM_LDQM"),
    P_AF(H750_GPIOE_BASE,  1u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_NBL1/SDRAM_UDQM"),
    P_AF(H750_GPIOG_BASE,  8u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_SDCLK/SDRAM_CLK"),
    P_AF(H750_GPIOF_BASE, 11u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_SDNRAS/SDRAM_RAS"),
    P_AF(H750_GPIOG_BASE, 15u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_SDNCAS/SDRAM_CAS"),
    P_AF(H750_GPIOC_BASE,  0u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_SDNWE/SDRAM_WE"),
    P_AF(H750_GPIOC_BASE,  2u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_SDNE0/SDRAM_CS"),
    P_AF(H750_GPIOC_BASE,  3u, AF_FMC, GPIO_OSPEED_VERYHIGH, "FMC_SDCKE0/SDRAM_CKE"),

    /* ------ RMII 以太网（LAN8720A） ------ */
    P_AF(H750_GPIOA_BASE,  1u, AF_ETH, GPIO_OSPEED_VERYHIGH, "ETH_REF_CLK"),
    P_AF(H750_GPIOA_BASE,  2u, AF_ETH, GPIO_OSPEED_MEDIUM,   "ETH_MDIO"),
    P_AF(H750_GPIOC_BASE,  1u, AF_ETH, GPIO_OSPEED_MEDIUM,   "ETH_MDC"),
    P_AF(H750_GPIOA_BASE,  7u, AF_ETH, GPIO_OSPEED_VERYHIGH, "ETH_CRS_DV"),
    P_AF(H750_GPIOC_BASE,  4u, AF_ETH, GPIO_OSPEED_VERYHIGH, "ETH_RXD0"),
    P_AF(H750_GPIOC_BASE,  5u, AF_ETH, GPIO_OSPEED_VERYHIGH, "ETH_RXD1"),
    P_AF(H750_GPIOG_BASE, 11u, AF_ETH, GPIO_OSPEED_VERYHIGH, "ETH_TX_EN"),
    P_AF(H750_GPIOG_BASE, 13u, AF_ETH, GPIO_OSPEED_VERYHIGH, "ETH_TXD0"),
    P_AF(H750_GPIOG_BASE, 14u, AF_ETH, GPIO_OSPEED_VERYHIGH, "ETH_TXD1"),

    /* ------ 双 FDCAN（ISO1042 隔离收发器） ------ */
    P_AF(H750_GPIOA_BASE, 11u, AF_FDCAN, GPIO_OSPEED_VERYHIGH, "FDCAN1_RX"),
    P_AF(H750_GPIOA_BASE, 12u, AF_FDCAN, GPIO_OSPEED_VERYHIGH, "FDCAN1_TX"),
    P_AF(H750_GPIOB_BASE, 12u, AF_FDCAN, GPIO_OSPEED_VERYHIGH, "FDCAN2_RX"),
    P_AF(H750_GPIOB_BASE, 13u, AF_FDCAN, GPIO_OSPEED_VERYHIGH, "FDCAN2_TX"),

    /* ------ QSPI Flash（8MB SPI NOR） ------ */
    P_AF(H750_GPIOB_BASE,  2u, AF_QSPI, GPIO_OSPEED_VERYHIGH, "QSPI_CLK"),
    P_AF(H750_GPIOB_BASE,  6u, AF_QSPI, GPIO_OSPEED_VERYHIGH, "QSPI_NCS"),
    P_AF(H750_GPIOF_BASE,  8u, AF_QSPI, GPIO_OSPEED_VERYHIGH, "QSPI_IO0"),
    P_AF(H750_GPIOF_BASE,  9u, AF_QSPI, GPIO_OSPEED_VERYHIGH, "QSPI_IO1"),
    P_AF(H750_GPIOF_BASE,  7u, AF_QSPI, GPIO_OSPEED_VERYHIGH, "QSPI_IO2"),
    P_AF(H750_GPIOF_BASE,  6u, AF_QSPI, GPIO_OSPEED_VERYHIGH, "QSPI_IO3"),

    /* ------ SDMMC1（microSD，4bit） ------ */
    P_AF(H750_GPIOC_BASE, 12u, AF_SDMMC1, GPIO_OSPEED_VERYHIGH, "SDMMC1_CK"),
    P_AF(H750_GPIOD_BASE,  2u, AF_SDMMC1, GPIO_OSPEED_VERYHIGH, "SDMMC1_CMD"),
    P_AF(H750_GPIOC_BASE,  8u, AF_SDMMC1, GPIO_OSPEED_VERYHIGH, "SDMMC1_D0"),
    P_AF(H750_GPIOC_BASE,  9u, AF_SDMMC1, GPIO_OSPEED_VERYHIGH, "SDMMC1_D1"),
    P_AF(H750_GPIOC_BASE, 10u, AF_SDMMC1, GPIO_OSPEED_VERYHIGH, "SDMMC1_D2"),
    P_AF(H750_GPIOC_BASE, 11u, AF_SDMMC1, GPIO_OSPEED_VERYHIGH, "SDMMC1_D3"),
    P_IN(H750_GPIOB_BASE,  5u, 1u, "SD_CD"),

    /* ------ LTDC（RGB565，使用 R[7:3]/G[7:2]/B[7:3]） ------ */
    P_AF(H750_GPIOG_BASE,  7u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_CLK"),
    P_AF(H750_GPIOF_BASE, 10u, AF_LTDC, GPIO_OSPEED_HIGH,     "LCD_DE"),
    P_AF(H750_GPIOC_BASE,  6u, AF_LTDC, GPIO_OSPEED_HIGH,     "LCD_HSYNC"),
    P_AF(H750_GPIOA_BASE,  4u, AF_LTDC, GPIO_OSPEED_HIGH,     "LCD_VSYNC"),
    P_AF(H750_GPIOB_BASE,  0u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_R3"),
    P_AF(H750_GPIOA_BASE,  5u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_R4"),
    P_AF(H750_GPIOA_BASE,  9u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_R5"),
    P_AF(H750_GPIOB_BASE,  1u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_R6"),
    P_AF(H750_GPIOG_BASE,  6u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_R7"),
    P_AF(H750_GPIOA_BASE,  6u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_G2"),
    P_AF(H750_GPIOG_BASE, 10u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_G3"),
    P_AF(H750_GPIOB_BASE, 10u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_G4"),
    P_AF(H750_GPIOB_BASE, 11u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_G5"),
    P_AF(H750_GPIOC_BASE,  7u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_G6"),
    P_AF(H750_GPIOD_BASE,  3u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_G7"),
    P_AF(H750_GPIOA_BASE,  8u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_B3"),
    P_AF(H750_GPIOG_BASE, 12u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_B4"),
    P_AF(H750_GPIOA_BASE,  3u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_B5"),
    P_AF(H750_GPIOB_BASE,  8u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_B6"),
    P_AF(H750_GPIOB_BASE,  9u, AF_LTDC, GPIO_OSPEED_VERYHIGH, "LCD_B7"),

    /* ------ 触摸与模拟采集（I2C4 总线） ------ */
    P_AF(H750_GPIOD_BASE, 12u, AF_I2C4, GPIO_OSPEED_MEDIUM, "I2C4_SCL"),
    P_AF(H750_GPIOD_BASE, 13u, AF_I2C4, GPIO_OSPEED_MEDIUM, "I2C4_SDA"),

    /* ------ 控制与指示 ------ */
    P_AF(H750_GPIOA_BASE,  0u, AF_TIM2, GPIO_OSPEED_LOW, "LCD_BL_PWM"),
    P_OUT(H750_GPIOD_BASE, 4u, GPIO_OSPEED_LOW, 1u, "LCD_RST"),
    P_IN(H750_GPIOD_BASE,  5u, 1u, "TOUCH_INT"),
    P_OUT(H750_GPIOD_BASE, 6u, GPIO_OSPEED_LOW, 1u, "TOUCH_RST"),
    P_OUT(H750_GPIOG_BASE, 9u, GPIO_OSPEED_LOW, 0u, "LED_RUN"),
    P_OUT(H750_GPIOG_BASE, 3u, GPIO_OSPEED_LOW, 0u, "LED_CAN1"),
    P_OUT(H750_GPIOC_BASE, 13u, GPIO_OSPEED_LOW, 0u, "LED_ERR"),
    P_IN(H750_GPIOD_BASE,  7u, 1u, "USER_KEY"),

    /* ------ 调试与串口 ------ */
    P_AF(H750_GPIOA_BASE, 13u, AF_SWD, GPIO_OSPEED_HIGH, "SWDIO"),
    P_AF(H750_GPIOA_BASE, 14u, AF_SWD, GPIO_OSPEED_HIGH, "SWCLK"),
    P_AF(H750_GPIOB_BASE,  3u, AF_SWD, GPIO_OSPEED_HIGH, "SWO"),
    P_AF(H750_GPIOB_BASE, 14u, AF_USART1, GPIO_OSPEED_HIGH, "USART1_TX"),
    P_AF(H750_GPIOB_BASE, 15u, AF_USART1, GPIO_OSPEED_HIGH, "USART1_RX"),

    /* ------ 时钟（由 RCC 振荡器驱动，引脚保持模拟态） ------ */
    P_OSC(H750_GPIOH_BASE,  0u, "HSE_IN"),
    P_OSC(H750_GPIOH_BASE,  1u, "HSE_OUT"),
    P_OSC(H750_GPIOC_BASE, 14u, "LSE_IN"),
    P_OSC(H750_GPIOC_BASE, 15u, "LSE_OUT"),
};

const uint32_t h750_pin_table_size =
    (uint32_t)(sizeof(h750_pin_table) / sizeof(h750_pin_table[0]));

/* ---------------------------------------------------------------------------
 * 时钟计算
 * ------------------------------------------------------------------------- */
uint32_t board_calc_pll_vco(uint32_t hse_hz, uint32_t m, uint32_t n)
{
    if ((m == 0u) || (n == 0u)) {
        return 0u;
    }
    return (hse_hz / m) * n;
}

uint32_t board_calc_sysclk(uint32_t hse_hz, uint32_t m, uint32_t n, uint32_t p)
{
    uint32_t vco = board_calc_pll_vco(hse_hz, m, n);
    if (p == 0u) {
        return 0u;
    }
    return vco / p;
}

uint32_t board_flash_latency_for(uint32_t cpuclk_hz, uint8_t vos)
{
    /* VOS0（最高性能）：> 210MHz 需要 4 个等待周期；VOS1：> 185MHz 需要 4 个 */
    (void)vos;
    if (cpuclk_hz > 210000000u) {
        return 4u;
    }
    if (cpuclk_hz > 180000000u) {
        return 3u;
    }
    if (cpuclk_hz > 150000000u) {
        return 2u;
    }
    if (cpuclk_hz > 120000000u) {
        return 1u;
    }
    return 0u;
}

uint32_t board_sdclk_div_code(uint32_t hclk_hz, uint32_t sdram_clk_hz)
{
    if ((sdram_clk_hz == 0u) || (hclk_hz < sdram_clk_hz)) {
        return FMC_SDCR_SDCLK_DIV1;
    }
    if (hclk_hz == sdram_clk_hz) {
        return FMC_SDCR_SDCLK_DIV1;
    }
    if ((hclk_hz / 2u) == sdram_clk_hz) {
        return FMC_SDCR_SDCLK_DIV2;
    }
    if ((hclk_hz / 3u) == sdram_clk_hz) {
        return FMC_SDCR_SDCLK_DIV3;
    }
    return FMC_SDCR_SDCLK_DIV2;   /* 缺省取 /2 */
}

static const board_clock_t s_clock = {
    H750_HSE_HZ,
    H750_SYSCLK_HZ,
    H750_CPUCLK_HZ,
    H750_HCLK_HZ,
    H750_APB_HZ,
    H750_PLL1_VCO_HZ,
    H750_PLL1Q_HZ,
    H750_FDCAN_KERNEL_HZ,
    H750_SDRAM_CLK_HZ,
    H750_QSPI_CLK_HZ,
    H750_SDMMC_CLK_HZ,
    H750_LTDC_PIXCLK_HZ
};

const board_clock_t *board_clock_get(void)
{
    return &s_clock;
}

/* ---------------------------------------------------------------------------
 * 时钟树初始化
 * ------------------------------------------------------------------------- */
int board_clock_init(void)
{
    uint32_t rcc = H750_RCC_BASE;
    uint32_t pwr = H750_PWR_BASE;
    uint32_t reg;

    /* 1) 电压等级 VOS0（480MHz 必需），等待 VOSRDY */
    reg = H750_REG32(pwr + PWR_D3CR);
    reg = (reg & ~PWR_D3CR_VOS_Msk) | (0x3u << PWR_D3CR_VOS_Pos);
    H750_REG32(pwr + PWR_D3CR) = reg;
    {
        uint32_t guard = 0u;
        while (((H750_REG32(pwr + PWR_D3CR) & PWR_D3CR_VOSRDY) == 0u) && (guard < 100000u)) {
            guard++;
        }
    }

    /* 2) 打开 HSE 并等待就绪 */
    H750_REG32(rcc + RCC_CR) |= RCC_CR_HSEON;
    {
        uint32_t guard = 0u;
        while (((H750_REG32(rcc + RCC_CR) & RCC_CR_HSERDY) == 0u) && (guard < 100000u)) {
            guard++;
        }
    }

    /* 3) PLL 输入分频与时钟源：HSE / 5 = 5MHz */
    H750_REG32(rcc + RCC_PLLCKSELR) =
        (RCC_PLLCKSELR_PLLSRC_HSE << RCC_PLLCKSELR_PLLSRC_Pos) |
        ((H750_PLL1_M << RCC_PLLCKSELR_DIVM1_Pos) & RCC_PLLCKSELR_DIVM1_Msk);

    /* 4) PLL1 分频系数：VCO 960MHz，P/Q/R = 2/4/2 */
    H750_REG32(rcc + RCC_PLL1DIVR) =
        ((H750_PLL1_N << RCC_PLL1DIVR_DIVN_Pos) & RCC_PLL1DIVR_DIVN_Msk) |
        ((H750_PLL1_P << RCC_PLL1DIVR_DIVP_Pos) & RCC_PLL1DIVR_DIVP_Msk) |
        ((H750_PLL1_Q << RCC_PLL1DIVR_DIVQ_Pos) & RCC_PLL1DIVR_DIVQ_Msk) |
        ((H750_PLL1_R << RCC_PLL1DIVR_DIVR_Pos) & RCC_PLL1DIVR_DIVR_Msk);

    /* 5) 宽 VCO 范围（192~960MHz），使能三个输出分频 */
    H750_REG32(rcc + RCC_PLLCFGR) =
        RCC_PLLCFGR_DIVP1EN | RCC_PLLCFGR_DIVQ1EN | RCC_PLLCFGR_DIVR1EN;

    /* 6) 启动 PLL1 */
    H750_REG32(rcc + RCC_CR) |= RCC_CR_PLL1ON;
    {
        uint32_t guard = 0u;
        while (((H750_REG32(rcc + RCC_CR) & RCC_CR_PLL1RDY) == 0u) && (guard < 100000u)) {
            guard++;
        }
    }

    /* 7) 域预分频：D1CPRE=/1, HPRE=/2, APBx=/2（注意 PPRE 是 3 位字段，编码与 HPRE 不同） */
    H750_REG32(rcc + RCC_D1CFGR) =
        (RCC_PRESC_DIV1 << RCC_D1CFGR_D1CPRE_Pos) |
        (RCC_PRESC_DIV2 << RCC_D1CFGR_HPRE_Pos) |
        ((uint32_t)RCC_PPRE_DIV2 << RCC_D1CFGR_D1PPRE_Pos);
    H750_REG32(rcc + RCC_D2CFGR) =
        ((uint32_t)RCC_PPRE_DIV2 << RCC_D2CFGR_D2PPRE1_Pos) |
        ((uint32_t)RCC_PPRE_DIV2 << RCC_D2CFGR_D2PPRE2_Pos);
    H750_REG32(rcc + RCC_D3CFGR) =
        ((uint32_t)RCC_PPRE_DIV2 << RCC_D3CFGR_D3PPRE_Pos);

    /* 8) 内核时钟源选择 */
    reg = H750_REG32(rcc + RCC_D1CCIPR);
    reg &= ~(RCC_D1CCIPR_FMCSEL_Msk | RCC_D1CCIPR_QSPISEL_Msk | RCC_D1CCIPR_SDMMCSEL);
    reg |= ((uint32_t)RCC_D1CCIPR_FMCSEL_HCLK3 << RCC_D1CCIPR_FMCSEL_Pos);
    reg |= ((uint32_t)RCC_D1CCIPR_QSPISEL_PLL1Q << RCC_D1CCIPR_QSPISEL_Pos);
    /* SDMMCSEL = 0 -> PLL1Q */
    H750_REG32(rcc + RCC_D1CCIPR) = reg;

    reg = H750_REG32(rcc + RCC_D2CCIP1R);
    reg &= ~RCC_D2CCIP1R_FDCANSEL_Msk;
    reg |= ((uint32_t)RCC_D2CCIP1R_FDCANSEL_PLL1Q << RCC_D2CCIP1R_FDCANSEL_Pos);
    H750_REG32(rcc + RCC_D2CCIP1R) = reg;

    /* 9) Flash 等待周期（VOS0 + 480MHz -> 4WS），同时提高编程/擦除时钟频率档 */
    reg = H750_REG32(H750_FLASH_REG_BASE + FLASH_ACR);
    reg &= ~(FLASH_ACR_LATENCY_Msk | FLASH_ACR_WRHIGHFREQ_Msk);
    reg |= (H750_FLASH_LATENCY << FLASH_ACR_LATENCY_Pos);
    reg |= (0x2u << FLASH_ACR_WRHIGHFREQ_Pos);
    H750_REG32(H750_FLASH_REG_BASE + FLASH_ACR) = reg;

    /* 10) 切换系统时钟到 PLL1 并等待生效 */
    reg = H750_REG32(rcc + RCC_CFGR);
    reg = (reg & ~RCC_CFGR_SW_Msk) | (RCC_CFGR_SW_PLL1 << RCC_CFGR_SW_Pos);
    H750_REG32(rcc + RCC_CFGR) = reg;
    {
        uint32_t guard = 0u;
        while ((((H750_REG32(rcc + RCC_CFGR) & RCC_CFGR_SWS_Msk) >> RCC_CFGR_SWS_Pos)
                != RCC_CFGR_SW_PLL1) && (guard < 100000u)) {
            guard++;
        }
    }

    /* 11) 外设时钟使能 */
    H750_REG32(rcc + RCC_AHB3ENR) |= RCC_AHB3ENR_FMCEN | RCC_AHB3ENR_QSPIEN;
    H750_REG32(rcc + RCC_AHB1ENR) |= RCC_AHB1ENR_SDMMC1EN | RCC_AHB1ENR_ETH1MACEN |
                                     RCC_AHB1ENR_ETH1TXEN | RCC_AHB1ENR_ETH1RXEN |
                                     RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMA2EN;
    H750_REG32(rcc + RCC_AHB4ENR) |= RCC_AHB4ENR_GPIOAEN | RCC_AHB4ENR_GPIOBEN |
                                     RCC_AHB4ENR_GPIOCEN | RCC_AHB4ENR_GPIODEN |
                                     RCC_AHB4ENR_GPIOEEN | RCC_AHB4ENR_GPIOFEN |
                                     RCC_AHB4ENR_GPIOGEN | RCC_AHB4ENR_BKPRAMEN;
    H750_REG32(rcc + RCC_APB3ENR) |= RCC_APB3ENR_LTDCEN;
    H750_REG32(rcc + RCC_APB1LENR) |= RCC_APB1LENR_TIM2EN | RCC_APB1LENR_I2C4EN;
    H750_REG32(rcc + RCC_APB1HENR) |= RCC_APB1HENR_FDCANEN;
    H750_REG32(rcc + RCC_APB2ENR) |= RCC_APB2ENR_USART1EN;

    return 0;
}

/* ---------------------------------------------------------------------------
 * 引脚与板级外设
 * ------------------------------------------------------------------------- */
void board_gpio_init(void)
{
    uint32_t i;
    for (i = 0u; i < h750_pin_table_size; i++) {
        const h750_pin_cfg_t *p = &h750_pin_table[i];
        hal_gpio_init(p->port, p->pin, p->mode, p->af, p->otype, p->ospeed, p->pupd);
    }
}

int board_pin_table_check(void)
{
    uint32_t i;
    uint32_t j;

    if (h750_pin_table_size == 0u) {
        return -1;
    }
    for (i = 0u; i < h750_pin_table_size; i++) {
        for (j = i + 1u; j < h750_pin_table_size; j++) {
            if ((h750_pin_table[i].port == h750_pin_table[j].port) &&
                (h750_pin_table[i].pin == h750_pin_table[j].pin)) {
                return -2;   /* 同一引脚被分配给两个功能 */
            }
        }
        if (h750_pin_table[i].pin > 15u) {
            return -3;
        }
        if (h750_pin_table[i].af > 15u) {
            return -4;
        }
    }
    return 0;
}

uint32_t board_pin_count_by_af(uint8_t af)
{
    uint32_t i;
    uint32_t n = 0u;
    for (i = 0u; i < h750_pin_table_size; i++) {
        if ((h750_pin_table[i].mode == GPIO_MODE_AF) && (h750_pin_table[i].af == af)) {
            n++;
        }
    }
    return n;
}

int board_init(void)
{
    int rc = board_clock_init();
    if (rc != 0) {
        return rc;
    }
    board_gpio_init();
    /* LCD 复位释放、触摸复位释放、指示灯默认熄灭 */
    hal_gpio_write(H750_LCD_RST_PORT, H750_LCD_RST_PIN, 1);
    hal_gpio_write(H750_TOUCH_RST_PORT, H750_TOUCH_RST_PIN, 1);
    hal_led_set(H750_LED_RUN_PORT, H750_LED_RUN_PIN, 0);
    hal_led_set(H750_LED_CAN1_PORT, H750_LED_CAN1_PIN, 0);
    hal_led_set(H750_LED_ERR_PORT, H750_LED_ERR_PIN, 0);
    return 0;
}
