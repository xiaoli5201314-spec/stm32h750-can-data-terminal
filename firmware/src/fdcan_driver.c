/*
 * fdcan_driver.c
 * ---------------------------------------------------------------------------
 * FDCAN 驱动实现：位时序计算、消息 RAM 分区、过滤器装载、收发、时间戳、
 * 错误计数与总线恢复、发送超时重试。
 */
#include <string.h>
#include "fdcan_driver.h"
#include "diag_log.h"
#include "hal_stub.h"

/* 时间戳计数器配置：内部计数器，按标称位时间递增（TCP = 0） */
#define FDCAN_TSCC_INTERNAL_BITTIME   ((1u << FDCAN_TSCC_TSS_Pos))
/* Bounded register polling, as in ST HAL_FDCAN_Stop (not a wall-clock delay). */
#define FDCAN_CONFIG_TIMEOUT_COUNT   50u

/* 每个实例的发送状态跟踪 */
typedef struct {
    uint32_t pending_mask;
    uint32_t start_ms;
    uint32_t retries;
    uint8_t  active;
} fdcan_tx_state_t;

static fdcan_tx_state_t s_tx[2];

static uint32_t bus_index(const fdcan_config_t *cfg)
{
    return (cfg->bus != 0u) ? 1u : 0u;
}

/* 寄存器读写封装：仿真构建下同步通知外设模型 */
static void fdcan_wr(uint32_t base, uint32_t off, uint32_t val)
{
#ifdef H750_PC_SIM
    hal_sim_fdcan_reg_write(base, off, val);
#else
    H750_REG32(base + off) = val;
#endif
}

static uint32_t fdcan_rd(uint32_t base, uint32_t off)
{
#ifdef H750_PC_SIM
    return hal_sim_fdcan_reg_read(base, off);
#else
    return H750_REG32(base + off);
#endif
}

static int fdcan_wait_cccr(uint32_t base, uint32_t mask, uint32_t value)
{
    uint32_t count;
    for (count = 0u; count <= FDCAN_CONFIG_TIMEOUT_COUNT; count++) {
        if ((fdcan_rd(base, FDCAN_CCCR) & mask) == value) {
            return 0;
        }
    }
    return -1;
}

/* 消息 RAM 访问：目标构建直接映射物理地址，仿真构建映射主机缓冲 */
static volatile uint32_t *msgram_ptr(uint32_t msgram_base, uint32_t bytes)
{
    return (volatile uint32_t *)hal_mem_ptr(msgram_base, bytes);
}

/* ==========================================================================
 * 消息 RAM 分区与 DLC
 * ========================================================================== */
uint32_t fdcan_dlc_to_bytes(uint8_t dlc)
{
    if (dlc <= 8u) {
        return dlc;
    }
    switch (dlc) {
    case 9u:  return 12u;
    case 10u: return 16u;
    case 11u: return 20u;
    case 12u: return 24u;
    case 13u: return 32u;
    case 14u: return 48u;
    case 15u: return 64u;
    default:  return 0u;
    }
}

uint32_t fdcan_frame_payload_bytes(const fdcan_frame_t *frame)
{
    if ((frame == 0) || (frame->rtr != 0u)) {
        return 0u;
    }
    if (frame->fdf == 0u) {
        return (frame->dlc > 8u) ? 8u : frame->dlc;
    }
    return fdcan_dlc_to_bytes(frame->dlc);
}

uint32_t fdcan_dlc_to_elem_size_code(uint8_t dlc)
{
    static const uint8_t code[16] = {
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,   /* 0..8   -> 8 字节 */
        1u, 2u, 3u, 4u, 5u, 6u, 7u            /* 9..15  -> 12/16/20/24/32/48/64 */
    };
    return (dlc < 16u) ? code[dlc] : 7u;
}

int fdcan_layout_compute(uint32_t std_filters, uint32_t ext_filters,
                         uint32_t rx0_elems, uint32_t rx1_elems,
                         uint32_t tx_elems, uint32_t txevt_elems,
                         uint32_t elem_words, fdcan_msgram_layout_t *out)
{
    uint32_t off;

    if (out == 0) {
        return -1;
    }
    if ((elem_words < FDCAN_ELEM_HDR_WORDS) || (elem_words > 32u)) {
        return -2;
    }
    (void)memset(out, 0, sizeof(*out));

    off = 0u;
    out->std_filter_words = off;
    off += std_filters * FDCAN_STD_FILTER_WORDS;
    out->ext_filter_words = off;
    off += ext_filters * FDCAN_EXT_FILTER_WORDS;
    out->rx0_words = off;
    off += rx0_elems * elem_words;
    out->rx1_words = off;
    off += rx1_elems * elem_words;
    out->txevt_words = off;
    off += txevt_elems * 2u;
    out->txbuf_words = off;
    off += tx_elems * elem_words;

    out->std_filter_count = std_filters;
    out->ext_filter_count = ext_filters;
    out->rx0_count = rx0_elems;
    out->rx1_count = rx1_elems;
    out->txevt_count = txevt_elems;
    out->txbuf_count = tx_elems;
    out->elem_words = elem_words;
    out->total_words = off;

    if (off > H750_FDCAN_MSGRAM_WORDS) {
        return -3;   /* 超出 2560 word 消息 RAM */
    }
    return 0;
}

/* ==========================================================================
 * 位时序计算
 * ========================================================================== */
static uint32_t pick_divisor(uint32_t kernel_hz, uint32_t bitrate,
                             uint32_t sp_target_pm, uint32_t tq_max,
                             uint32_t *dbrp_out, uint32_t *tseg1_out,
                             uint32_t *tseg2_out, uint32_t *sjw_out,
                             uint32_t *tq_out, uint32_t *sp_out,
                             uint32_t *actual_out)
{
    uint32_t d;
    uint32_t best_d = 0u;
    uint32_t best_score = 0xFFFFFFFFu;
    uint32_t best_tq_dev = 0xFFFFFFFFu;
    const uint32_t tq_pref = 20u;   /* 工程经验：每比特 16~25 个 tq 兼顾精度与抖动 */

    if ((kernel_hz == 0u) || (bitrate == 0u)) {
        return -1;
    }
    for (d = 1u; d <= 512u; d++) {
        uint32_t total;
        uint32_t sp_tq;
        uint32_t tseg1;
        uint32_t tseg2;
        uint32_t sp;
        uint32_t score;
        uint32_t tq_dev;

        if ((kernel_hz % (d * bitrate)) != 0u) {
            continue;
        }
        total = kernel_hz / (d * bitrate);
        if ((total < 8u) || (total > tq_max)) {
            continue;
        }
        /* 目标采样点对应的 (1 + TSEG1 + 1) 个 tq，四舍五入 */
        sp_tq = (total * sp_target_pm + 500u) / 1000u;
        if (sp_tq < 3u) {
            sp_tq = 3u;
        }
        if (sp_tq > (total - 2u)) {
            sp_tq = total - 2u;
        }
        tseg1 = sp_tq - 2u;            /* NTSEG1 = 采样点tq - 1(SYNC) - 1(编码偏移) */
        tseg2 = total - 3u - tseg1;
        if ((tseg1 < 1u) || (tseg1 > 255u) || (tseg2 < 1u) || (tseg2 > 127u)) {
            continue;
        }
        sp = ((tseg1 + 2u) * 1000u) / total;
        score = (sp > sp_target_pm) ? (sp - sp_target_pm) : (sp_target_pm - sp);
        tq_dev = (total > tq_pref) ? (total - tq_pref) : (tq_pref - total);

        if ((score < best_score) || ((score == best_score) && (tq_dev < best_tq_dev))) {
            best_score = score;
            best_tq_dev = tq_dev;
            best_d = d;
            *dbrp_out = d - 1u;
            *tseg1_out = tseg1;
            *tseg2_out = tseg2;
            *sjw_out = (tseg2 < 16u) ? tseg2 : 16u;
            *tq_out = total;
            *sp_out = sp;
        }
    }
    if (best_d == 0u) {
        return -2;   /* 无可行分频 */
    }
    /* 实际可达速率 = tq 频率 / 每比特 tq 数（分频搜索保证能整除，故等于目标速率） */
    *actual_out = kernel_hz / (best_d * (*tq_out));
    return 0;
}

int fdcan_bit_timing_calc(uint32_t kernel_hz, uint32_t nominal_bps,
                          uint32_t data_bps, uint32_t sp_target_pm,
                          fdcan_bit_timing_t *out)
{
    uint32_t nominal_actual = 0u;
    uint32_t data_actual = 0u;
    int rc;

    if (out == 0) {
        return -1;
    }
    (void)memset(out, 0, sizeof(*out));

    rc = pick_divisor(kernel_hz, nominal_bps, sp_target_pm, 40u,
                      &out->nbrp, &out->ntseg1, &out->ntseg2, &out->nsjw,
                      &out->nominal_tq, &out->nominal_sp_pm, &nominal_actual);
    if (rc != 0) {
        return rc;
    }
    rc = pick_divisor(kernel_hz, data_bps, sp_target_pm, 25u,
                      &out->dbrp, &out->dtseg1, &out->dtseg2, &out->dsjw,
                      &out->data_tq, &out->data_sp_pm, &data_actual);
    if (rc != 0) {
        return rc;
    }
    out->nominal_bps = nominal_actual;
    out->data_bps = data_actual;
    out->tdc_enable = (data_bps > 1000000u) ? 1u : 0u;
    return 0;
}

uint32_t fdcan_nbtp_value(const fdcan_bit_timing_t *bt)
{
    uint32_t v = 0u;
    if (bt == 0) {
        return 0u;
    }
    v |= ((bt->nsjw << FDCAN_NBTP_NSJW_Pos) & FDCAN_NBTP_NSJW_Msk);
    v |= ((bt->nbrp << FDCAN_NBTP_NBRP_Pos) & FDCAN_NBTP_NBRP_Msk);
    v |= ((bt->ntseg1 << FDCAN_NBTP_NTSEG1_Pos) & FDCAN_NBTP_NTSEG1_Msk);
    v |= ((bt->ntseg2 << FDCAN_NBTP_NTSEG2_Pos) & FDCAN_NBTP_NTSEG2_Msk);
    return v;
}

uint32_t fdcan_dbtp_value(const fdcan_bit_timing_t *bt)
{
    uint32_t v = 0u;
    if (bt == 0) {
        return 0u;
    }
    v |= ((bt->dsjw << FDCAN_DBTP_DSJW_Pos) & FDCAN_DBTP_DSJW_Msk);
    v |= ((bt->dtseg2 << FDCAN_DBTP_DTSEG2_Pos) & FDCAN_DBTP_DTSEG2_Msk);
    v |= ((bt->dtseg1 << FDCAN_DBTP_DTSEG1_Pos) & FDCAN_DBTP_DTSEG1_Msk);
    v |= ((bt->dbrp << FDCAN_DBTP_DBRP_Pos) & FDCAN_DBTP_DBRP_Msk);
    if (bt->tdc_enable != 0u) {
        v |= FDCAN_DBTP_TDC;
    }
    return v;
}

uint32_t fdcan_tdcr_value(uint32_t kernel_hz, uint32_t data_bps,
                          uint32_t loop_delay_ns, uint32_t tdcf_min)
{
    uint32_t mtq_ns;
    uint32_t tdco;

    if ((kernel_hz == 0u) || (data_bps == 0u)) {
        return 0u;
    }
    /* 1 个 mtq（最小时间量）= 1 / data_bps */
    mtq_ns = 1000000000u / data_bps;
    tdco = (loop_delay_ns + mtq_ns - 1u) / mtq_ns;
    if (tdco > 127u) {
        tdco = 127u;
    }
    return ((tdco & 0x7Fu) << 8) | (tdcf_min & 0x7Fu);
}

/* ==========================================================================
 * 初始化
 * ========================================================================== */
static int fdcan_program_msgram(const fdcan_config_t *cfg,
                                const fdcan_msgram_layout_t *lay)
{
    uint32_t base = cfg->base;
    uint32_t word;
    uint32_t code = fdcan_dlc_to_elem_size_code(15u);   /* 全部按 64 字节元素配置 */

    /* 标准/扩展过滤器区 */
    if (fdcan_filter_program(base, cfg->msgram_base, cfg->filters, lay) != 0) {
        return -1;
    }

    /* RX FIFO0 */
    word = lay->rx0_words;
    fdcan_wr(base, FDCAN_RXF0C,
             ((word << FDCAN_RXF0C_F0SA_Pos) & FDCAN_RXF0C_F0SA_Msk) |
             ((lay->rx0_count << FDCAN_RXF0C_F0S_Pos) & FDCAN_RXF0C_F0S_Msk) |
             ((lay->rx0_count / 2u) << FDCAN_RXF0C_F0WM_Pos));

    /* RX FIFO1 */
    word = lay->rx1_words;
    fdcan_wr(base, FDCAN_RXF1C,
             ((word << FDCAN_RXF1C_F1SA_Pos) & FDCAN_RXF1C_F1SA_Msk) |
             ((lay->rx1_count << FDCAN_RXF1C_F1S_Pos) & FDCAN_RXF1C_F1S_Msk) |
             ((lay->rx1_count / 2u) << FDCAN_RXF1C_F1WM_Pos));

    /* 元素尺寸：RX FIFO0/1 均 64 字节数据；RX 专用缓冲同尺寸 */
    fdcan_wr(base, FDCAN_RXESC,
             ((code << FDCAN_RXESC_F0DS_Pos) & FDCAN_RXESC_F0DS_Msk) |
             ((code << FDCAN_RXESC_F1DS_Pos) & FDCAN_RXESC_F1DS_Msk) |
             ((code << FDCAN_RXESC_RBDS_Pos) & FDCAN_RXESC_RBDS_Msk));

    /* TX 事件 FIFO */
    word = lay->txevt_words;
    fdcan_wr(base, FDCAN_TXEFC,
             ((word << FDCAN_TXEFC_EFSA_Pos) & FDCAN_TXEFC_EFSA_Msk) |
             ((lay->txevt_count << FDCAN_TXEFC_EFS_Pos) & FDCAN_TXEFC_EFS_Msk));

    /* TX 缓冲：专用缓冲（TFQM = 0），不使用 FIFO/队列模式 */
    word = lay->txbuf_words;
    fdcan_wr(base, FDCAN_TXBC,
             ((word << FDCAN_TXBC_TBSA_Pos) & FDCAN_TXBC_TBSA_Msk) |
             ((lay->txbuf_count << FDCAN_TXBC_NDTB_Pos) & FDCAN_TXBC_NDTB_Msk));
    fdcan_wr(base, FDCAN_TXESC, (code << FDCAN_TXESC_TBDS_Pos) & FDCAN_TXESC_TBDS_Msk);
    return 0;
}

int fdcan_init(const fdcan_config_t *cfg, fdcan_msgram_layout_t *layout_out)
{
    fdcan_msgram_layout_t lay;
    fdcan_bit_timing_t bt;
    uint32_t ie;
    uint32_t cccr;
    int rc;

    if (cfg == 0) {
        return -1;
    }
    rc = fdcan_bit_timing_calc(cfg->kernel_hz, cfg->nominal_bps, cfg->data_bps,
                               cfg->sp_target_pm, &bt);
    if (rc != 0) {
        return -2;
    }
    rc = fdcan_layout_compute(cfg->filters->std_count, cfg->filters->ext_count,
                              (cfg->bus == 0u) ? H750_CAN1_RXFIFO0_ELEMS : H750_CAN2_RXFIFO0_ELEMS,
                              (cfg->bus == 0u) ? H750_CAN1_RXFIFO1_ELEMS : H750_CAN2_RXFIFO1_ELEMS,
                              (cfg->bus == 0u) ? H750_CAN1_TX_ELEMS : H750_CAN2_TX_ELEMS,
                              (cfg->bus == 0u) ? H750_CAN1_TXEVT_ELEMS : H750_CAN2_TXEVT_ELEMS,
                              H750_CAN_FD_ELEM_WORDS, &lay);
    if (rc != 0) {
        return -3;
    }
    if (layout_out != 0) {
        *layout_out = lay;
    }

    /* 进入初始化模式并允许配置访问 */
    fdcan_wr(cfg->base, FDCAN_CCCR, FDCAN_CCCR_INIT | FDCAN_CCCR_CCE);

    /* 位时序 */
    fdcan_wr(cfg->base, FDCAN_NBTP, fdcan_nbtp_value(&bt));
    fdcan_wr(cfg->base, FDCAN_DBTP, fdcan_dbtp_value(&bt));
    if (bt.tdc_enable != 0u) {
        fdcan_wr(cfg->base, FDCAN_TDCR, fdcan_tdcr_value(cfg->kernel_hz, cfg->data_bps, 200u, 4u));
    }

    /* 时间戳：内部计数器，每个标称位时间 +1（TCP = 0） */
    fdcan_wr(cfg->base, FDCAN_TSCC, FDCAN_TSCC_INTERNAL_BITTIME);
    /* 超时计数器关闭 */
    fdcan_wr(cfg->base, FDCAN_TOCC, 0u);

    /* 消息 RAM 分区与过滤器 */
    rc = fdcan_program_msgram(cfg, &lay);
    if (rc != 0) {
        return -4;
    }

    /* 中断：RX FIFO0/1 新报文、总线关闭、错误被动/警告、错误计数变化 */
    ie = FDCAN_IR_RF0N | FDCAN_IR_RF1N | FDCAN_IR_RF0L | FDCAN_IR_RF1L |
         FDCAN_IR_BO | FDCAN_IR_EP | FDCAN_IR_EW | FDCAN_IR_BEC |
         FDCAN_IR_TOO | FDCAN_IR_MRAF;
    fdcan_wr(cfg->base, FDCAN_IE, ie);
    fdcan_wr(cfg->base, FDCAN_ILS, 0u);          /* 全部走中断线 0 */
    fdcan_wr(cfg->base, FDCAN_ILE, 1u);

    /* CC 控制：FD 使能、位速率切换、回环测试 */
    cccr = FDCAN_CCCR_INIT | FDCAN_CCCR_CCE;
    if (cfg->fd_enable != 0u) {
        cccr |= FDCAN_CCCR_FDOE;
    }
    if (cfg->brs_enable != 0u) {
        cccr |= FDCAN_CCCR_BRSE;
    }
    if (cfg->loopback != 0u) {
        cccr |= FDCAN_CCCR_TEST;
        fdcan_wr(cfg->base, FDCAN_TEST, FDCAN_TEST_LBCK);
    }
    fdcan_wr(cfg->base, FDCAN_CCCR, cccr);

    (void)memset(&s_tx[bus_index(cfg)], 0, sizeof(s_tx[0]));

#ifdef H750_PC_SIM
    hal_sim_fdcan_attach(cfg->base, cfg, &lay);
#endif
    diag_log_write(H750_LOG_INFO, (cfg->bus == 0u) ? DIAG_MOD_CAN1 : DIAG_MOD_CAN2,
                   DIAG_EV_BOOT, &lay.total_words, 4u);
    return 0;
}

int fdcan_start(uint32_t base)
{
    uint32_t cccr = fdcan_rd(base, FDCAN_CCCR);
    cccr &= ~(FDCAN_CCCR_INIT | FDCAN_CCCR_CCE);
    fdcan_wr(base, FDCAN_CCCR, cccr);
    return fdcan_wait_cccr(base, FDCAN_CCCR_INIT | FDCAN_CCCR_CCE, 0u);
}

int fdcan_stop(uint32_t base)
{
    uint32_t cccr = fdcan_rd(base, FDCAN_CCCR);
    fdcan_wr(base, FDCAN_CCCR, cccr | FDCAN_CCCR_INIT);
    if (fdcan_wait_cccr(base, FDCAN_CCCR_INIT, FDCAN_CCCR_INIT) != 0) {
        return -1;
    }
    cccr = fdcan_rd(base, FDCAN_CCCR);
    fdcan_wr(base, FDCAN_CCCR, cccr | FDCAN_CCCR_CCE);
    return fdcan_wait_cccr(base, FDCAN_CCCR_INIT | FDCAN_CCCR_CCE,
                           FDCAN_CCCR_INIT | FDCAN_CCCR_CCE);
}

/* ==========================================================================
 * 接收
 * ========================================================================== */
int fdcan_read_rx(const fdcan_config_t *cfg, const fdcan_msgram_layout_t *lay,
                  uint8_t fifo, fdcan_frame_t *out)
{
    uint32_t status_addr;
    uint32_t ack_addr;
    uint32_t elem_base_words;
    uint32_t status;
    uint32_t fl;
    uint32_t gi;
    uint32_t base_idx;
    volatile uint32_t *mram;
    uint32_t w0;
    uint32_t w1;
    uint32_t bytes;
    uint32_t words;
    uint32_t i;

    if ((cfg == 0) || (lay == 0) || (out == 0) || (fifo > 1u)) {
        return -1;
    }
    if (fifo == 0u) {
        status_addr = cfg->base + FDCAN_RXF0S;
        ack_addr = cfg->base + FDCAN_RXF0A;
        elem_base_words = lay->rx0_words;
    } else {
        status_addr = cfg->base + FDCAN_RXF1S;
        ack_addr = cfg->base + FDCAN_RXF1A;
        elem_base_words = lay->rx1_words;
    }

    mram = msgram_ptr(cfg->msgram_base, lay->total_words * 4u);
    if (mram == 0) {
        return -1;
    }

    status = fdcan_rd(cfg->base, status_addr - cfg->base);
    fl = (status & 0x7Fu);                       /* fill level */
    gi = (status >> 8) & 0x3Fu;                  /* get index  */
    if (fl == 0u) {
        return 0;
    }

    base_idx = elem_base_words + (gi * lay->elem_words);
    w0 = mram[base_idx];
    w1 = mram[base_idx + 1u];

    (void)memset(out, 0, sizeof(*out));
    out->can_id = w0 & FDCAN_RX_W0_ID_Msk;
    out->xtd = (uint8_t)((w0 >> 30) & 0x1u);
    out->rtr = (uint8_t)((w0 >> 29) & 0x1u);
    out->esi = (uint8_t)((w0 >> 31) & 0x1u);
    out->anmf = (uint8_t)((w1 >> 31) & 0x1u);
    out->filter_index = (uint8_t)((w1 >> 24) & 0x7Fu);
    out->fdf = (uint8_t)((w1 >> 21) & 0x1u);
    out->brs = (uint8_t)((w1 >> 20) & 0x1u);
    out->dlc = (uint8_t)((w1 >> 16) & 0xFu);
    out->rx_ts = (uint16_t)(w1 & 0xFFFFu);
    out->fifo = fifo;

    bytes = fdcan_frame_payload_bytes(out);
    if (bytes > H750_CAN_MAX_DATA) {
        bytes = H750_CAN_MAX_DATA;
    }
    words = FDCAN_DATA_BYTES_TO_WORDS(bytes);
    for (i = 0u; i < words; i++) {
        uint32_t w = mram[base_idx + 2u + i];
        uint32_t off = i * 4u;
        uint32_t k;
        for (k = 0u; (k < 4u) && ((off + k) < bytes); k++) {
            out->data[off + k] = (uint8_t)((w >> (8u * k)) & 0xFFu);
        }
    }

    /* 应答：写 put index，硬件自动推进 get index */
    fdcan_wr(cfg->base, ack_addr - cfg->base, gi);
    return 1;
}

/* ==========================================================================
 * 发送
 * ========================================================================== */
int fdcan_transmit(const fdcan_config_t *cfg, const fdcan_msgram_layout_t *lay,
                   const fdcan_frame_t *f)
{
    uint32_t fqs;
    uint32_t tffl;
    uint32_t pi;
    uint32_t base_idx;
    volatile uint32_t *mram;
    uint32_t w0;
    uint32_t w1;
    uint32_t bytes;
    uint32_t words;
    uint32_t i;
    uint32_t bus;

    if ((cfg == 0) || (lay == 0) || (f == 0)) {
        return -1;
    }
    mram = msgram_ptr(cfg->msgram_base, lay->total_words * 4u);
    if (mram == 0) {
        return -1;
    }
    fqs = fdcan_rd(cfg->base, FDCAN_TXFQS);
    tffl = fqs & FDCAN_TXFQS_TFFL_Msk;
    if (tffl >= lay->txbuf_count) {
        return -2;   /* TX 缓冲满 */
    }
    pi = (fqs & FDCAN_TXFQS_TFQPI_Msk) >> FDCAN_TXFQS_TFQPI_Pos;

    base_idx = lay->txbuf_words + (pi * lay->elem_words);

    w0 = (f->can_id & FDCAN_RX_W0_ID_Msk);
    if (f->xtd != 0u) { w0 |= FDCAN_RX_W0_XTD; }
    if (f->rtr != 0u) { w0 |= FDCAN_RX_W0_RTR; }
    if (f->esi != 0u) { w0 |= FDCAN_RX_W0_ESI; }

    w1 = ((uint32_t)f->dlc << FDCAN_TX_W1_DLC_Pos) & FDCAN_TX_W1_DLC_Msk;
    if (f->fdf != 0u) { w1 |= FDCAN_TX_W1_FDF; }
    if (f->brs != 0u) { w1 |= FDCAN_TX_W1_BRS; }
    w1 |= FDCAN_TX_W1_EFC;   /* 存入 TX 事件 FIFO，便于发送追踪 */

    mram[base_idx]      = w0;
    mram[base_idx + 1u] = w1;

    bytes = fdcan_frame_payload_bytes(f);
    if (bytes > H750_CAN_MAX_DATA) {
        bytes = H750_CAN_MAX_DATA;
    }
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
        mram[base_idx + 2u + i] = w;
    }

    bus = bus_index(cfg);
    s_tx[bus].pending_mask |= (1u << pi);
    s_tx[bus].start_ms = hal_time_ms();
    s_tx[bus].retries = 0u;
    s_tx[bus].active = 1u;

    fdcan_wr(cfg->base, FDCAN_TXBAR, (1u << pi));
    return (int)pi;
}

int fdcan_poll_tx(const fdcan_config_t *cfg, const fdcan_msgram_layout_t *lay,
                  fdcan_status_t *st, uint32_t now_ms)
{
    uint32_t bus;
    uint32_t pending;
    uint32_t elapsed;

    if ((cfg == 0) || (lay == 0) || (st == 0)) {
        return -1;
    }
    bus = bus_index(cfg);
    if (s_tx[bus].active == 0u) {
        return 0;
    }
    pending = fdcan_rd(cfg->base, FDCAN_TXBRP) & s_tx[bus].pending_mask;
    if (pending == 0u) {
        s_tx[bus].active = 0u;
        s_tx[bus].pending_mask = 0u;
        st->tx_frames++;
        return 1;
    }

    elapsed = now_ms - s_tx[bus].start_ms;
    if (elapsed < cfg->tx_timeout_ms) {
        return 2;   /* 等待中 */
    }

    /* 超时：取消未发出的请求 */
    fdcan_wr(cfg->base, FDCAN_TXBCR, pending);
    st->tx_timeouts++;
    if (s_tx[bus].retries < cfg->tx_retry_max) {
        s_tx[bus].retries++;
        st->tx_retries++;
        s_tx[bus].start_ms = now_ms;
        fdcan_wr(cfg->base, FDCAN_TXBAR, pending);
        diag_log_write(H750_LOG_WARN, (cfg->bus == 0u) ? DIAG_MOD_CAN1 : DIAG_MOD_CAN2,
                       DIAG_EV_CAN_TX_RETRY, &pending, 4u);
        return -1;
    }

    /* 重试用尽 */
    st->tx_failures++;
    s_tx[bus].active = 0u;
    s_tx[bus].pending_mask = 0u;
    diag_log_write(H750_LOG_ERROR, (cfg->bus == 0u) ? DIAG_MOD_CAN1 : DIAG_MOD_CAN2,
                   DIAG_EV_CAN_TX_TIMEOUT, &pending, 4u);
    return -2;
}

/* ==========================================================================
 * 状态、错误计数与总线恢复
 * ========================================================================== */
void fdcan_get_status(uint32_t base, fdcan_status_t *st)
{
    uint32_t ecr;
    uint32_t psr;

    if (st == 0) {
        return;
    }
    ecr = fdcan_rd(base, FDCAN_ECR);
    psr = fdcan_rd(base, FDCAN_PSR);

    st->tec = ecr & FDCAN_ECR_TEC_Msk;
    st->rec = (ecr & FDCAN_ECR_REC_Msk) >> FDCAN_ECR_REC_Pos;
    st->cel = (ecr & FDCAN_ECR_CEL_Msk) >> FDCAN_ECR_CEL_Pos;
    st->lec = psr & FDCAN_PSR_LEC_Msk;
    st->dlec = (psr & FDCAN_PSR_DLEC_Msk) >> FDCAN_PSR_DLEC_Pos;
    st->psr = psr;
    st->bus_off = ((psr & FDCAN_PSR_BO) != 0u) ? 1u : 0u;
    st->error_passive = ((psr & FDCAN_PSR_EP) != 0u) ? 1u : 0u;
    st->error_warning = ((psr & FDCAN_PSR_EW) != 0u) ? 1u : 0u;
    st->activity = (uint8_t)((psr & FDCAN_PSR_ACT_Msk) >> FDCAN_PSR_ACT_Pos);
}

int fdcan_handle_bus_error(const fdcan_config_t *cfg, fdcan_status_t *st,
                           uint32_t now_ms)
{
    uint32_t tec;
    uint32_t rec;

    if ((cfg == 0) || (st == 0)) {
        return -1;
    }
    fdcan_get_status(cfg->base, st);
    if (st->severe_fault != 0u) {
        return 3;
    }
    tec = st->tec;
    rec = st->rec;

    if (st->bus_off != 0u) {
        if (st->recovering == 0u) {
            /* 距上次总线关闭超过 60s 视为偶发，重置退避阶梯 */
            if ((st->last_busoff_ms != 0u) &&
                ((now_ms - st->last_busoff_ms) > 60000u)) {
                st->consecutive_busoff = 0u;
            }
            st->consecutive_busoff++;
            st->busoff_events++;
            st->last_busoff_ms = now_ms;
            st->recovering = 1u;
            st->recovery_started_ms = now_ms;
            {
                uint32_t shift = (st->consecutive_busoff > 5u) ? 4u
                                                              : (st->consecutive_busoff - 1u);
                uint32_t backoff = H750_CAN_BUSOFF_BACKOFF_MIN_MS << shift;
                if (backoff > H750_CAN_BUSOFF_BACKOFF_MAX_MS) {
                    backoff = H750_CAN_BUSOFF_BACKOFF_MAX_MS;
                }
                st->backoff_ms = backoff;
            }
            diag_log_write(H750_LOG_ERROR,
                           (cfg->bus == 0u) ? DIAG_MOD_CAN1 : DIAG_MOD_CAN2,
                           DIAG_EV_CAN_BUSOFF, &tec, 4u);
            if (st->consecutive_busoff >= H750_CAN_BUSOFF_MAX_RETRY) {
                st->severe_fault = 1u;
                return 3;
            }
            return 1;
        }
        return 2;   /* 已在恢复流程中 */
    }

    if ((tec >= H750_CAN_TEC_PASSIVE) || (rec >= H750_CAN_TEC_PASSIVE)) {
        st->error_passive = 1u;
    }
    if ((tec >= H750_CAN_TEC_WARN) || (rec >= H750_CAN_TEC_WARN)) {
        st->error_warning = 1u;
    }
    return 0;
}

int fdcan_bus_recovery_step(const fdcan_config_t *cfg, fdcan_status_t *st,
                            uint32_t now_ms)
{
    if ((cfg == 0) || (st == 0)) {
        return -1;
    }
    if (st->recovering == 0u) {
        return 0;
    }
    if (st->severe_fault != 0u) {
        return 3;
    }
    if ((now_ms - st->recovery_started_ms) < st->backoff_ms) {
        return 2;   /* 退避等待中，等待总线空闲 128 x 11 位时间以上 */
    }

    /* 重新初始化 CC：进入 INIT 后再退出，硬件会重新参与总线并清除 BUS-OFF */
    fdcan_wr(cfg->base, FDCAN_CCCR, FDCAN_CCCR_INIT | FDCAN_CCCR_CCE);
    fdcan_start(cfg->base);

    st->recovering = 0u;
    st->recover_events++;
    st->bus_off = 0u;
    diag_log_write(H750_LOG_WARN,
                   (cfg->bus == 0u) ? DIAG_MOD_CAN1 : DIAG_MOD_CAN2,
                   DIAG_EV_CAN_RECOVERED, &st->backoff_ms, 4u);
    return 1;
}

uint32_t fdcan_timestamp_to_us(uint16_t rx_ts, uint32_t nominal_bps)
{
    if (nominal_bps == 0u) {
        return 0u;
    }
    /* 时间戳计数器按标称位时间递增：us = ts * 1e6 / 标称速率 */
    return (uint32_t)(((uint64_t)rx_ts * 1000000ull) / (uint64_t)nominal_bps);
}

static int fdcan_restore_cccr(uint32_t base, uint32_t original)
{
    if ((original & FDCAN_CCCR_INIT) != 0u) {
        fdcan_wr(base, FDCAN_CCCR, fdcan_rd(base, FDCAN_CCCR) | FDCAN_CCCR_INIT);
        if (fdcan_wait_cccr(base, FDCAN_CCCR_INIT, FDCAN_CCCR_INIT) != 0) {
            return -1;
        }
    }
    /* Also cancels a pending INIT request after an entry timeout. */
    fdcan_wr(base, FDCAN_CCCR, original);
    return fdcan_wait_cccr(base, FDCAN_CCCR_INIT | FDCAN_CCCR_CCE,
                           original & (FDCAN_CCCR_INIT | FDCAN_CCCR_CCE));
}

int fdcan_set_sniff_mode(const fdcan_config_t *cfg, uint8_t enable)
{
    uint32_t gfc;
    uint32_t original_gfc;
    uint32_t original_cccr;
    int restored = 1;

    if ((cfg == 0) || (cfg->filters == 0)) {
        return -1;
    }
    original_cccr = fdcan_rd(cfg->base, FDCAN_CCCR);
    original_gfc = fdcan_rd(cfg->base, FDCAN_GFC);
    gfc = original_gfc;
    gfc &= ~(FDCAN_GFC_ANFS_Msk | FDCAN_GFC_ANFE_Msk);
    if (enable == 0u) {
        gfc |= fdcan_filter_gfc_value(cfg->filters) &
               (FDCAN_GFC_ANFS_Msk | FDCAN_GFC_ANFE_Msk);
    }
    /* GFC is writable only after INIT acknowledgement and CCE permission. */
    if (fdcan_stop(cfg->base) != 0) {
        goto rollback;
    }
    fdcan_wr(cfg->base, FDCAN_GFC, gfc);
    if (fdcan_rd(cfg->base, FDCAN_GFC) != gfc) {
        goto rollback;
    }
    if (fdcan_restore_cccr(cfg->base, original_cccr) != 0) {
        goto rollback;
    }
    return 0;

rollback:
    /* Restore filters before attempting to resume the original controller state. */
    if (fdcan_rd(cfg->base, FDCAN_GFC) != original_gfc) {
        if (fdcan_stop(cfg->base) != 0) {
            restored = 0;
        } else {
            fdcan_wr(cfg->base, FDCAN_GFC, original_gfc);
            if (fdcan_rd(cfg->base, FDCAN_GFC) != original_gfc) {
                restored = 0;
            }
        }
    }
    if (fdcan_restore_cccr(cfg->base, original_cccr) != 0) {
        restored = 0;
    }
    return restored ? -2 : -3;
}

int fdcan_get_sniff_mode(const fdcan_config_t *cfg)
{
    uint32_t gfc;
    if (cfg == 0) {
        return -1;
    }
    gfc = fdcan_rd(cfg->base, FDCAN_GFC);
    return ((gfc & (FDCAN_GFC_ANFS_Msk | FDCAN_GFC_ANFE_Msk)) == 0u) ? 1 : 0;
}

/* ==========================================================================
 * 板级默认配置
 * ========================================================================== */
const fdcan_config_t h750_fdcan1_config = {
    H750_FDCAN1_BASE,
    H750_FDCAN_MSGRAM_BASE,
    H750_FDCAN_KERNEL_HZ,
    H750_CAN1_NOMINAL_BPS,
    H750_CAN1_DATA_BPS,
    H750_CAN_SAMPLE_PT_PM,
    0u,                     /* bus 0 */
    1u, 1u, 0u,             /* FD 使能、BRS 使能、非回环 */
    H750_CAN_TX_MAX_RETRY,
    H750_CAN_TX_TIMEOUT_MS,
    &h750_can1_filter_cfg
};

const fdcan_config_t h750_fdcan2_config = {
    H750_FDCAN2_BASE,
    H750_FDCAN_MSGRAM_BASE + (H750_FDCAN_MSGRAM_WORDS * 4u),
    H750_FDCAN_KERNEL_HZ,
    H750_CAN2_NOMINAL_BPS,
    H750_CAN2_DATA_BPS,
    H750_CAN_SAMPLE_PT_PM,
    1u,                     /* bus 1 */
    1u, 1u, 0u,
    H750_CAN_TX_MAX_RETRY,
    H750_CAN_TX_TIMEOUT_MS,
    &h750_can2_filter_cfg
};
