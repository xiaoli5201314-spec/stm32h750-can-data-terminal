/*
 * h750_config.h
 * ---------------------------------------------------------------------------
 * 板级与工程配置：时钟树常量、内存布局、FDCAN 位速率与过滤器规模、
 * 分层缓存/内存池规模、上报参数、板级 I/O 定义。
 *
 * 本文件是"设计意图"的唯一来源：驱动、应用、单元测试与文档中的数值
 * 都应从这里引用，避免出现多处硬编码不一致。
 */
#ifndef H750_CONFIG_H
#define H750_CONFIG_H

#include <stdint.h>
#include "stm32h750_regs.h"

/* ==========================================================================
 * 1. 时钟树（HSE 25MHz -> PLL1 -> 480MHz）
 * ========================================================================== */
#define H750_HSE_HZ             25000000u
#define H750_PLL1_M             5u          /* DIVM1：25MHz / 5 = 5MHz 参考 */
#define H750_PLL1_N             192u        /* DIVN：5MHz * 192 = 960MHz VCO */
#define H750_PLL1_P             2u          /* DIVP：960 / 2 = 480MHz SYSCLK */
#define H750_PLL1_Q             4u          /* DIVQ：960 / 4 = 240MHz（FDCAN/QSPI） */
#define H750_PLL1_R             2u          /* DIVR：960 / 2 = 480MHz */
#define H750_PLL1_VCO_HZ        960000000u
#define H750_SYSCLK_HZ          480000000u
#define H750_CPUCLK_HZ          480000000u  /* D1CPRE = /1 */
#define H750_HCLK_HZ            240000000u  /* HPRE   = /2 */
#define H750_APB_HZ             120000000u  /* DxPPRE = /2 */
#define H750_PLL1Q_HZ           240000000u
#define H750_FDCAN_KERNEL_HZ    H750_PLL1Q_HZ
#define H750_FMC_KERNEL_HZ      H750_HCLK_HZ
#define H750_SDRAM_CLK_HZ       (H750_HCLK_HZ / 2u)     /* SDCLK = HCLK3/2 = 120MHz */
#define H750_QSPI_KERNEL_HZ     H750_PLL1Q_HZ
#define H750_QSPI_CLK_HZ        60000000u               /* 240MHz / (2*(1+1)) */
#define H750_SDMMC_KERNEL_HZ    H750_PLL1Q_HZ
#define H750_SDMMC_CLK_HZ       50000000u
#define H750_LTDC_PIXCLK_HZ     33260000u               /* 800x480@60Hz */
#define H750_FLASH_LATENCY      4u                      /* VOS0 + 480MHz，见参考手册等待周期表 */

/* LTDC 时序（800x480 面板） */
#define H750_LCD_WIDTH          800u
#define H750_LCD_HEIGHT         480u
#define H750_LCD_HSYNC          8u
#define H750_LCD_HBP            88u
#define H750_LCD_HFP            40u
#define H750_LCD_VSYNC          3u
#define H750_LCD_VBP            32u
#define H750_LCD_VFP            13u

/* ==========================================================================
 * 2. FDCAN 位速率与过滤器规模
 * ========================================================================== */
#define H750_CAN1_NOMINAL_BPS   500000u
#define H750_CAN1_DATA_BPS      2000000u
#define H750_CAN2_NOMINAL_BPS   1000000u
#define H750_CAN2_DATA_BPS      5000000u
#define H750_CAN_SAMPLE_PT_PM   800u        /* 目标采样点 80.0%（千分比） */

#define H750_CAN1_STD_FILTERS   28u
#define H750_CAN1_EXT_FILTERS   16u
#define H750_CAN1_RXFIFO0_ELEMS 32u
#define H750_CAN1_RXFIFO1_ELEMS 16u
#define H750_CAN1_TX_ELEMS      8u
#define H750_CAN1_TXEVT_ELEMS   8u

#define H750_CAN2_STD_FILTERS   28u
#define H750_CAN2_EXT_FILTERS   16u
#define H750_CAN2_RXFIFO0_ELEMS 32u
#define H750_CAN2_RXFIFO1_ELEMS 16u
#define H750_CAN2_TX_ELEMS      8u
#define H750_CAN2_TXEVT_ELEMS   8u

#define H750_CAN_MAX_DATA       64u         /* CAN FD 最大数据场 */
#define H750_CAN_FD_ELEM_WORDS  18u         /* 2 word 头 + 16 word 数据 */

/* 总线错误阈值（ISO 11898-1 / CAN FD 协议） */
#define H750_CAN_TEC_WARN       96u
#define H750_CAN_TEC_PASSIVE    128u
#define H750_CAN_TEC_BUSOFF     256u
#define H750_CAN_BUSOFF_BACKOFF_MIN_MS  1000u
#define H750_CAN_BUSOFF_BACKOFF_MAX_MS  30000u
#define H750_CAN_BUSOFF_MAX_RETRY       5u
#define H750_CAN_TX_TIMEOUT_MS  10u
#define H750_CAN_TX_MAX_RETRY   3u

/* ==========================================================================
 * 3. 内存布局（与 MPU 区域一一对应）
 * ========================================================================== */
/* SRAM1（0x30000000，非缓存）：以太网收发缓冲 + 批量上报缓冲 */
#define H750_ETH_RX_BUF_BASE    0x30000000u
#define H750_ETH_RX_BUF_SIZE    0x00003000u   /* 12KB：8 x 1536B */
#define H750_REPORT_BUF_BASE    0x30004000u
#define H750_REPORT_BUF_SIZE    0x00004000u   /* 16KB：32 x 512B */

/* SRAM3（0x30040000，非缓存）：FDCAN 元素层与高优先级事件池 */
#define H750_CAN_POOL_BASE      0x30040000u
#define H750_CAN_POOL_SIZE      0x00002000u   /* 8KB：128 x 64B */
#define H750_CAN_EVT_POOL_BASE  0x30042000u
#define H750_CAN_EVT_POOL_SIZE  0x00001000u   /* 4KB：128 x 32B */

/* SRAM4（0x38000000，非缓存）：ETH DMA 描述符环 + 低功耗日志 */
#define H750_ETH_DESC_BASE      0x38000000u
#define H750_ETH_DESC_SIZE      0x00001000u   /* 4KB：TX/RX 描述符环 */

/* SDRAM 分区（32MB 中的前 16MB 为 MPU 可缓存区，0xC1000000 起 1MB 为非缓存窗口） */
#define H750_SDRAM_CACHE_BASE   (H750_SDRAM_BASE)                 /* 0xC0000000 */
#define H750_SDRAM_MSGCACHE_OFF 0x00000000u                       /* 512KB 分层报文缓存 */
#define H750_SDRAM_MSGCACHE_SIZE 0x00080000u
#define H750_SDRAM_OFFLINE_OFF  0x00080000u                       /* 8MB 离线数据环形区 */
#define H750_SDRAM_OFFLINE_SIZE 0x00800000u
#define H750_SDRAM_FB_OFF       0x00880000u                       /* 2 x 768KB 帧缓冲 */
#define H750_SDRAM_FB_SIZE      0x00180000u
#define H750_SDRAM_LOG_OFF      0x00A00000u                       /* 256KB 诊断日志环 */
#define H750_SDRAM_LOG_SIZE     0x00040000u
#define H750_SDRAM_NC_BASE      (H750_SDRAM_BASE + 0x01000000u)   /* 0xC1000000 */
#define H750_SDRAM_NC_SIZE      0x00100000u                       /* 1MB 非缓存 DMA 窗口 */

/* ==========================================================================
 * 4. 分层缓存与内存池
 * ========================================================================== */
#define H750_CACHE_P0_SLOTS     64u     /* 事件/报警：不可丢 */
#define H750_CACHE_P1_SLOTS     256u    /* 周期关键数据 */
#define H750_CACHE_P2_SLOTS     512u    /* 高频观测数据：可丢 */
/* 三层统一使用紧凑上报记录（80 字节），便于批量上报时零拷贝拼接 TLV */
#define H750_CACHE_ENTRY_BYTES  80u
#define H750_CACHE_FLUSH_WM_PM  700u    /* 水位触发上报：70.0% */

#define H750_REPORT_PERIOD_MS   20u
#define H750_REPORT_MAX_BYTES   1400u   /* 单包上限（以太网 MTU 内） */
#define H750_REPORT_MAX_RETRY   3u
#define H750_ETHERNET_FRAME_MAX 1518u   /* 含 14 字节以太网头的最长帧 */

#define H750_POOL_CAN_BLOCK     64u
#define H750_POOL_CAN_BLOCKS    128u
#define H750_POOL_EVT_BLOCK     32u
#define H750_POOL_EVT_BLOCKS    128u
#define H750_POOL_REPORT_BLOCK  512u
#define H750_POOL_REPORT_BLOCKS 32u
#define H750_POOL_SMALL_BLOCK   16u
#define H750_POOL_SMALL_BLOCKS  64u

#define H750_RING_CAN_RX_SLOTS  256u    /* CAN 接收队列（中断 -> 线程） */
#define H750_MAILBOX_PARSE_DEPTH 32u
#define H750_MAILBOX_REPORT_DEPTH 16u

#define H750_DIAG_LOG_ENTRIES   2048u   /* 诊断日志环形条目数 */
#define H750_DIAG_LOG_LEVEL     H750_LOG_INFO

/* ==========================================================================
 * 5. 报文字段/协议参数
 * ========================================================================== */
#define H750_REPORT_VERSION     1u
#define H750_NODE_ID            0x21u   /* 本机节点号 */
#define H750_HEARTBEAT_ID       0x700u
#define H750_SYNC_ID            0x580u

/* ==========================================================================
 * 6. 板级 I/O（控制/指示/调试）
 * ========================================================================== */
#define H750_LED_RUN_PORT       H750_GPIOG_BASE
#define H750_LED_RUN_PIN        9u
#define H750_LED_CAN1_PORT      H750_GPIOG_BASE
#define H750_LED_CAN1_PIN       3u
#define H750_LED_ERR_PORT       H750_GPIOC_BASE
#define H750_LED_ERR_PIN        13u
#define H750_USER_KEY_PORT      H750_GPIOD_BASE
#define H750_USER_KEY_PIN       7u
#define H750_LCD_BL_PORT        H750_GPIOA_BASE
#define H750_LCD_BL_PIN         0u
#define H750_LCD_RST_PORT       H750_GPIOD_BASE
#define H750_LCD_RST_PIN        4u
#define H750_TOUCH_INT_PORT     H750_GPIOD_BASE
#define H750_TOUCH_INT_PIN      5u
#define H750_TOUCH_RST_PORT     H750_GPIOD_BASE
#define H750_TOUCH_RST_PIN      6u
#define H750_SD_CD_PORT         H750_GPIOB_BASE
#define H750_SD_CD_PIN          5u

/* 触摸/模拟 I2C4 从地址 */
#define H750_I2C4_TOUCH_ADDR    0x5Du   /* 电容触摸控制器 */
#define H750_I2C4_ADC_ADDR      0x48u   /* 16bit ADC */
#define H750_I2C4_EEPROM_ADDR   0x50u   /* 配置 EEPROM */

/* ==========================================================================
 * 7. 日志级别
 * ========================================================================== */
#define H750_LOG_ERROR          0u
#define H750_LOG_WARN           1u
#define H750_LOG_INFO           2u
#define H750_LOG_DEBUG          3u

/* ==========================================================================
 * 8. SDRAM 器件时序参数（W9825G6KH-6 等级，单位 ns）
 *    tRCD=18, tRP=18, tRAS=42, tRC=60, tWR=15(2CLK@133MHz 保守 15ns), tRFC=60, tXSR=72, tMRD=12
 * ========================================================================== */
#define H750_SDRAM_TRCD_NS      18u
#define H750_SDRAM_TRP_NS       18u
#define H750_SDRAM_TRAS_NS      42u
#define H750_SDRAM_TRC_NS       60u
#define H750_SDRAM_TWR_NS       15u
#define H750_SDRAM_TRFC_NS      60u
#define H750_SDRAM_TXSR_NS      72u
#define H750_SDRAM_TMRD_NS      12u

/* 结构参数：4 bank x 4096 行 x 512 列 x 16 bit = 32MB */
#define H750_SDRAM_BANK_BITS    2u
#define H750_SDRAM_ROW_BITS     12u
#define H750_SDRAM_COL_BITS     9u
#define H750_SDRAM_WIDTH_BITS   16u
#define H750_SDRAM_CAS_LATENCY  2u      /* CL=2 @120MHz（-6 等级在 120MHz 下 CL=2 有余量） */
#define H750_SDRAM_REFRESH_MS   64u     /* 8192 行 / 64ms */
#define H750_SDRAM_ROWS         4096u

/* ==========================================================================
 * 9. QSPI Flash 参数（8MB SPI NOR）
 * ========================================================================== */
#define H750_QSPI_PAGE_SIZE     256u
#define H750_QSPI_SECTOR_SIZE   4096u
#define H750_QSPI_BLOCK_SIZE    65536u
#define H750_QSPI_CMD_READ_ID   0x9Fu
#define H750_QSPI_CMD_READ      0x03u
#define H750_QSPI_CMD_FAST_READ 0x0Bu
#define H750_QSPI_CMD_QUAD_READ 0xEBu
#define H750_QSPI_CMD_WRITE_EN  0x06u
#define H750_QSPI_CMD_PAGE_PROG 0x02u
#define H750_QSPI_CMD_QUAD_PROG 0x32u
#define H750_QSPI_CMD_SECTOR_ER 0x20u
#define H750_QSPI_CMD_BLOCK_ER  0xD8u
#define H750_QSPI_CMD_CHIP_ER   0xC7u
#define H750_QSPI_CMD_RDSR      0x05u

/* ==========================================================================
 * 10. SD 卡与自研日志文件系统（H750FS-lite）
 * ========================================================================== */
#define H750_SD_SECTOR_SIZE     512u
#define H750_SD_BLOCK_COUNT     65536u  /* 32MB 卡影像（仿真） */
#define H750_FS_MAGIC           0x48373530u  /* "H750" */
#define H750_FS_MAX_FILES       16u
#define H750_FS_NAME_LEN        24u
#define H750_FS_SECTORS_PER_CLUSTER 8u

/* ==========================================================================
 * 11. 以太网
 * ========================================================================== */
#define H750_ETH_TX_DESC        8u
#define H750_ETH_RX_DESC        8u
#define H750_ETH_RX_BUF_LEN     1536u
#define H750_ETH_PHY_ADDR       0u    /* LAN8720A，MDIO 地址由 PHYAD0 引脚决定 */
#define H750_ETH_PHY_BCR        0u
#define H750_ETH_PHY_BSR        1u
#define H750_ETH_PHY_ID1        2u
#define H750_ETH_PHY_ID2        3u
#define H750_ETH_PHY_SPECIAL    31u
#define H750_ETH_PHY_ID1_VALUE  0x0007u   /* OUI 高 16 位 */
#define H750_ETH_PHY_ID2_VALUE  0xC0F1u   /* 型号+版本 */

#endif /* H750_CONFIG_H */
