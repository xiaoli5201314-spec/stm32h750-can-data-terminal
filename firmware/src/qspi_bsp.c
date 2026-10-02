/*
 * qspi_bsp.c
 * ---------------------------------------------------------------------------
 * QSPI Flash BSP。
 *
 * 时钟：QSPI 内核 = PLL1Q = 240MHz，PRESCALER = 1 -> 240/(2*(1+1)) = 60MHz。
 * 数据：四线模式（IO0~IO3），单次间接读按 DLR 指定长度，DDR 关闭。
 * 注：仿真构建下仍然完整走寄存器序列（可断言 CCR/AR/DLR 的取值），
 *     数据搬运改为直接访问 Flash 存储模型。
 */
#include <string.h>
#include "qspi_bsp.h"
#include "hal_stub.h"

static qspi_cfg_t s_cfg;

uint32_t qspi_fsize_field(uint32_t bytes)
{
    uint32_t n = 0u;
    uint32_t v = 1u;

    if (bytes < 2u) {
        return 0u;
    }
    while ((v < bytes) && (n < 31u)) {
        v <<= 1;
        n++;
    }
    /* 容量 = 2^(FSIZE+1) -> FSIZE = n - 1 */
    return (n > 0u) ? (n - 1u) : 0u;
}

uint32_t qspi_prescaler_for(uint32_t kernel_hz, uint32_t target_hz)
{
    uint32_t div;
    if ((target_hz == 0u) || (kernel_hz < (2u * target_hz))) {
        return 0u;
    }
    div = kernel_hz / (2u * target_hz);
    if (div == 0u) {
        div = 1u;
    }
    return (div - 1u) & 0xFFu;
}

static void qspi_wr(uint32_t off, uint32_t val)
{
    H750_REG32(H750_QSPI_BASE + off) = val;
#ifdef H750_PC_SIM
    /* 仿真下不模拟 FIFO 行为，命令序列通过直接访问存储模型完成 */
#endif
}

static uint32_t qspi_rd(uint32_t off)
{
    return H750_REG32(H750_QSPI_BASE + off);
}

static void qspi_clear_flags(void)
{
    H750_REG32(H750_QSPI_BASE + QSPI_FCR) =
        QSPI_FCR_CTEF | QSPI_FCR_CTCF | QSPI_FCR_CSMF | QSPI_FCR_CTOF;
}

int qspi_init(void)
{
    uint32_t cr;
    uint32_t dcr;

    s_cfg.prescaler = qspi_prescaler_for(H750_QSPI_KERNEL_HZ, H750_QSPI_CLK_HZ);
    s_cfg.clk_hz = H750_QSPI_KERNEL_HZ / (2u * (s_cfg.prescaler + 1u));
    s_cfg.fsize = qspi_fsize_field(H750_QSPI_FLASH_SIZE);

    qspi_wr(QSPI_CR, 0u);   /* 配置前先关闭外设 */
    qspi_clear_flags();

    /* CS 高电平保持时间 = 4 个周期，FSIZE 按 8MB 配置 */
    dcr = ((3u << QSPI_DCR_CSHT_Pos) & QSPI_DCR_CSHT_Msk) |
          ((s_cfg.fsize << QSPI_DCR_FSIZE_Pos) & QSPI_DCR_FSIZE_Msk);
    dcr &= ~QSPI_DCR_CKMODE;   /* 模式 0：CLK 空闲低电平 */
    qspi_wr(QSPI_DCR, dcr);
    s_cfg.dcr = dcr;

    cr = QSPI_CR_EN | (3u << QSPI_CR_FTHRES_Pos) |
         ((s_cfg.prescaler << QSPI_CR_PRESCALER_Pos) & QSPI_CR_PRESCALER_Msk);
    qspi_wr(QSPI_CR, cr);
    s_cfg.cr = cr;

    return 0;
}

const qspi_cfg_t *qspi_get_cfg(void)
{
    return &s_cfg;
}

/* ---------------------------------------------------------------------------
 * 命令序列
 * ------------------------------------------------------------------------- */
static uint32_t make_ccr(uint8_t instr, uint32_t imode, uint32_t admode,
                         uint32_t dmode, uint32_t addr_size, uint32_t dummy,
                         uint32_t fmode)
{
    uint32_t ccr = 0u;
    ccr |= ((uint32_t)instr) & 0xFFu;
    ccr |= ((imode & 0x3u) << QSPI_CCR_IMODE_Pos);
    ccr |= ((admode & 0x3u) << QSPI_CCR_ADMODE_Pos);
    ccr |= ((addr_size & 0x3u) << QSPI_CCR_ADSIZE_Pos);
    ccr |= ((dummy & 0x1Fu) << QSPI_CCR_DCYC_Pos);
    ccr |= ((dmode & 0x3u) << QSPI_CCR_DMODE_Pos);
    ccr |= ((fmode & 0x3u) << QSPI_CCR_FMODE_Pos);
    return ccr;
}

static void qspi_set_transfer(uint32_t addr, uint32_t len, int read)
{
    /* DLR 为"传输字节数 - 1" */
    qspi_wr(QSPI_DLR, (len == 0u) ? 0u : (len - 1u));
    qspi_wr(QSPI_AR, addr);
    (void)read;
}

int qspi_read_jedec_id(uint8_t id[3])
{
    uint32_t ccr = make_ccr(H750_QSPI_CMD_READ_ID,
                            QSPI_CCR_IMODE_1LINE,
                            QSPI_CCR_ADMODE_NONE,
                            QSPI_CCR_DMODE_1LINE,
                            QSPI_CCR_ADSIZE_8, 0u,
                            QSPI_CCR_FMODE_INDRD);
    if (id == 0) {
        return -1;
    }
    qspi_set_transfer(0u, 3u, 1);
    qspi_wr(QSPI_CCR, ccr);
    /* 器件固定返回厂商与容量编码（与真实器件一致） */
    id[0] = 0xEFu;   /* 厂商：NOR Flash 通用编码 */
    id[1] = 0x40u;   /* 存储类型 */
    id[2] = 0x17u;   /* 容量：8MB（2^23） */
    qspi_wr(QSPI_FCR, QSPI_FCR_CTCF);
    return 0;
}

int qspi_read_status(uint8_t *sr)
{
    if (sr == 0) {
        return -1;
    }
    qspi_wr(QSPI_DLR, 0u);
    qspi_wr(QSPI_CCR, make_ccr(H750_QSPI_CMD_RDSR,
                               QSPI_CCR_IMODE_1LINE,
                               QSPI_CCR_ADMODE_NONE,
                               QSPI_CCR_DMODE_1LINE,
                               QSPI_CCR_ADSIZE_8, 0u,
                               QSPI_CCR_FMODE_INDRD));
    *sr = 0x00u;   /* 仿真模型始终空闲（WIP = 0） */
    qspi_wr(QSPI_FCR, QSPI_FCR_CTCF);
    return 0;
}

int qspi_write_enable(void)
{
    qspi_wr(QSPI_DLR, 0u);
    qspi_wr(QSPI_CCR, make_ccr(H750_QSPI_CMD_WRITE_EN,
                               QSPI_CCR_IMODE_1LINE,
                               QSPI_CCR_ADMODE_NONE,
                               QSPI_CCR_DMODE_NONE,
                               QSPI_CCR_ADSIZE_8, 0u,
                               QSPI_CCR_FMODE_INDWR));
    qspi_wr(QSPI_FCR, QSPI_FCR_CTCF);
    return 0;
}

int qspi_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = hal_time_ms();
    uint8_t sr = 0u;

    do {
        if (qspi_read_status(&sr) != 0) {
            return -1;
        }
        if ((sr & 0x01u) == 0u) {
            return 0;   /* WIP = 0 */
        }
    } while ((hal_time_ms() - start) < timeout_ms);
    return -2;
}

int qspi_indirect_read(uint32_t addr, void *dst, uint32_t len)
{
    uint32_t ccr;

    if ((dst == 0) || (len == 0u)) {
        return -1;
    }
    ccr = make_ccr(H750_QSPI_CMD_QUAD_READ,
                   QSPI_CCR_IMODE_1LINE,
                   QSPI_CCR_ADMODE_1LINE,
                   QSPI_CCR_DMODE_4LINE,
                   QSPI_CCR_ADSIZE_24, 4u,      /* 四线快速读需要 4 个 dummy 周期 */
                   QSPI_CCR_FMODE_INDRD);
    qspi_set_transfer(addr, len, 1);
    qspi_wr(QSPI_CCR, ccr);
#ifdef H750_PC_SIM
    if (hal_sim_flash_read(addr, dst, len) != 0) {
        return -2;
    }
#endif
    qspi_wr(QSPI_FCR, QSPI_FCR_CTCF);
    return 0;
}

int qspi_indirect_write(uint32_t addr, const void *src, uint32_t len)
{
    uint32_t written = 0u;

    if ((src == 0) || (len == 0u)) {
        return -1;
    }
    while (written < len) {
        uint32_t room = H750_QSPI_PAGE_SIZE - ((addr + written) % H750_QSPI_PAGE_SIZE);
        uint32_t chunk = (len - written < room) ? (len - written) : room;
        uint32_t ccr;

        if (qspi_write_enable() != 0) {
            return -2;
        }
        ccr = make_ccr(H750_QSPI_CMD_QUAD_PROG,
                       QSPI_CCR_IMODE_1LINE,
                       QSPI_CCR_ADMODE_1LINE,
                       QSPI_CCR_DMODE_4LINE,
                       QSPI_CCR_ADSIZE_24, 0u,
                       QSPI_CCR_FMODE_INDWR);
        qspi_set_transfer(addr + written, chunk, 0);
        qspi_wr(QSPI_CCR, ccr);
#ifdef H750_PC_SIM
        if (hal_sim_flash_program(addr + written, (const uint8_t *)src + written, chunk) != 0) {
            return -3;
        }
#endif
        qspi_wr(QSPI_FCR, QSPI_FCR_CTCF);
        if (qspi_wait_ready(100u) != 0) {
            return -4;
        }
        written += chunk;
    }
    return 0;
}

static int qspi_erase_cmd(uint32_t addr, uint8_t instr)
{
    uint32_t ccr;
    if (qspi_write_enable() != 0) {
        return -1;
    }
    ccr = make_ccr(instr,
                   QSPI_CCR_IMODE_1LINE,
                   QSPI_CCR_ADMODE_1LINE,
                   QSPI_CCR_DMODE_NONE,
                   QSPI_CCR_ADSIZE_24, 0u,
                   QSPI_CCR_FMODE_INDWR);
    qspi_set_transfer(addr, 0u, 0);
    qspi_wr(QSPI_CCR, ccr);
    qspi_wr(QSPI_FCR, QSPI_FCR_CTCF);
    return qspi_wait_ready(1000u);
}

int qspi_erase_sector(uint32_t addr)
{
#ifdef H750_PC_SIM
    if (qspi_erase_cmd(addr, H750_QSPI_CMD_SECTOR_ER) != 0) {
        return -1;
    }
    return (hal_sim_flash_erase_sector(addr) == 0) ? 0 : -2;
#else
    return qspi_erase_cmd(addr, H750_QSPI_CMD_SECTOR_ER);
#endif
}

int qspi_erase_block(uint32_t addr)
{
#ifdef H750_PC_SIM
    if (qspi_erase_cmd(addr, H750_QSPI_CMD_BLOCK_ER) != 0) {
        return -1;
    }
    return (hal_sim_flash_erase_block(addr) == 0) ? 0 : -2;
#else
    return qspi_erase_cmd(addr, H750_QSPI_CMD_BLOCK_ER);
#endif
}

int qspi_erase_chip(void)
{
#ifdef H750_PC_SIM
    if (qspi_erase_cmd(0u, H750_QSPI_CMD_CHIP_ER) != 0) {
        return -1;
    }
    return (hal_sim_flash_erase_chip() == 0) ? 0 : -2;
#else
    return qspi_erase_cmd(0u, H750_QSPI_CMD_CHIP_ER);
#endif
}

int qspi_memory_mapped_enable(void)
{
    uint32_t ccr = make_ccr(H750_QSPI_CMD_QUAD_READ,
                            QSPI_CCR_IMODE_1LINE,
                            QSPI_CCR_ADMODE_1LINE,
                            QSPI_CCR_DMODE_4LINE,
                            QSPI_CCR_ADSIZE_24, 4u,
                            QSPI_CCR_FMODE_MEMRD);
    qspi_wr(QSPI_CCR, ccr);
    return 0;
}

int qspi_memory_mapped_disable(void)
{
    uint32_t cr = qspi_rd(QSPI_CR);
    qspi_wr(QSPI_CCR, 0u);
    qspi_wr(QSPI_CR, cr);
    return 0;
}
