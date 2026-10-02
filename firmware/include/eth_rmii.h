/*
 * eth_rmii.h
 * ---------------------------------------------------------------------------
 * RMII 以太网（LAN8720A PHY）BSP：MAC 初始化、MDIO 管理接口、DMA 描述符环、
 * 帧收发，以及面向"批量上报"的极简 IPv4/UDP 组包与校验和计算。
 */
#ifndef H750_ETH_RMII_H
#define H750_ETH_RMII_H

#include <stdint.h>
#include "h750_config.h"

typedef struct {
    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t tx_errors;
    uint32_t rx_frames;
    uint32_t rx_bytes;
    uint32_t rx_dropped;
    uint32_t mdio_reads;
    uint32_t mdio_writes;
    uint16_t phy_bcr;
    uint16_t phy_bsr;
    uint16_t phy_id1;
    uint16_t phy_id2;
    uint8_t  link_up;
    uint8_t  speed_100m;
    uint8_t  full_duplex;
    uint8_t  loopback;
} eth_stats_t;

int  eth_init(void);
int  eth_link_status(uint8_t *link_up);
int  eth_phy_identify(uint16_t *id1, uint16_t *id2);
int  eth_phy_read(uint8_t reg, uint16_t *value);
int  eth_phy_write(uint8_t reg, uint16_t value);
void eth_get_stats(eth_stats_t *st);
int  eth_send_frame(const void *frame, uint32_t len);
int  eth_poll_rx(uint8_t *buf, uint32_t cap, uint32_t *out_len);
/* 自环自检：无 PHY/网线时也能验证上报链路（发出后立刻回收到接收队列） */
void eth_set_loopback(int enable);
int  eth_get_loopback(void);
/* 把上报负载封装为 IPv4/UDP 帧并发出 */
int  eth_send_report(const uint8_t *payload, uint32_t len);
/* 极简协议栈工具（纯函数，便于单元测试） */
uint16_t eth_ip_checksum(const uint8_t *hdr, uint32_t len);
uint32_t eth_build_udp_ipv4(uint8_t *buf, uint32_t cap,
                            const uint8_t src_mac[6], const uint8_t dst_mac[6],
                            uint32_t src_ip, uint32_t dst_ip,
                            uint16_t src_port, uint16_t dst_port,
                            const uint8_t *payload, uint32_t payload_len);
uint32_t eth_parse_stats(const eth_stats_t *st, char *buf, uint32_t cap);

#endif /* H750_ETH_RMII_H */
