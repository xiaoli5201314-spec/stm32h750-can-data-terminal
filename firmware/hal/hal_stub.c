/*
 * hal_stub.c
 * ---------------------------------------------------------------------------
 * 平台抽象层实现。同一个文件提供两套后端：
 *
 *   目标构建（未定义 H750_PC_SIM）
 *     - TIM2 自由运行计数器 + DWT 周期计数，提供 ms / us 时基
 *     - PRIMASK 临界区
 *     - GPIO / USART1 调试口 / 独立看门狗 / 软件复位
 *
 *   仿真构建（定义 H750_PC_SIM）
 *     - 寄存器文件数组：所有外设寄存器访问都被重定向到这里
 *     - 内存区域表：把片内 SRAM / SDRAM / 消息 RAM 映射到主机缓冲
 *     - 时间仿真、GPIO 注入
 *     - FDCAN 外设模型（过滤器路由、RX FIFO、TX 请求与完成、ECR/PSR）
 *     - 以太网 PHY（MDIO）模型
 *     - QSPI Flash 与 SD 卡的存储模型
 */
#include <string.h>
#include <stdlib.h>
#include "hal_stub.h"

/* ==========================================================================
 * 目标构建后端
 * ========================================================================== */
#ifndef H750_PC_SIM

void *hal_mem_ptr(uint32_t phys_addr, uint32_t size)
{
    (void)size;
    return (void *)(uintptr_t)phys_addr;
}

void hal_mem_region_register(uint32_t base, uint32_t size, void *host, uint32_t host_size)
{
    (void)base; (void)size; (void)host; (void)host_size;   /* 目标上无需映射 */
}

uint32_t hal_time_ms(void)
{
    /* TIM2 配置为 1MHz 自由计数（PSC = APB 时钟 / 1MHz - 1） */
    return (H750_REG32(H750_TIM2_BASE + TIM_CNT)) / 1000u;
}

uint32_t hal_time_us(void)
{
    return H750_REG32(H750_TIM2_BASE + TIM_CNT);
}

void hal_delay_us(uint32_t us)
{
    uint32_t start = hal_time_us();
    while ((hal_time_us() - start) < us) {
        /* 空转等待 */
    }
}

void hal_delay_ms(uint32_t ms)
{
    uint32_t start = hal_time_ms();
    while ((hal_time_ms() - start) < ms) {
        hal_watchdog_feed();
    }
}

uint32_t hal_enter_critical(void)
{
    uint32_t primask;
#if defined(__GNUC__)
    __asm volatile ("mrs %0, primask" : "=r" (primask));
    __asm volatile ("cpsid i" ::: "memory");
#else
    primask = 0u;
#endif
    return primask;
}

void hal_exit_critical(uint32_t state)
{
#if defined(__GNUC__)
    if ((state & 0x1u) == 0u) {
        __asm volatile ("cpsie i" ::: "memory");
    }
#else
    (void)state;
#endif
}

void hal_gpio_init(uint32_t port, uint32_t pin, uint32_t mode, uint32_t af,
                   uint32_t otype, uint32_t ospeed, uint32_t pupd)
{
    uint32_t shift2 = (pin & 15u) * 2u;
    uint32_t shift4 = (pin & 7u) * 4u;
    uint32_t reg;

    reg = H750_REG32(port + GPIO_MODER);
    reg &= ~(0x3u << shift2);
    reg |= ((mode & 0x3u) << shift2);
    H750_REG32(port + GPIO_MODER) = reg;

    reg = H750_REG32(port + GPIO_OTYPER);
    reg &= ~(0x1u << (pin & 15u));
    reg |= ((otype & 0x1u) << (pin & 15u));
    H750_REG32(port + GPIO_OTYPER) = reg;

    reg = H750_REG32(port + GPIO_OSPEEDR);
    reg &= ~(0x3u << shift2);
    reg |= ((ospeed & 0x3u) << shift2);
    H750_REG32(port + GPIO_OSPEEDR) = reg;

    reg = H750_REG32(port + GPIO_PUPDR);
    reg &= ~(0x3u << shift2);
    reg |= ((pupd & 0x3u) << shift2);
    H750_REG32(port + GPIO_PUPDR) = reg;

    if ((mode == GPIO_MODE_AF) && (pin < 16u)) {
        if (pin < 8u) {
            reg = H750_REG32(port + GPIO_AFRL);
            reg &= ~(0xFu << shift4);
            reg |= ((af & 0xFu) << shift4);
            H750_REG32(port + GPIO_AFRL) = reg;
        } else {
            reg = H750_REG32(port + GPIO_AFRH);
            reg &= ~(0xFu << shift4);
            reg |= ((af & 0xFu) << shift4);
            H750_REG32(port + GPIO_AFRH) = reg;
        }
    }
}

void hal_gpio_write(uint32_t port, uint32_t pin, int level)
{
    uint32_t bit = 1u << (pin & 15u);
    H750_REG32(port + GPIO_BSRR) = (level != 0) ? bit : (bit << 16);
}

int hal_gpio_read(uint32_t port, uint32_t pin)
{
    return ((H750_REG32(port + GPIO_IDR) >> (pin & 15u)) & 0x1u) ? 1 : 0;
}

void hal_led_set(uint32_t port, uint32_t pin, int on)
{
    hal_gpio_write(port, pin, on);
}

void hal_uart_putc(char c)
{
    while ((H750_REG32(H750_USART1_BASE + USART_ISR) & USART_ISR_TXE) == 0u) {
        /* 等待发送寄存器空 */
    }
    H750_REG32(H750_USART1_BASE + USART_TDR) = (uint32_t)(uint8_t)c;
}

void hal_uart_write(const char *s)
{
    while ((s != 0) && (*s != '\0')) {
        if (*s == '\n') {
            hal_uart_putc('\r');
        }
        hal_uart_putc(*s);
        s++;
    }
}

void hal_watchdog_feed(void)
{
    /* IWDG1：喂狗键值 0xAAAA */
    H750_REG32(0x58004800u) = 0x0000AAAAu;
}

void hal_reset(void)
{
    /* AIRCR：SYSRESETREQ */
    H750_REG32(0xE000ED0Cu) = (0x5FAu << 16) | (1u << 2);
    for (;;) {
        /* 等待复位 */
    }
}

#else  /* ==================== PC 仿真后端 ==================== */

/* --------------------------------------------------------------------------
 * 寄存器文件
 * -------------------------------------------------------------------------- */
volatile uint32_t h750_sim_regfile[H750_SIM_REGFILE_WORDS];

/* --------------------------------------------------------------------------
 * 内存区域映射
 * -------------------------------------------------------------------------- */
#define SIM_RAM_REGIONS 12u

typedef struct {
    uint32_t base;
    uint32_t size;
    uint8_t *host;
    uint32_t host_size;
    int      allocated;
} sim_ram_region_t;

static sim_ram_region_t s_ram[SIM_RAM_REGIONS];
static uint32_t         s_ram_count;

void hal_mem_region_register(uint32_t base, uint32_t size, void *host, uint32_t host_size)
{
    if ((s_ram_count >= SIM_RAM_REGIONS) || (host == 0)) {
        return;
    }
    s_ram[s_ram_count].base = base;
    s_ram[s_ram_count].size = size;
    s_ram[s_ram_count].host = (uint8_t *)host;
    s_ram[s_ram_count].host_size = (host_size != 0u) ? host_size : size;
    s_ram[s_ram_count].allocated = 0;
    s_ram_count++;
}

static void ram_region_alloc(uint32_t base, uint32_t size)
{
    uint8_t *buf;
    if (s_ram_count >= SIM_RAM_REGIONS) {
        return;
    }
    buf = (uint8_t *)malloc((size_t)size);
    if (buf == 0) {
        return;
    }
    (void)memset(buf, 0, (size_t)size);
    s_ram[s_ram_count].base = base;
    s_ram[s_ram_count].size = size;
    s_ram[s_ram_count].host = buf;
    s_ram[s_ram_count].host_size = size;
    s_ram[s_ram_count].allocated = 1;
    s_ram_count++;
}

void *hal_mem_ptr(uint32_t phys_addr, uint32_t size)
{
    uint32_t i;
    for (i = 0u; i < s_ram_count; i++) {
        uint32_t off;
        if ((phys_addr < s_ram[i].base)) {
            continue;
        }
        off = phys_addr - s_ram[i].base;
        if ((off + size) > s_ram[i].host_size) {
            continue;
        }
        return (void *)(s_ram[i].host + off);
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * 时间仿真与临界区
 * -------------------------------------------------------------------------- */
static uint32_t s_time_ms;
static uint32_t s_time_us_frac;

void hal_sim_advance_ms(uint32_t ms)
{
    s_time_ms += ms;
}

void hal_sim_advance_us(uint32_t us)
{
    s_time_us_frac += us;
    while (s_time_us_frac >= 1000u) {
        s_time_us_frac -= 1000u;
        s_time_ms++;
    }
}

uint32_t hal_sim_elapsed_ms(void)
{
    return s_time_ms;
}

uint32_t hal_time_ms(void)
{
    return s_time_ms;
}

uint32_t hal_time_us(void)
{
    return (s_time_ms * 1000u) + s_time_us_frac;
}

void hal_delay_ms(uint32_t ms)
{
    hal_sim_advance_ms(ms);
}

void hal_delay_us(uint32_t us)
{
    hal_sim_advance_us(us);
}

uint32_t hal_enter_critical(void)
{
    return 0u;
}

void hal_exit_critical(uint32_t state)
{
    (void)state;
}

/* --------------------------------------------------------------------------
 * GPIO 注入
 * -------------------------------------------------------------------------- */
static uint32_t s_gpio_in[8];

void hal_sim_gpio_set_input(uint32_t port, uint32_t pin, int level)
{
    uint32_t idx = (port - H750_GPIOA_BASE) / 0x400u;
    uint32_t bit = 1u << (pin & 15u);
    if (idx > 7u) {
        return;
    }
    if (level != 0) {
        s_gpio_in[idx] |= bit;
    } else {
        s_gpio_in[idx] &= ~bit;
    }
}

int hal_gpio_read(uint32_t port, uint32_t pin)
{
    uint32_t addr = port + GPIO_IDR;
    uint32_t idx = (port - H750_GPIOA_BASE) / 0x400u;
    uint32_t v = H750_REG32(addr);
    if (idx <= 7u) {
        v |= s_gpio_in[idx];
    }
    return ((v >> (pin & 15u)) & 0x1u) ? 1 : 0;
}

void hal_led_set(uint32_t port, uint32_t pin, int on)
{
    uint32_t bit = 1u << (pin & 15u);
    H750_REG32(port + GPIO_BSRR) = (on != 0) ? bit : (bit << 16);
}

void hal_gpio_write(uint32_t port, uint32_t pin, int level)
{
    hal_led_set(port, pin, level);
}

void hal_gpio_init(uint32_t port, uint32_t pin, uint32_t mode, uint32_t af,
                   uint32_t otype, uint32_t ospeed, uint32_t pupd)
{
    uint32_t shift2 = (pin & 15u) * 2u;
    uint32_t shift4 = (pin & 7u) * 4u;
    uint32_t reg;

    reg = H750_REG32(port + GPIO_MODER);
    reg &= ~(0x3u << shift2);
    reg |= ((mode & 0x3u) << shift2);
    H750_REG32(port + GPIO_MODER) = reg;

    reg = H750_REG32(port + GPIO_OTYPER);
    reg &= ~(0x1u << (pin & 15u));
    reg |= ((otype & 0x1u) << (pin & 15u));
    H750_REG32(port + GPIO_OTYPER) = reg;

    reg = H750_REG32(port + GPIO_OSPEEDR);
    reg &= ~(0x3u << shift2);
    reg |= ((ospeed & 0x3u) << shift2);
    H750_REG32(port + GPIO_OSPEEDR) = reg;

    reg = H750_REG32(port + GPIO_PUPDR);
    reg &= ~(0x3u << shift2);
    reg |= ((pupd & 0x3u) << shift2);
    H750_REG32(port + GPIO_PUPDR) = reg;

    if ((mode == GPIO_MODE_AF) && (pin < 16u)) {
        if (pin < 8u) {
            reg = H750_REG32(port + GPIO_AFRL);
            reg &= ~(0xFu << shift4);
            reg |= ((af & 0xFu) << shift4);
            H750_REG32(port + GPIO_AFRL) = reg;
        } else {
            reg = H750_REG32(port + GPIO_AFRH);
            reg &= ~(0xFu << shift4);
            reg |= ((af & 0xFu) << shift4);
            H750_REG32(port + GPIO_AFRH) = reg;
        }
    }
}

void hal_uart_putc(char c)
{
    (void)c;   /* 仿真下由测试框架直接输出到 stdout */
}

void hal_uart_write(const char *s)
{
    (void)s;
}

void hal_watchdog_feed(void)
{
}

void hal_reset(void)
{
    hal_sim_reset();
}

/* --------------------------------------------------------------------------
 * FDCAN 外设模型
 * -------------------------------------------------------------------------- */
typedef struct {
    int                    attached;
    fdcan_config_t         cfg;
    fdcan_msgram_layout_t  lay;
    uint32_t rx0_gi;
    uint32_t rx0_pi;
    uint32_t rx0_cnt;
    uint32_t rx1_gi;
    uint32_t rx1_pi;
    uint32_t rx1_cnt;
    uint32_t tx_pending;
    uint32_t tx_put;
    uint32_t tx_cnt;
    int      tx_stuck;
    uint32_t injected;
    uint32_t rejected;
    uint32_t tx_requests;
    uint32_t tx_completed;
    uint32_t tec;
    uint32_t rec;
    uint32_t cel;
    uint32_t config_delay;
    uint32_t config_faults;
    int      config_fault_once;
    uint32_t init_target;
    uint32_t init_reads;
    int      init_pending;
} sim_fdcan_t;

static sim_fdcan_t s_can[2];

static sim_fdcan_t *can_of(uint32_t base)
{
    if (base == H750_FDCAN1_BASE) {
        return &s_can[0];
    }
    if (base == H750_FDCAN2_BASE) {
        return &s_can[1];
    }
    return 0;
}

static uint32_t can_rx_capacity(const sim_fdcan_t *c, uint8_t fifo)
{
    return (fifo == 0u) ? c->lay.rx0_count : c->lay.rx1_count;
}

static void can_update_rx_status(sim_fdcan_t *c, uint8_t fifo)
{
    uint32_t v;
    if (fifo == 0u) {
        v = (c->rx0_cnt & 0x7Fu) |
            ((c->rx0_gi & 0x3Fu) << FDCAN_RXF0S_F0GI_Pos) |
            ((c->rx0_pi & 0x3Fu) << FDCAN_RXF0S_F0PI_Pos);
        if (c->rx0_cnt >= can_rx_capacity(c, 0u)) {
            v |= FDCAN_RXF0S_F0F;
        }
        H750_REG32(c->cfg.base + FDCAN_RXF0S) = v;
    } else {
        v = (c->rx1_cnt & 0x7Fu) |
            ((c->rx1_gi & 0x3Fu) << FDCAN_RXF1S_F1GI_Pos) |
            ((c->rx1_pi & 0x3Fu) << FDCAN_RXF1S_F1PI_Pos);
        if (c->rx1_cnt >= can_rx_capacity(c, 1u)) {
            v |= FDCAN_RXF1S_F1F;
        }
        H750_REG32(c->cfg.base + FDCAN_RXF1S) = v;
    }
}

void hal_sim_fdcan_reset(uint32_t base)
{
    sim_fdcan_t *c = can_of(base);
    if (c != 0) {
        (void)memset(c, 0, sizeof(*c));
    }
}

void hal_sim_fdcan_attach(uint32_t base, const fdcan_config_t *cfg,
                          const fdcan_msgram_layout_t *lay)
{
    sim_fdcan_t *c = can_of(base);
    if ((c == 0) || (cfg == 0) || (lay == 0)) {
        return;
    }
    (void)memset(c, 0, sizeof(*c));
    c->attached = 1;
    c->cfg = *cfg;
    c->lay = *lay;
    /* 上电默认 PSR：总线空闲、错误主动 */
    H750_REG32(base + FDCAN_PSR) = (2u << FDCAN_PSR_ACT_Pos);
    H750_REG32(base + FDCAN_ECR) = 0u;
    H750_REG32(base + FDCAN_TXFQS) = 0u;
    can_update_rx_status(c, 0u);
    can_update_rx_status(c, 1u);
}

static int can_config_fault(sim_fdcan_t *c, uint32_t fault)
{
    if ((c->config_faults & fault) == 0u) {
        return 0;
    }
    if (c->config_fault_once != 0) {
        c->config_faults &= ~fault;
    }
    return 1;
}

static void can_apply_cccr(uint32_t base, uint32_t value)
{
    if ((value & FDCAN_CCCR_INIT) == 0u) {
        value &= ~FDCAN_CCCR_CCE;
        H750_REG32(base + FDCAN_PSR) &= ~FDCAN_PSR_BO;
    }
    H750_REG32(base + FDCAN_CCCR) = value;
}

uint32_t hal_sim_fdcan_reg_read(uint32_t base, uint32_t offset)
{
    sim_fdcan_t *c = can_of(base);
    if ((c != 0) && (offset == FDCAN_CCCR) && (c->init_pending != 0)) {
        if (c->init_reads > 0u) {
            c->init_reads--;
        } else {
            can_apply_cccr(base, c->init_target);
            c->init_pending = 0;
        }
    }
    return H750_REG32(base + offset);
}

void hal_sim_fdcan_set_config_delay(uint32_t base, uint32_t reads)
{
    sim_fdcan_t *c = can_of(base);
    if (c != 0) {
        c->config_delay = reads;
    }
}

void hal_sim_fdcan_set_config_faults(uint32_t base, uint32_t faults, int once)
{
    sim_fdcan_t *c = can_of(base);
    if (c != 0) {
        c->config_faults = faults;
        c->config_fault_once = once;
    }
}

void hal_sim_fdcan_reg_write(uint32_t base, uint32_t offset, uint32_t value)
{
    sim_fdcan_t *c = can_of(base);
    uint32_t old = H750_REG32(base + offset);
    uint32_t access = FDCAN_CCCR_INIT | FDCAN_CCCR_CCE;

    if ((c == 0) || (c->attached == 0)) {
        H750_REG32(base + offset) = value;
        return;
    }
    if (offset == FDCAN_CCCR) {
        uint32_t target = old;
        c->init_pending = 0; /* A new request supersedes a delayed handshake. */
        if ((old & access) == access) {
            target = (target & access) | (value & ~access);
        }
        target &= ~FDCAN_CCCR_CCE;
        if (((old & FDCAN_CCCR_INIT) != 0u) &&
            ((value & FDCAN_CCCR_CCE) != 0u) &&
            !can_config_fault(c, HAL_SIM_FDCAN_FAIL_CCE)) {
            target |= FDCAN_CCCR_CCE;
        }
        if (((old ^ value) & FDCAN_CCCR_INIT) != 0u) {
            uint32_t fault = (value & FDCAN_CCCR_INIT) ?
                             HAL_SIM_FDCAN_FAIL_INIT : HAL_SIM_FDCAN_FAIL_START;
            if (!can_config_fault(c, fault)) {
                c->init_target = (target & ~FDCAN_CCCR_INIT) |
                                 (value & FDCAN_CCCR_INIT);
                c->init_reads = c->config_delay;
                c->init_pending = 1;
            }
        }
        H750_REG32(base + offset) = target;
        if ((c->init_pending != 0) && (c->init_reads == 0u)) {
            can_apply_cccr(base, c->init_target);
            c->init_pending = 0;
        }
        return;
    }
    if (offset == FDCAN_GFC) {
        if ((H750_REG32(base + FDCAN_CCCR) & access) != access ||
            can_config_fault(c, HAL_SIM_FDCAN_FAIL_GFC)) {
            return;
        }
    }
    H750_REG32(base + offset) = value;

    switch (offset) {
    case FDCAN_RXF0A: {
        uint32_t gi = value & 0x3Fu;
        if (c->rx0_cnt > 0u) {
            c->rx0_cnt--;
            c->rx0_gi = (gi + 1u) % can_rx_capacity(c, 0u);
        }
        can_update_rx_status(c, 0u);
        break;
    }

    case FDCAN_RXF1A: {
        uint32_t gi = value & 0x3Fu;
        if (c->rx1_cnt > 0u) {
            c->rx1_cnt--;
            c->rx1_gi = (gi + 1u) % can_rx_capacity(c, 1u);
        }
        can_update_rx_status(c, 1u);
        break;
    }

    case FDCAN_TXBAR: {
        uint32_t bit;
        for (bit = 0u; bit < 32u; bit++) {
            if ((value & (1u << bit)) == 0u) {
                continue;
            }
            c->tx_requests++;
            c->tx_pending |= (1u << bit);
            if (c->tx_stuck == 0) {
                c->tx_pending &= ~(1u << bit);
                H750_REG32(base + FDCAN_TXBTO) |= (1u << bit);
                c->tx_completed++;
                c->tx_put = (c->tx_put + 1u) % c->lay.txbuf_count;
                if (c->tx_cnt < c->lay.txbuf_count) {
                    c->tx_cnt++;
                }
                /* 空闲时置发送完成中断标志 */
                H750_REG32(base + FDCAN_IR) |= FDCAN_IR_TC;
            }
        }
        H750_REG32(base + FDCAN_TXBRP) = c->tx_pending;
        H750_REG32(base + FDCAN_TXFQS) =
            (c->tx_cnt & FDCAN_TXFQS_TFFL_Msk) |
            ((c->tx_put & 0x1Fu) << FDCAN_TXFQS_TFQPI_Pos);
        break;
    }

    case FDCAN_TXBCR:
        c->tx_pending &= ~value;
        H750_REG32(base + FDCAN_TXBTO) &= ~value;
        H750_REG32(base + FDCAN_TXBRP) = c->tx_pending;
        H750_REG32(base + FDCAN_TXBCF) |= value;
        if (c->tx_cnt > 0u) {
            c->tx_cnt--;
        }
        break;

    case FDCAN_ECR:
        c->tec = value & FDCAN_ECR_TEC_Msk;
        c->rec = (value & FDCAN_ECR_REC_Msk) >> FDCAN_ECR_REC_Pos;
        c->cel = (value & FDCAN_ECR_CEL_Msk) >> FDCAN_ECR_CEL_Pos;
        break;

    default:
        break;
    }
}

void hal_sim_fdcan_set_ecr(uint32_t base, uint32_t tec, uint32_t rec, uint32_t cel)
{
    sim_fdcan_t *c = can_of(base);
    if (c == 0) {
        return;
    }
    c->tec = tec & 0xFFu;
    c->rec = rec & 0x7Fu;
    c->cel = cel & 0xFFu;
    H750_REG32(base + FDCAN_ECR) =
        (c->tec & FDCAN_ECR_TEC_Msk) |
        ((c->rec << FDCAN_ECR_REC_Pos) & FDCAN_ECR_REC_Msk) |
        ((c->cel << FDCAN_ECR_CEL_Pos) & FDCAN_ECR_CEL_Msk);
}

void hal_sim_fdcan_set_psr(uint32_t base, uint32_t psr)
{
    H750_REG32(base + FDCAN_PSR) = psr;
}

void hal_sim_fdcan_set_tx_stuck(uint32_t base, int stuck)
{
    sim_fdcan_t *c = can_of(base);
    if (c != 0) {
        c->tx_stuck = stuck;
    }
}

void hal_sim_fdcan_complete_tx(uint32_t base, int all)
{
    sim_fdcan_t *c = can_of(base);
    if (c == 0) {
        return;
    }
    if (all != 0) {
        c->tx_pending = 0u;
    } else if (c->tx_pending != 0u) {
        uint32_t lowest = c->tx_pending & (~c->tx_pending + 1u);
        c->tx_pending &= ~lowest;
        H750_REG32(base + FDCAN_TXBTO) |= lowest;
        c->tx_completed++;
    }
    H750_REG32(base + FDCAN_TXBRP) = c->tx_pending;
    H750_REG32(base + FDCAN_IR) |= FDCAN_IR_TC;
}

uint32_t hal_sim_fdcan_tx_requests(uint32_t base)
{
    sim_fdcan_t *c = can_of(base);
    return (c == 0) ? 0u : c->tx_requests;
}

uint32_t hal_sim_fdcan_injected(uint32_t base)
{
    sim_fdcan_t *c = can_of(base);
    return (c == 0) ? 0u : c->injected;
}

uint32_t hal_sim_fdcan_rejected(uint32_t base)
{
    sim_fdcan_t *c = can_of(base);
    return (c == 0) ? 0u : c->rejected;
}

uint32_t hal_sim_fdcan_tx_completed(uint32_t base)
{
    sim_fdcan_t *c = can_of(base);
    return (c == 0) ? 0u : c->tx_completed;
}

int hal_sim_fdcan_inject(uint32_t base, const fdcan_frame_t *f)
{
    sim_fdcan_t *c = can_of(base);
    volatile uint32_t *mram;
    uint32_t route;
    uint8_t hp = 0u;
    uint8_t fifo;
    uint32_t idx;
    uint32_t w0;
    uint32_t w1;
    uint32_t bytes;
    uint32_t words;
    uint32_t i;
    int fidx;
    fdcan_filter_config_t filters;
    uint32_t gfc;

    if ((c == 0) || (c->attached == 0) || (f == 0)) {
        return -2;
    }
    filters = *c->cfg.filters;
    gfc = H750_REG32(base + FDCAN_GFC);
    filters.anfs = (uint8_t)((gfc & FDCAN_GFC_ANFS_Msk) >> FDCAN_GFC_ANFS_Pos);
    filters.anfe = (uint8_t)((gfc & FDCAN_GFC_ANFE_Msk) >> FDCAN_GFC_ANFE_Pos);
    route = (uint32_t)fdcan_filter_route(&filters, f->can_id, f->xtd, &hp);
    if (route == (uint32_t)FDCAN_ROUTE_REJECT) {
        c->rejected++;
        return -1;
    }
    fifo = (route == (uint32_t)FDCAN_ROUTE_FIFO1) ? 1u : 0u;

    if ((fifo == 0u) ? (c->rx0_cnt >= c->lay.rx0_count)
                     : (c->rx1_cnt >= c->lay.rx1_count)) {
        c->rejected++;   /* FIFO 满：硬件会置 RF0L/RF1L 并丢弃 */
        return -1;
    }

    mram = (volatile uint32_t *)hal_mem_ptr(c->cfg.msgram_base, c->lay.total_words * 4u);
    if (mram == 0) {
        return -2;
    }

    fidx = fdcan_filter_match_index(c->cfg.filters, f->can_id, f->xtd, &hp);
    idx = (fifo == 0u)
        ? (c->lay.rx0_words + (c->rx0_pi * c->lay.elem_words))
        : (c->lay.rx1_words + (c->rx1_pi * c->lay.elem_words));

    w0 = f->can_id & FDCAN_RX_W0_ID_Msk;
    if (f->xtd != 0u) { w0 |= FDCAN_RX_W0_XTD; }
    if (f->rtr != 0u) { w0 |= FDCAN_RX_W0_RTR; }
    if (f->esi != 0u) { w0 |= FDCAN_RX_W0_ESI; }

    w1 = ((uint32_t)f->dlc << FDCAN_RX_W1_DLC_Pos) & FDCAN_RX_W1_DLC_Msk;
    w1 |= ((uint32_t)((fidx < 0) ? 1u : 0u) << 31);            /* ANMF */
    w1 |= (((uint32_t)((fidx < 0) ? 0u : (uint32_t)fidx) & 0x7Fu) << FDCAN_RX_W1_FIDX_Pos);
    if (f->fdf != 0u) { w1 |= FDCAN_RX_W1_FDF; }
    if (f->brs != 0u) { w1 |= FDCAN_RX_W1_BRS; }
    w1 |= (uint32_t)(f->rx_ts & FDCAN_RX_W1_RXTS_Msk);

    mram[idx]      = w0;
    mram[idx + 1u] = w1;

    bytes = fdcan_frame_payload_bytes(f);
    words = FDCAN_DATA_BYTES_TO_WORDS(bytes);
    for (i = 0u; i < words; i++) {
        uint32_t off = i * 4u;
        uint32_t w = 0u;
        uint32_t k;
        for (k = 0u; k < 4u; k++) {
            if ((off + k) < bytes) {
                w |= ((uint32_t)f->data[off + k]) << (8u * k);
            }
        }
        mram[idx + 2u + i] = w;
    }

    if (fifo == 0u) {
        c->rx0_cnt++;
        c->rx0_pi = (c->rx0_pi + 1u) % c->lay.rx0_count;
        H750_REG32(base + FDCAN_IR) |= FDCAN_IR_RF0N;
    } else {
        c->rx1_cnt++;
        c->rx1_pi = (c->rx1_pi + 1u) % c->lay.rx1_count;
        H750_REG32(base + FDCAN_IR) |= FDCAN_IR_RF1N;
    }
    can_update_rx_status(c, fifo);
    c->injected++;
    return (int)fifo;
}

/* --------------------------------------------------------------------------
 * 以太网 PHY（MDIO）模型
 * -------------------------------------------------------------------------- */
#define SIM_PHY_COUNT   4u
#define SIM_PHY_REGS    32u

static uint16_t s_phy[SIM_PHY_COUNT][SIM_PHY_REGS];
static uint32_t s_mdio_reads;
static uint32_t s_mdio_writes;

void hal_sim_phy_set(uint32_t phy_addr, uint32_t reg, uint16_t value)
{
    if ((phy_addr < SIM_PHY_COUNT) && (reg < SIM_PHY_REGS)) {
        s_phy[phy_addr][reg] = value;
    }
}

uint16_t hal_sim_phy_get(uint32_t phy_addr, uint32_t reg)
{
    if ((phy_addr < SIM_PHY_COUNT) && (reg < SIM_PHY_REGS)) {
        return s_phy[phy_addr][reg];
    }
    return 0u;
}

uint32_t hal_sim_mdio_reads(void)
{
    return s_mdio_reads;
}

uint32_t hal_sim_mdio_writes(void)
{
    return s_mdio_writes;
}

static void phy_defaults(void)
{
    uint32_t i;
    for (i = 0u; i < SIM_PHY_COUNT; i++) {
        (void)memset(s_phy[i], 0, sizeof(s_phy[i]));
    }
    /* 默认：自协商完成、100Mbps 全双工、链路已建立（LAN8720A 行为） */
    s_phy[0][H750_ETH_PHY_BCR] = 0x3100u;
    s_phy[0][H750_ETH_PHY_BSR] = 0x786Du;
    s_phy[0][H750_ETH_PHY_ID1] = H750_ETH_PHY_ID1_VALUE;
    s_phy[0][H750_ETH_PHY_ID2] = H750_ETH_PHY_ID2_VALUE;
    s_phy[0][H750_ETH_PHY_SPECIAL] = 0x0000u;
}

void hal_sim_eth_set_link(int up)
{
    if (up != 0) {
        s_phy[0][H750_ETH_PHY_BSR] |= 0x0024u;   /* Link up + Auto-Neg Complete */
    } else {
        s_phy[0][H750_ETH_PHY_BSR] &= (uint16_t)~0x0004u;
    }
}

void hal_sim_mac_reg_write(uint32_t mac_base, uint32_t offset, uint32_t value)
{
    (void)mac_base;
    if (offset == ETH_MACMDIOAR) {
        uint32_t goc = (value & ETH_MACMDIOAR_GOC_Msk) >> ETH_MACMDIOAR_GOC_Pos;
        uint32_t pa = (value & ETH_MACMDIOAR_PA_Msk) >> ETH_MACMDIOAR_PA_Pos;
        uint32_t rda = (value & ETH_MACMDIOAR_RDA_Msk) >> ETH_MACMDIOAR_RDA_Pos;
        uint32_t clr_mb = value & ~ETH_MACMDIOAR_MB;

        if ((value & ETH_MACMDIOAR_MB) != 0u) {
            if (goc == ETH_MACMDIOAR_GOC_WRITE) {
                uint16_t data = (uint16_t)(H750_REG32(mac_base + ETH_MACMDIODR) & 0xFFFFu);
                if ((pa < SIM_PHY_COUNT) && (rda < SIM_PHY_REGS)) {
                    s_phy[pa][rda] = data;
                }
                s_mdio_writes++;
            } else if (goc == ETH_MACMDIOAR_GOC_READ) {
                uint16_t data = ((pa < SIM_PHY_COUNT) && (rda < SIM_PHY_REGS))
                                ? s_phy[pa][rda] : 0u;
                H750_REG32(mac_base + ETH_MACMDIODR) = ((uint32_t)data) | ETH_MACMDIODR_RA;
                s_mdio_reads++;
            }
            H750_REG32(mac_base + ETH_MACMDIOAR) = clr_mb;   /* MB 自动清零 */
        }
    }
}

/* --------------------------------------------------------------------------
 * QSPI Flash 存储模型
 * -------------------------------------------------------------------------- */
static uint8_t  s_flash[8u * 1024u * 1024u];
static uint32_t s_flash_erase_count;
static uint32_t s_flash_program_count;

void hal_sim_flash_reset(void)
{
    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    s_flash_erase_count = 0u;
    s_flash_program_count = 0u;
}

uint32_t hal_sim_flash_size(void)
{
    return (uint32_t)sizeof(s_flash);
}

int hal_sim_flash_read(uint32_t addr, void *dst, uint32_t len)
{
    if (((uint64_t)addr + len) > (uint64_t)sizeof(s_flash)) {
        return -1;
    }
    (void)memcpy(dst, &s_flash[addr], len);
    return 0;
}

void hal_sim_flash_preset(uint32_t addr, const void *src, uint32_t len)
{
    if (((uint64_t)addr + len) <= (uint64_t)sizeof(s_flash)) {
        (void)memcpy(&s_flash[addr], src, len);
    }
}

int hal_sim_flash_program(uint32_t addr, const void *src, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)src;
    uint32_t i;

    if (((uint64_t)addr + len) > (uint64_t)sizeof(s_flash)) {
        return -1;
    }
    /* NOR Flash 语义：只能把 1 写成 0，不能写 1 */
    for (i = 0u; i < len; i++) {
        s_flash[addr + i] = (uint8_t)(s_flash[addr + i] & p[i]);
    }
    s_flash_program_count++;
    return 0;
}

int hal_sim_flash_erase_sector(uint32_t addr)
{
    if (((uint64_t)addr + H750_QSPI_SECTOR_SIZE) > (uint64_t)sizeof(s_flash)) {
        return -1;
    }
    (void)memset(&s_flash[addr], 0xFF, H750_QSPI_SECTOR_SIZE);
    s_flash_erase_count++;
    return 0;
}

int hal_sim_flash_erase_block(uint32_t addr)
{
    if (((uint64_t)addr + H750_QSPI_BLOCK_SIZE) > (uint64_t)sizeof(s_flash)) {
        return -1;
    }
    (void)memset(&s_flash[addr], 0xFF, H750_QSPI_BLOCK_SIZE);
    s_flash_erase_count++;
    return 0;
}

int hal_sim_flash_erase_chip(void)
{
    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    s_flash_erase_count++;
    return 0;
}

uint32_t hal_sim_flash_erase_count(void)
{
    return s_flash_erase_count;
}

uint32_t hal_sim_flash_program_count(void)
{
    return s_flash_program_count;
}

/* --------------------------------------------------------------------------
 * SD 卡（块设备）模型
 * -------------------------------------------------------------------------- */
static uint8_t  s_sd[H750_SD_BLOCK_COUNT * H750_SD_SECTOR_SIZE];
static int      s_sd_present = 1;
static uint32_t s_sd_writes;
static int      s_sd_fail_after = -1;

void hal_sim_sd_reset(void)
{
    (void)memset(s_sd, 0, sizeof(s_sd));
    s_sd_present = 1;
    s_sd_writes = 0u;
    s_sd_fail_after = -1;
}

void hal_sim_sd_set_present(int present)
{
    s_sd_present = present;
}

int hal_sim_sd_present(void)
{
    return s_sd_present;
}

int hal_sim_sd_read_sector(uint32_t sector, void *dst)
{
    if ((s_sd_present == 0) || (sector >= H750_SD_BLOCK_COUNT)) {
        return -1;
    }
    (void)memcpy(dst, &s_sd[(size_t)sector * H750_SD_SECTOR_SIZE], H750_SD_SECTOR_SIZE);
    return 0;
}

int hal_sim_sd_write_sector(uint32_t sector, const void *src)
{
    if ((s_sd_present == 0) || (sector >= H750_SD_BLOCK_COUNT)) {
        return -1;
    }
    if ((s_sd_fail_after >= 0) && (s_sd_writes >= (uint32_t)s_sd_fail_after)) {
        return -2;   /* 模拟掉电/写失败 */
    }
    (void)memcpy(&s_sd[(size_t)sector * H750_SD_SECTOR_SIZE], src, H750_SD_SECTOR_SIZE);
    s_sd_writes++;
    return 0;
}

void hal_sim_sd_set_write_fail(int fail_after_writes)
{
    s_sd_fail_after = fail_after_writes;
}

uint32_t hal_sim_sd_write_count(void)
{
    return s_sd_writes;
}

uint32_t hal_sim_sd_capacity_sectors(void)
{
    return H750_SD_BLOCK_COUNT;
}

int hal_sim_sd_peek(uint32_t byte_offset, void *dst, uint32_t len)
{
    if (((uint64_t)byte_offset + len) > (uint64_t)sizeof(s_sd)) {
        return -1;
    }
    (void)memcpy(dst, &s_sd[byte_offset], len);
    return 0;
}

/* --------------------------------------------------------------------------
 * 全局仿真复位
 * -------------------------------------------------------------------------- */
void hal_sim_reset(void)
{
    uint32_t i;

    (void)memset((void *)h750_sim_regfile, 0, sizeof(h750_sim_regfile));
    (void)memset(s_gpio_in, 0, sizeof(s_gpio_in));

    for (i = 0u; i < s_ram_count; i++) {
        if (s_ram[i].allocated != 0) {
            free(s_ram[i].host);
        }
    }
    s_ram_count = 0u;

    s_time_ms = 0u;
    s_time_us_frac = 0u;

    /* 片内 SRAM */
    ram_region_alloc(H750_DTCM_BASE, H750_DTCM_SIZE);
    ram_region_alloc(H750_AXI_SRAM_BASE, H750_AXI_SRAM_SIZE);
    ram_region_alloc(H750_SRAM1_BASE, H750_SRAM1_SIZE + H750_SRAM2_SIZE);
    ram_region_alloc(H750_SRAM3_BASE, H750_SRAM3_SIZE);
    ram_region_alloc(H750_SRAM4_BASE, H750_SRAM4_SIZE);
    ram_region_alloc(H750_BKPSRAM_BASE, H750_BKPSRAM_SIZE);
    /* FDCAN1/FDCAN2 各自的 2560 word 消息 RAM */
    ram_region_alloc(H750_FDCAN_MSGRAM_BASE, H750_FDCAN_MSGRAM_WORDS * 4u * 2u);
    /* SDRAM：16MB 可缓存区 + 1MB 非缓存窗口，连续映射 */
    ram_region_alloc(H750_SDRAM_BASE, 16u * 1024u * 1024u + H750_SDRAM_NC_SIZE);

    /* 上电默认 MPU/寄存器状态：Cortex-M7 有 16 个数据区域 */
    H750_REG32(H750_MPU_TYPE) = (16u << MPU_TYPE_DREGION_Pos);

    hal_sim_fdcan_reset(H750_FDCAN1_BASE);
    hal_sim_fdcan_reset(H750_FDCAN2_BASE);
    phy_defaults();
    s_mdio_reads = 0u;
    s_mdio_writes = 0u;
    hal_sim_flash_reset();
    hal_sim_sd_reset();
}

#endif /* H750_PC_SIM */
