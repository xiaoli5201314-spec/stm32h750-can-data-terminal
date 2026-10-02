/*
 * eth_rmii.c
 * ---------------------------------------------------------------------------
 * RMII 以太网 BSP。
 *
 *   MAC：RMII 模式，100Mbps 全双工，由 PHY 提供 25MHz 参考时钟
 *   DMA：TX/RX 描述符环放在 SRAM4（MPU 非缓存区），收发缓冲放在 SRAM1 非缓存池
 *   管理：MDIO 读写 PHY 寄存器（BCR/BSR/ID1/ID2），用于链路诊断
 *
 * 本文件同时提供极简的 IPv4/UDP 组包与校验和，用于"批量上报"。
 */
#include <string.h>
#include "eth_rmii.h"
#include "hal_stub.h"
#include "diag_log.h"

/* 描述符（简化版：地址 + 状态/长度 + 下一描述符） */
typedef struct {
    uint32_t buf_addr;
    uint32_t status;
    uint32_t next;
    uint32_t reserved;
} eth_desc_t;

#define ETH_DESC_OWN    (1u << 31)
#define ETH_DESC_FD     (1u << 30)
#define ETH_DESC_LD     (1u << 29)
#define ETH_DESC_LEN_Msk 0x3FFFu

static eth_desc_t *s_tx_desc;
static eth_desc_t *s_rx_desc;
static uint8_t    *s_tx_buf;
static uint8_t    *s_rx_buf;
static uint32_t    s_tx_head;
static uint32_t    s_rx_tail;
static eth_stats_t s_stats;
static uint8_t     s_mac[6] = { 0x02u, 0x00u, 0x00u, 0x48u, 0x37u, 0x50u };
static uint8_t     s_loopback;

/* 自环模式的接收暂存队列（不依赖 PHY） */
#define ETH_LOOP_QUEUE  4u
static uint8_t  s_loop_buf[ETH_LOOP_QUEUE][H750_ETH_RX_BUF_LEN];
static uint32_t s_loop_len[ETH_LOOP_QUEUE];
static uint32_t s_loop_head;
static uint32_t s_loop_tail;

static void mac_wr(uint32_t off, uint32_t val)
{
    H750_REG32(H750_ETH_BASE + off) = val;
#ifdef H750_PC_SIM
    hal_sim_mac_reg_write(H750_ETH_BASE, off, val);
#endif
}

static uint32_t mac_rd(uint32_t off)
{
    return H750_REG32(H750_ETH_BASE + off);
}

/* ---------------------------------------------------------------------------
 * MDIO
 * ------------------------------------------------------------------------- */
int eth_phy_write(uint8_t reg, uint16_t value)
{
    mac_wr(ETH_MACMDIODR, (uint32_t)value);
    mac_wr(ETH_MACMDIOAR,
           ETH_MACMDIOAR_MB |
           ((uint32_t)ETH_MACMDIOAR_GOC_WRITE << ETH_MACMDIOAR_GOC_Pos) |
           ((uint32_t)0x0u << ETH_MACMDIOAR_CR_Pos) |
           (((uint32_t)reg & 0x1Fu) << ETH_MACMDIOAR_RDA_Pos) |
           (((uint32_t)H750_ETH_PHY_ADDR & 0x1Fu) << ETH_MACMDIOAR_PA_Pos));
    s_stats.mdio_writes++;
    return 0;
}

int eth_phy_read(uint8_t reg, uint16_t *value)
{
    uint32_t guard = 0u;

    if (value == 0) {
        return -1;
    }
    mac_wr(ETH_MACMDIOAR,
           ETH_MACMDIOAR_MB |
           ((uint32_t)ETH_MACMDIOAR_GOC_READ << ETH_MACMDIOAR_GOC_Pos) |
           ((uint32_t)0x0u << ETH_MACMDIOAR_CR_Pos) |
           (((uint32_t)reg & 0x1Fu) << ETH_MACMDIOAR_RDA_Pos) |
           (((uint32_t)H750_ETH_PHY_ADDR & 0x1Fu) << ETH_MACMDIOAR_PA_Pos));

    s_stats.mdio_reads++;
    /* 等待 MB 位被硬件清零 */
    while (((mac_rd(ETH_MACMDIOAR) & ETH_MACMDIOAR_MB) != 0u) && (guard < 10000u)) {
        guard++;
    }
    *value = (uint16_t)(mac_rd(ETH_MACMDIODR) & 0xFFFFu);
    return 0;
}

int eth_phy_identify(uint16_t *id1, uint16_t *id2)
{
    uint16_t a = 0u;
    uint16_t b = 0u;
    if ((eth_phy_read(H750_ETH_PHY_ID1, &a) != 0) ||
        (eth_phy_read(H750_ETH_PHY_ID2, &b) != 0)) {
        return -1;
    }
    s_stats.phy_id1 = a;
    s_stats.phy_id2 = b;
    if (id1 != 0) { *id1 = a; }
    if (id2 != 0) { *id2 = b; }
    return 0;
}

int eth_link_status(uint8_t *link_up)
{
    uint16_t bsr = 0u;
    uint16_t bcr = 0u;

    if (eth_phy_read(H750_ETH_PHY_BSR, &bsr) != 0) {
        return -1;
    }
    if (eth_phy_read(H750_ETH_PHY_BCR, &bcr) != 0) {
        return -1;
    }
    s_stats.phy_bsr = bsr;
    s_stats.phy_bcr = bcr;
    s_stats.link_up = ((bsr & 0x0004u) != 0u) ? 1u : 0u;
    s_stats.full_duplex = ((bsr & 0x2000u) != 0u) ? 1u : 0u;
    s_stats.speed_100m = ((bsr & 0x4000u) != 0u) ? 1u : 0u;
    if (link_up != 0) {
        *link_up = s_stats.link_up;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------------------- */
int eth_init(void)
{
    uint32_t i;
    uint16_t bcr = 0u;

    s_tx_desc = (eth_desc_t *)hal_mem_ptr(H750_ETH_DESC_BASE, H750_ETH_DESC_SIZE);
    if (s_tx_desc == 0) {
        return -1;
    }
    s_rx_desc = &s_tx_desc[H750_ETH_TX_DESC];
    s_tx_buf = (uint8_t *)hal_mem_ptr(H750_ETH_RX_BUF_BASE, H750_ETH_RX_BUF_SIZE);
    if (s_tx_buf == 0) {
        return -1;
    }
    s_rx_buf = &s_tx_buf[H750_ETH_RX_BUF_LEN * H750_ETH_TX_DESC];

    (void)memset(&s_stats, 0, sizeof(s_stats));
    s_tx_head = 0u;
    s_rx_tail = 0u;
    s_loop_head = 0u;
    s_loop_tail = 0u;

    /* DMA 描述符环初始化 */
    for (i = 0u; i < H750_ETH_TX_DESC; i++) {
        s_tx_desc[i].buf_addr = H750_ETH_RX_BUF_BASE + (i * H750_ETH_RX_BUF_LEN);
        s_tx_desc[i].status = 0u;
        s_tx_desc[i].next = (i + 1u == H750_ETH_TX_DESC) ? 0u : (i + 1u);
    }
    for (i = 0u; i < H750_ETH_RX_DESC; i++) {
        s_rx_desc[i].buf_addr = H750_ETH_RX_BUF_BASE +
                                ((H750_ETH_TX_DESC + i) * H750_ETH_RX_BUF_LEN);
        s_rx_desc[i].status = ETH_DESC_OWN;   /* 交还给 DMA */
        s_rx_desc[i].next = (i + 1u == H750_ETH_RX_DESC) ? 0u : (i + 1u);
    }

    /* MAC 配置：100Mbps 全双工、RMII、CRC 剥离、接收所有帧（调试期） */
    mac_wr(ETH_MACA0HR, ((uint32_t)s_mac[5] << 8) | (uint32_t)s_mac[4]);
    mac_wr(ETH_MACA0LR, ((uint32_t)s_mac[3] << 24) | ((uint32_t)s_mac[2] << 16) |
                        ((uint32_t)s_mac[1] << 8) | (uint32_t)s_mac[0]);
    mac_wr(ETH_MACCR, ETH_MACCR_DM | ETH_MACCR_FES | ETH_MACCR_IPC);
    mac_wr(ETH_MACPFR, ETH_MACPFR_RA | ETH_MACPFR_DBF);

    /* PHY：软复位 -> 自协商 -> 等待链路 */
    if (eth_phy_read(H750_ETH_PHY_BCR, &bcr) == 0) {
        eth_phy_write(H750_ETH_PHY_BCR, (uint16_t)(bcr | 0x8000u));   /* 软复位 */
        eth_phy_write(H750_ETH_PHY_BCR, (uint16_t)0x1000u);           /* 自协商使能 */
    }
    (void)eth_link_status(0);

    /* 使能收发 */
    mac_wr(ETH_MACCR, mac_rd(ETH_MACCR) | ETH_MACCR_TE | ETH_MACCR_RE);

    diag_log_write(H750_LOG_INFO, DIAG_MOD_ETH, DIAG_EV_BOOT, 0, 0u);
    return 0;
}

void eth_get_stats(eth_stats_t *st)
{
    if (st == 0) {
        return;
    }
    *st = s_stats;
    st->loopback = s_loopback;
}

void eth_set_loopback(int enable)
{
    s_loopback = (enable != 0) ? 1u : 0u;
}

int eth_get_loopback(void)
{
    return (s_loopback != 0) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * 收发
 * ------------------------------------------------------------------------- */
int eth_send_frame(const void *frame, uint32_t len)
{
    uint32_t idx;

    if ((frame == 0) || (len == 0u) || (len > H750_ETH_RX_BUF_LEN)) {
        s_stats.tx_errors++;
        return -1;
    }
    idx = s_tx_head;
    (void)memcpy(&s_tx_buf[idx * H750_ETH_RX_BUF_LEN], frame, len);
    s_tx_desc[idx].status = ETH_DESC_OWN | ETH_DESC_FD | ETH_DESC_LD | (len & ETH_DESC_LEN_Msk);
    s_tx_head = (idx + 1u) % H750_ETH_TX_DESC;
    s_stats.tx_frames++;
    s_stats.tx_bytes += len;

    if (s_loopback != 0u) {
        /* 自环：直接投递到接收队列，用于验证上报链路 */
        uint32_t slot = s_loop_tail;
        (void)memcpy(s_loop_buf[slot], frame, len);
        s_loop_len[slot] = len;
        s_loop_tail = (slot + 1u) % ETH_LOOP_QUEUE;
        s_stats.rx_frames++;
        s_stats.rx_bytes += len;
    }
    return 0;
}

int eth_poll_rx(uint8_t *buf, uint32_t cap, uint32_t *out_len)
{
    uint32_t len;

    if ((buf == 0) || (out_len == 0)) {
        return -1;
    }
    /* 先看 RX 描述符环 */
    if ((s_rx_desc[s_rx_tail].status & ETH_DESC_OWN) == 0u) {
        len = s_rx_desc[s_rx_tail].status & ETH_DESC_LEN_Msk;
        if (len > cap) {
            len = cap;
            s_stats.rx_dropped++;
        }
        (void)memcpy(buf, (const void *)(uintptr_t)s_rx_desc[s_rx_tail].buf_addr, len);
        s_rx_desc[s_rx_tail].status = ETH_DESC_OWN;
        s_rx_tail = (s_rx_tail + 1u) % H750_ETH_RX_DESC;
        *out_len = len;
        return 1;
    }

    /* 再看自环队列 */
    if (s_loop_head != s_loop_tail) {
        len = s_loop_len[s_loop_head];
        if (len > cap) {
            len = cap;
        }
        (void)memcpy(buf, s_loop_buf[s_loop_head], len);
        s_loop_head = (s_loop_head + 1u) % ETH_LOOP_QUEUE;
        *out_len = len;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * 极简 IPv4 / UDP 组包
 * ------------------------------------------------------------------------- */
uint16_t eth_ip_checksum(const uint8_t *hdr, uint32_t len)
{
    uint32_t sum = 0u;
    uint32_t i;

    if (hdr == 0) {
        return 0u;
    }
    for (i = 0u; (i + 1u) < len; i += 2u) {
        sum += ((uint32_t)hdr[i] << 8) | (uint32_t)hdr[i + 1u];
    }
    if ((len & 1u) != 0u) {
        sum += ((uint32_t)hdr[len - 1u] << 8);
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

static uint32_t put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
    return 2u;
}

static uint32_t put_ip(uint8_t *p, uint32_t ip)
{
    p[0] = (uint8_t)((ip >> 24) & 0xFFu);
    p[1] = (uint8_t)((ip >> 16) & 0xFFu);
    p[2] = (uint8_t)((ip >> 8) & 0xFFu);
    p[3] = (uint8_t)(ip & 0xFFu);
    return 4u;
}

uint32_t eth_build_udp_ipv4(uint8_t *buf, uint32_t cap,
                            const uint8_t src_mac[6], const uint8_t dst_mac[6],
                            uint32_t src_ip, uint32_t dst_ip,
                            uint16_t src_port, uint16_t dst_port,
                            const uint8_t *payload, uint32_t payload_len)
{
    uint32_t total = 14u + 20u + 8u + payload_len;
    uint32_t udp_len = 8u + payload_len;
    uint8_t *eth;
    uint8_t *ip;
    uint8_t *udp;
    uint16_t csum;

    if ((buf == 0) || (cap < total) || (src_mac == 0) || (dst_mac == 0)) {
        return 0u;
    }
    eth = buf;
    (void)memcpy(&eth[0], dst_mac, 6u);
    (void)memcpy(&eth[6], src_mac, 6u);
    eth[12] = 0x08u;
    eth[13] = 0x00u;

    ip = &buf[14];
    ip[0] = 0x45u;
    ip[1] = 0x00u;
    (void)put16(&ip[2], (uint16_t)(20u + udp_len));
    (void)put16(&ip[4], 0x0001u);           /* 标识 */
    (void)put16(&ip[6], 0x4000u);           /* 不分片 */
    ip[8] = 64u;                            /* TTL */
    ip[9] = 17u;                            /* UDP */
    (void)put16(&ip[10], 0u);
    (void)put_ip(&ip[12], src_ip);
    (void)put_ip(&ip[16], dst_ip);
    csum = eth_ip_checksum(ip, 20u);
    (void)put16(&ip[10], csum);

    udp = &buf[34];
    (void)put16(&udp[0], src_port);
    (void)put16(&udp[2], dst_port);
    (void)put16(&udp[4], (uint16_t)udp_len);
    (void)put16(&udp[6], 0u);               /* 校验和：IPv4 下可选，置 0 */
    if ((payload != 0) && (payload_len > 0u)) {
        (void)memcpy(&udp[8], payload, payload_len);
    }
    /* 以太网最小帧长 60 字节（不含 FCS），不足时补零 */
    if (total < 60u) {
        (void)memset(&buf[total], 0, 60u - total);
        total = 60u;
    }
    return total;
}

int eth_send_report(const uint8_t *payload, uint32_t len)
{
    static uint8_t frame[H750_ETHERNET_FRAME_MAX];
    uint8_t dst_mac[6] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu };
    uint32_t total;

    if ((payload == 0) || (len == 0u)) {
        return -1;
    }
    if (len > (H750_ETHERNET_FRAME_MAX - 42u)) {
        return -2;
    }
    total = eth_build_udp_ipv4(frame, sizeof(frame), s_mac, dst_mac,
                               0x0A000021u, 0x0A000001u,
                               50000u, 50001u, payload, len);
    if (total == 0u) {
        return -3;
    }
    return eth_send_frame(frame, total);
}

uint32_t eth_parse_stats(const eth_stats_t *st, char *buf, uint32_t cap)
{
    if ((st == 0) || (buf == 0) || (cap < 8u)) {
        return 0u;
    }
    /* 极简格式化：LINK=1,TX=123,RX=45 */
    {
        uint32_t n = 0u;
        const char *k = "LINK=";
        while ((*k != '\0') && (n + 1u < cap)) { buf[n++] = *k++; }
        if (n + 1u < cap) { buf[n++] = (char)('0' + (st->link_up ? 1 : 0)); }
        k = ",TX=";
        while ((*k != '\0') && (n + 1u < cap)) { buf[n++] = *k++; }
        {
            uint32_t v = st->tx_frames;
            char tmp[12];
            uint32_t m = 0u;
            if (v == 0u) { tmp[m++] = '0'; }
            while (v > 0u) { tmp[m++] = (char)('0' + (v % 10u)); v /= 10u; }
            while ((m > 0u) && (n + 1u < cap)) { buf[n++] = tmp[--m]; }
        }
        k = ",RX=";
        while ((*k != '\0') && (n + 1u < cap)) { buf[n++] = *k++; }
        {
            uint32_t v = st->rx_frames;
            char tmp[12];
            uint32_t m = 0u;
            if (v == 0u) { tmp[m++] = '0'; }
            while (v > 0u) { tmp[m++] = (char)('0' + (v % 10u)); v /= 10u; }
            while ((m > 0u) && (n + 1u < cap)) { buf[n++] = tmp[--m]; }
        }
        buf[n] = '\0';
        return n;
    }
}
