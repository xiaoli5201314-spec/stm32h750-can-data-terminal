/*
 * stm32h750_regs.h
 * ---------------------------------------------------------------------------
 * STM32H750 (Cortex-M7, LQFP144) 片上资源寄存器定义。
 *
 * 说明：
 *   - 所有地址、偏移、位域均按 STM32H750 参考手册（RM0433）与 Cortex-M7
 *     架构参考手册（ARMv7-M）中的客观定义独立编写，未引用任何厂商源码。
 *   - 目标构建（真实硬件）直接对物理地址做 volatile 访问；
 *     PC 仿真构建（定义 H750_PC_SIM）把寄存器访问重定向到 hal_stub.c 中的
 *     寄存器文件数组，从而让驱动代码可以在主机上跑单元测试。
 *   - 本文件只描述"硬件事实"，不含任何业务逻辑。
 */
#ifndef H750_STM32H750_REGS_H
#define H750_STM32H750_REGS_H

#include <stdint.h>

/* ==========================================================================
 * 0. 寄存器访问抽象
 * ========================================================================== */

#ifdef H750_PC_SIM
/* PC 仿真：寄存器文件大小（word 数），地址按 word 索引取模映射 */
#define H750_SIM_REGFILE_WORDS   (1u << 20)   /* 1M word = 4MB 仿真寄存器空间 */
extern volatile uint32_t h750_sim_regfile[H750_SIM_REGFILE_WORDS];
#define H750_REG_PTR(addr) \
    (&h750_sim_regfile[(((uint32_t)(addr)) >> 2) & (H750_SIM_REGFILE_WORDS - 1u)])
#else
#define H750_REG_PTR(addr)  ((volatile uint32_t *)(uintptr_t)((uint32_t)(addr)))
#endif

#define H750_REG32(addr)    (*H750_REG_PTR(addr))
#define H750_REG16(addr)    (*((volatile uint16_t *)H750_REG_PTR(addr)))
#define H750_REG8(addr)     (*((volatile uint8_t  *)H750_REG_PTR(addr)))

/* ==========================================================================
 * 1. 存储与总线地址
 * ========================================================================== */
#define H750_ITCM_BASE          0x00000000u
#define H750_FLASH_BASE         0x08000000u
#define H750_DTCM_BASE          0x20000000u
#define H750_AXI_SRAM_BASE      0x24000000u
#define H750_SRAM1_BASE         0x30000000u
#define H750_SRAM2_BASE         0x30020000u
#define H750_SRAM3_BASE         0x30040000u
#define H750_SRAM4_BASE         0x38000000u
#define H750_BKPSRAM_BASE       0x38800000u
#define H750_PERIPH_BASE        0x40000000u
#define H750_QSPI_XIP_BASE      0x90000000u
#define H750_SDRAM_BASE         0xC0000000u

#define H750_ITCM_SIZE          (64u   * 1024u)
#define H750_FLASH_SIZE         (128u  * 1024u)
#define H750_DTCM_SIZE          (128u  * 1024u)
#define H750_AXI_SRAM_SIZE      (512u  * 1024u)
#define H750_SRAM1_SIZE         (128u  * 1024u)
#define H750_SRAM2_SIZE         (128u  * 1024u)
#define H750_SRAM3_SIZE         (32u   * 1024u)
#define H750_SRAM4_SIZE         (64u   * 1024u)
#define H750_BKPSRAM_SIZE       (4u    * 1024u)
#define H750_SDRAM_SIZE         (32u * 1024u * 1024u)
#define H750_QSPI_FLASH_SIZE    (8u  * 1024u * 1024u)

/* ==========================================================================
 * 2. 外设基地址
 * ========================================================================== */
#define H750_FDCAN1_BASE        0x4000A000u
#define H750_FDCAN2_BASE        0x4000A400u
#define H750_FDCAN_MSGRAM_BASE  0x4000AC00u   /* SRAMCAN，每实例 2560 word */
#define H750_FDCAN_MSGRAM_WORDS 2560u

#define H750_DMA1_BASE          0x40020000u
#define H750_DMA2_BASE          0x40020400u
#define H750_DMAMUX1_BASE       0x40020800u
#define H750_ETH_BASE           0x40028000u
#define H750_ADC1_BASE          0x40022000u

#define H750_TIM2_BASE          0x40000000u
#define H750_USART1_BASE        0x40011000u
#define H750_SPI1_BASE          0x40013000u
#define H750_I2C1_BASE          0x40005400u
#define H750_I2C4_BASE          0x58001C00u

#define H750_LTDC_BASE          0x50001000u
#define H750_FMC_BASE           0x52004000u
#define H750_QSPI_BASE          0x52005000u
#define H750_SDMMC1_BASE        0x52007000u
#define H750_FLASH_REG_BASE     0x52002000u

#define H750_GPIOA_BASE         0x58020000u
#define H750_GPIOB_BASE         0x58020400u
#define H750_GPIOC_BASE         0x58020800u
#define H750_GPIOD_BASE         0x58020C00u
#define H750_GPIOE_BASE         0x58021000u
#define H750_GPIOF_BASE         0x58021400u
#define H750_GPIOG_BASE         0x58021800u
#define H750_GPIOH_BASE         0x58021C00u

#define H750_RCC_BASE           0x58024400u
#define H750_PWR_BASE           0x58024800u

/* ==========================================================================
 * 3. Cortex-M7 内核：MPU / SCB / DWT（ARMv7-M 架构地址）
 * ========================================================================== */
#define H750_MPU_BASE           0xE000ED90u
#define H750_MPU_TYPE           0xE000ED90u
#define H750_MPU_CTRL           0xE000ED94u
#define H750_MPU_RNR            0xE000ED98u
#define H750_MPU_RBAR           0xE000ED9Cu
#define H750_MPU_RASR           0xE000EDA0u
#define H750_MPU_RBAR_A(n)      (0xE000ED9Cu + 8u * (uint32_t)(n))
#define H750_MPU_RASR_A(n)      (0xE000EDA0u + 8u * (uint32_t)(n))

/* MPU_TYPE */
#define MPU_TYPE_SEPARATE       (1u << 0)
#define MPU_TYPE_DREGION_Pos    8u
#define MPU_TYPE_DREGION_Msk    (0xFFu << MPU_TYPE_DREGION_Pos)
#define MPU_TYPE_IREGION_Pos    16u
#define MPU_TYPE_IREGION_Msk    (0xFFu << MPU_TYPE_IREGION_Pos)

/* MPU_CTRL */
#define MPU_CTRL_ENABLE         (1u << 0)
#define MPU_CTRL_HFNMIENA       (1u << 1)
#define MPU_CTRL_PRIVDEFENA     (1u << 2)

/* MPU_RBAR */
#define MPU_RBAR_REGION_Pos     0u
#define MPU_RBAR_REGION_Msk     (0xFu << MPU_RBAR_REGION_Pos)
#define MPU_RBAR_VALID          (1u << 4)
#define MPU_RBAR_ADDR_Msk       0xFFFFFFE0u

/* MPU_RASR */
#define MPU_RASR_ENABLE         (1u << 0)
#define MPU_RASR_SIZE_Pos       1u
#define MPU_RASR_SIZE_Msk       (0x1Fu << MPU_RASR_SIZE_Pos)
#define MPU_RASR_SRD_Pos        8u
#define MPU_RASR_SRD_Msk        (0xFFu << MPU_RASR_SRD_Pos)
#define MPU_RASR_B              (1u << 16)
#define MPU_RASR_C              (1u << 17)
#define MPU_RASR_S              (1u << 18)
#define MPU_RASR_TEX_Pos        19u
#define MPU_RASR_TEX_Msk        (0x7u << MPU_RASR_TEX_Pos)
#define MPU_RASR_AP_Pos         24u
#define MPU_RASR_AP_Msk         (0x7u << MPU_RASR_AP_Pos)
#define MPU_RASR_XN             (1u << 28)

/* AP 编码（ARMv7-M） */
#define MPU_AP_NO_ACCESS        0u   /* 特权/非特权均不可访问            */
#define MPU_AP_PRIV_RW          1u   /* 特权 RW，非特权不可访问          */
#define MPU_AP_PRIV_RW_URO      2u   /* 特权 RW，非特权只读              */
#define MPU_AP_FULL_ACCESS      3u   /* 特权/非特权均 RW                 */
#define MPU_AP_PRIV_RO          5u   /* 特权只读，非特权不可访问          */
#define MPU_AP_PRIV_RO_URO      6u   /* 特权/非特权均只读                */

/* SCB（Cache 维护） */
#define H750_SCB_CCSIDR         0xE000ED80u
#define H750_SCB_CCR            0xE000ED14u
#define H750_SCB_CLIDR          0xE000ED78u
#define H750_SCB_DCIMVAC        0xE000EF5Cu
#define H750_SCB_DCISW          0xE000EF60u
#define H750_SCB_DCCMVAC        0xE000EF68u
#define H750_SCB_DCCSW          0xE000EF64u
#define H750_SCB_DCCMVAU        0xE000EF74u
#define H750_SCB_DCCIMVAC       0xE000EF70u
#define H750_SCB_ICIALLU        0xE000EF50u

#define SCB_CCR_IC              (1u << 17)   /* I-Cache 使能 */
#define SCB_CCR_DC              (1u << 16)   /* D-Cache 使能 */
#define SCB_CCSIDR_LINESIZE_Pos 0u
#define SCB_CCSIDR_LINESIZE_Msk (0x7u << SCB_CCSIDR_LINESIZE_Pos)

/* DWT（周期计数，用于 µs 级时间戳） */
#define H750_DWT_CTRL           0xE0001000u
#define H750_DWT_CYCCNT         0xE0001004u
#define DWT_CTRL_CYCCNTENA      (1u << 0)

/* ==========================================================================
 * 4. FDCAN（Bosch M_CAN 兼容）
 *    寄存器偏移与位域按 RM0433 第 56 章定义
 * ========================================================================== */
#define FDCAN_CREL              0x000u
#define FDCAN_ENDN              0x004u
#define FDCAN_DBTP              0x00Cu
#define FDCAN_TEST              0x010u
#define FDCAN_RWD               0x014u
#define FDCAN_CCCR              0x018u
#define FDCAN_NBTP              0x01Cu
#define FDCAN_TSCC              0x020u
#define FDCAN_TSCV              0x024u
#define FDCAN_TOCC              0x028u
#define FDCAN_TOCV              0x02Cu
#define FDCAN_ECR               0x040u
#define FDCAN_PSR               0x044u
#define FDCAN_TDCR              0x048u
#define FDCAN_IR                0x050u
#define FDCAN_IE                0x054u
#define FDCAN_ILS               0x058u
#define FDCAN_ILE               0x05Cu
#define FDCAN_GFC               0x080u
#define FDCAN_SIDFC             0x084u
#define FDCAN_XIDFC             0x088u
#define FDCAN_XIDAM             0x090u
#define FDCAN_HPMS              0x094u
#define FDCAN_NDAT1             0x098u
#define FDCAN_NDAT2             0x09Cu
#define FDCAN_RXF0C             0x0A0u
#define FDCAN_RXF0S             0x0A4u
#define FDCAN_RXF0A             0x0A8u
#define FDCAN_RXBC              0x0ACu
#define FDCAN_RXF1C             0x0B0u
#define FDCAN_RXF1S             0x0B4u
#define FDCAN_RXF1A             0x0B8u
#define FDCAN_RXESC             0x0BCu
#define FDCAN_TXBC              0x0C0u
#define FDCAN_TXFQS            0x0C4u
#define FDCAN_TXESC             0x0C8u
#define FDCAN_TXBRP             0x0CCu
#define FDCAN_TXBAR             0x0D0u
#define FDCAN_TXBCR             0x0D4u
#define FDCAN_TXBTO             0x0D8u
#define FDCAN_TXBCF             0x0DCu
#define FDCAN_TXBTIE            0x0E0u
#define FDCAN_TXBCIE            0x0E4u
#define FDCAN_TXEFC             0x0F0u
#define FDCAN_TXEFS             0x0F4u
#define FDCAN_TXEFA             0x0F8u

/* CREL */
#define FDCAN_CREL_DAY_Pos      0u
#define FDCAN_CREL_MON_Pos      8u
#define FDCAN_CREL_YEAR_Pos     16u
#define FDCAN_CREL_SUBSTEP_Pos  20u
#define FDCAN_CREL_STEP_Pos     24u
#define FDCAN_CREL_REL_Pos      28u

/* CCCR */
#define FDCAN_CCCR_INIT         (1u << 0)
#define FDCAN_CCCR_CCE          (1u << 1)
#define FDCAN_CCCR_ASM          (1u << 2)
#define FDCAN_CCCR_CSA          (1u << 3)
#define FDCAN_CCCR_CSR          (1u << 4)
#define FDCAN_CCCR_MON          (1u << 5)
#define FDCAN_CCCR_DAR          (1u << 6)
#define FDCAN_CCCR_TEST         (1u << 7)
#define FDCAN_CCCR_FDOE         (1u << 8)
#define FDCAN_CCCR_BRSE         (1u << 9)
#define FDCAN_CCCR_PXHD         (1u << 12)
#define FDCAN_CCCR_EFBI         (1u << 13)
#define FDCAN_CCCR_TXP          (1u << 14)
#define FDCAN_CCCR_NISO         (1u << 15)

/* NBTP */
#define FDCAN_NBTP_NTSEG2_Pos   0u
#define FDCAN_NBTP_NTSEG2_Msk   (0x7Fu << FDCAN_NBTP_NTSEG2_Pos)
#define FDCAN_NBTP_NTSEG1_Pos   8u
#define FDCAN_NBTP_NTSEG1_Msk   (0xFFu << FDCAN_NBTP_NTSEG1_Pos)
#define FDCAN_NBTP_NBRP_Pos     16u
#define FDCAN_NBTP_NBRP_Msk     (0x1FFu << FDCAN_NBTP_NBRP_Pos)
#define FDCAN_NBTP_NSJW_Pos     25u
#define FDCAN_NBTP_NSJW_Msk     (0x7Fu << FDCAN_NBTP_NSJW_Pos)

/* DBTP */
#define FDCAN_DBTP_DSJW_Pos     0u
#define FDCAN_DBTP_DSJW_Msk     (0xFu << FDCAN_DBTP_DSJW_Pos)
#define FDCAN_DBTP_DTSEG2_Pos   4u
#define FDCAN_DBTP_DTSEG2_Msk   (0xFu << FDCAN_DBTP_DTSEG2_Pos)
#define FDCAN_DBTP_DTSEG1_Pos   8u
#define FDCAN_DBTP_DTSEG1_Msk   (0x1Fu << FDCAN_DBTP_DTSEG1_Pos)
#define FDCAN_DBTP_DBRP_Pos     16u
#define FDCAN_DBTP_DBRP_Msk     (0x1Fu << FDCAN_DBTP_DBRP_Pos)
#define FDCAN_DBTP_TDC          (1u << 23)

/* TEST */
#define FDCAN_TEST_LBCK         (1u << 4)
#define FDCAN_TEST_TX_Pos       5u
#define FDCAN_TEST_TX_Msk       (0x3u << FDCAN_TEST_TX_Pos)
#define FDCAN_TEST_RX           (1u << 7)

/* TSCC / TSCV */
#define FDCAN_TSCC_TSS_Pos      0u
#define FDCAN_TSCC_TSS_Msk      (0x3u << FDCAN_TSCC_TSS_Pos)
#define FDCAN_TSCC_TCP_Pos      16u
#define FDCAN_TSCC_TCP_Msk      (0xFu << FDCAN_TSCC_TCP_Pos)

/* TOCC */
#define FDCAN_TOCC_ETOC         (1u << 0)
#define FDCAN_TOCC_TOS_Pos      1u
#define FDCAN_TOCC_TOS_Msk      (0x3u << FDCAN_TOCC_TOS_Pos)
#define FDCAN_TOCC_TOP_Msk      0xFFFFu

/* GFC（全局过滤配置） */
#define FDCAN_GFC_RRFE          (1u << 0)
#define FDCAN_GFC_RRFS          (1u << 1)
#define FDCAN_GFC_ANFE_Pos      2u
#define FDCAN_GFC_ANFE_Msk      (0x3u << FDCAN_GFC_ANFE_Pos)
#define FDCAN_GFC_ANFS_Pos      4u
#define FDCAN_GFC_ANFS_Msk      (0x3u << FDCAN_GFC_ANFS_Pos)

/* SIDFC / XIDFC */
#define FDCAN_SIDFC_FLSSA_Pos   2u
#define FDCAN_SIDFC_FLSSA_Msk   (0x3FFFu << FDCAN_SIDFC_FLSSA_Pos)
#define FDCAN_SIDFC_LSS_Pos     16u
#define FDCAN_SIDFC_LSS_Msk     (0xFFu << FDCAN_SIDFC_LSS_Pos)
#define FDCAN_XIDFC_FLESA_Pos   2u
#define FDCAN_XIDFC_FLESA_Msk   (0x3FFFu << FDCAN_XIDFC_FLESA_Pos)
#define FDCAN_XIDFC_LSE_Pos     16u
#define FDCAN_XIDFC_LSE_Msk     (0x7Fu << FDCAN_XIDFC_LSE_Pos)

/* XIDAM */
#define FDCAN_XIDAM_EIDM_Msk    0x1FFFFFFFu

/* RXF0C / RXF1C */
#define FDCAN_RXF0C_F0SA_Pos    2u
#define FDCAN_RXF0C_F0SA_Msk    (0x3FFFu << FDCAN_RXF0C_F0SA_Pos)
#define FDCAN_RXF0C_F0S_Pos     16u
#define FDCAN_RXF0C_F0S_Msk     (0x7Fu << FDCAN_RXF0C_F0S_Pos)
#define FDCAN_RXF0C_F0WM_Pos    24u
#define FDCAN_RXF0C_F0WM_Msk    (0x7Fu << FDCAN_RXF0C_F0WM_Pos)
#define FDCAN_RXF0C_F0OM        (1u << 31)
#define FDCAN_RXF1C_F1SA_Pos    2u
#define FDCAN_RXF1C_F1SA_Msk    (0x3FFFu << FDCAN_RXF1C_F1SA_Pos)
#define FDCAN_RXF1C_F1S_Pos     16u
#define FDCAN_RXF1C_F1S_Msk     (0x7Fu << FDCAN_RXF1C_F1S_Pos)
#define FDCAN_RXF1C_F1WM_Pos    24u
#define FDCAN_RXF1C_F1WM_Msk    (0x7Fu << FDCAN_RXF1C_F1WM_Pos)
#define FDCAN_RXF1C_F1OM        (1u << 31)

/* RXF0S / RXF1S */
#define FDCAN_RXF0S_F0FL_Pos    0u
#define FDCAN_RXF0S_F0FL_Msk    (0x7Fu << FDCAN_RXF0S_F0FL_Pos)
#define FDCAN_RXF0S_F0GI_Pos    8u
#define FDCAN_RXF0S_F0GI_Msk    (0x3Fu << FDCAN_RXF0S_F0GI_Pos)
#define FDCAN_RXF0S_F0PI_Pos    16u
#define FDCAN_RXF0S_F0PI_Msk    (0x3Fu << FDCAN_RXF0S_F0PI_Pos)
#define FDCAN_RXF0S_F0F         (1u << 24)
#define FDCAN_RXF0S_RF0L        (1u << 25)
#define FDCAN_RXF1S_F1FL_Pos    0u
#define FDCAN_RXF1S_F1FL_Msk    (0x7Fu << FDCAN_RXF1S_F1FL_Pos)
#define FDCAN_RXF1S_F1GI_Pos    8u
#define FDCAN_RXF1S_F1GI_Msk    (0x3Fu << FDCAN_RXF1S_F1GI_Pos)
#define FDCAN_RXF1S_F1PI_Pos    16u
#define FDCAN_RXF1S_F1PI_Msk    (0x3Fu << FDCAN_RXF1S_F1PI_Pos)
#define FDCAN_RXF1S_F1F         (1u << 24)
#define FDCAN_RXF1S_RF1L        (1u << 25)

/* RXESC：元素数据区尺寸编码 0..7 -> 8/12/16/20/24/32/48/64 字节 */
#define FDCAN_RXESC_F0DS_Pos    0u
#define FDCAN_RXESC_F0DS_Msk    (0x7u << FDCAN_RXESC_F0DS_Pos)
#define FDCAN_RXESC_F1DS_Pos    4u
#define FDCAN_RXESC_F1DS_Msk    (0x7u << FDCAN_RXESC_F1DS_Pos)
#define FDCAN_RXESC_RBDS_Pos    8u
#define FDCAN_RXESC_RBDS_Msk    (0x7u << FDCAN_RXESC_RBDS_Pos)
#define FDCAN_DLC_TO_DS(dlc)    (0x7u)

/* TXBC / TXFQS / TXESC */
#define FDCAN_TXBC_TBSA_Pos     2u
#define FDCAN_TXBC_TBSA_Msk     (0x3FFFu << FDCAN_TXBC_TBSA_Pos)
#define FDCAN_TXBC_NDTB_Pos     16u
#define FDCAN_TXBC_NDTB_Msk     (0x3Fu << FDCAN_TXBC_NDTB_Pos)
#define FDCAN_TXBC_TFQS_Pos     24u
#define FDCAN_TXBC_TFQS_Msk     (0x3Fu << FDCAN_TXBC_TFQS_Pos)
#define FDCAN_TXBC_TFQM         (1u << 30)
#define FDCAN_TXFQS_TFFL_Pos    0u
#define FDCAN_TXFQS_TFFL_Msk    (0x3Fu << FDCAN_TXFQS_TFFL_Pos)
#define FDCAN_TXFQS_TFGI_Pos    8u
#define FDCAN_TXFQS_TFGI_Msk    (0x1Fu << FDCAN_TXFQS_TFGI_Pos)
#define FDCAN_TXFQS_TFQPI_Pos   16u
#define FDCAN_TXFQS_TFQPI_Msk   (0x1Fu << FDCAN_TXFQS_TFQPI_Pos)
#define FDCAN_TXFQS_TFQF        (1u << 21)
#define FDCAN_TXESC_TBDS_Pos    0u
#define FDCAN_TXESC_TBDS_Msk    (0x7u << FDCAN_TXESC_TBDS_Pos)

/* TXEFC / TXEFS */
#define FDCAN_TXEFC_EFSA_Pos    2u
#define FDCAN_TXEFC_EFSA_Msk    (0x3FFFu << FDCAN_TXEFC_EFSA_Pos)
#define FDCAN_TXEFC_EFS_Pos     16u
#define FDCAN_TXEFC_EFS_Msk     (0x3Fu << FDCAN_TXEFC_EFS_Pos)
#define FDCAN_TXEFC_EFWM_Pos    24u
#define FDCAN_TXEFC_EFWM_Msk    (0x3Fu << FDCAN_TXEFC_EFWM_Pos)
#define FDCAN_TXEFS_EFFL_Pos    0u
#define FDCAN_TXEFS_EFFL_Msk    (0x3Fu << FDCAN_TXEFS_EFFL_Pos)

/* IR / IE / ILS / ILE 公共位定义 */
#define FDCAN_IR_RF0N           (1u << 0)
#define FDCAN_IR_RF0W           (1u << 1)
#define FDCAN_IR_RF0F           (1u << 2)
#define FDCAN_IR_RF0L           (1u << 3)
#define FDCAN_IR_RF1N           (1u << 4)
#define FDCAN_IR_RF1W           (1u << 5)
#define FDCAN_IR_RF1F           (1u << 6)
#define FDCAN_IR_RF1L           (1u << 7)
#define FDCAN_IR_HPM            (1u << 8)
#define FDCAN_IR_TC             (1u << 9)
#define FDCAN_IR_TCF            (1u << 10)
#define FDCAN_IR_TFE            (1u << 11)
#define FDCAN_IR_TEFN           (1u << 12)
#define FDCAN_IR_TEFW           (1u << 13)
#define FDCAN_IR_TEFF           (1u << 14)
#define FDCAN_IR_TEFL           (1u << 15)
#define FDCAN_IR_TSW            (1u << 16)
#define FDCAN_IR_MRAF           (1u << 17)
#define FDCAN_IR_TOO            (1u << 18)
#define FDCAN_IR_DRX            (1u << 19)
#define FDCAN_IR_BEC            (1u << 20)
#define FDCAN_IR_BEU            (1u << 21)
#define FDCAN_IR_ELO            (1u << 22)
#define FDCAN_IR_EP             (1u << 23)
#define FDCAN_IR_EW             (1u << 24)
#define FDCAN_IR_BO             (1u << 25)
#define FDCAN_IR_WDI            (1u << 26)
#define FDCAN_IR_PEA            (1u << 27)
#define FDCAN_IR_PED            (1u << 28)
#define FDCAN_IR_ARA            (1u << 29)

/* ECR */
#define FDCAN_ECR_TEC_Pos       0u
#define FDCAN_ECR_TEC_Msk       (0xFFu << FDCAN_ECR_TEC_Pos)
#define FDCAN_ECR_REC_Pos       8u
#define FDCAN_ECR_REC_Msk       (0x7Fu << FDCAN_ECR_REC_Pos)
#define FDCAN_ECR_RP            (1u << 16)
#define FDCAN_ECR_CEL_Pos       16u
#define FDCAN_ECR_CEL_Msk       (0xFFu << FDCAN_ECR_CEL_Pos)

/* PSR */
#define FDCAN_PSR_LEC_Pos       0u
#define FDCAN_PSR_LEC_Msk       (0x7u << FDCAN_PSR_LEC_Pos)
#define FDCAN_PSR_ACT_Pos       3u
#define FDCAN_PSR_ACT_Msk       (0x3u << FDCAN_PSR_ACT_Pos)
#define FDCAN_PSR_EP            (1u << 5)
#define FDCAN_PSR_EW            (1u << 6)
#define FDCAN_PSR_BO            (1u << 7)
#define FDCAN_PSR_DLEC_Pos      8u
#define FDCAN_PSR_DLEC_Msk      (0x7u << FDCAN_PSR_DLEC_Pos)
#define FDCAN_PSR_RESI          (1u << 11)
#define FDCAN_PSR_RBRS          (1u << 12)
#define FDCAN_PSR_REDL          (1u << 13)
#define FDCAN_PSR_PXE           (1u << 14)
#define FDCAN_PSR_TDCV_Pos      16u
#define FDCAN_PSR_TDCV_Msk      (0x7Fu << FDCAN_PSR_TDCV_Pos)

/* PSR.LEC 取值 */
#define FDCAN_LEC_NO_ERROR      0u
#define FDCAN_LEC_STUFF         1u
#define FDCAN_LEC_FORM          2u
#define FDCAN_LEC_ACK           3u
#define FDCAN_LEC_BIT1          4u
#define FDCAN_LEC_BIT0          5u
#define FDCAN_LEC_CRC           6u
#define FDCAN_LEC_NO_CHANGE     7u

/* 过滤器元素配置（SFEC/EFEC，3 bit） */
#define FDCAN_FEC_DISABLE       0u
#define FDCAN_FEC_FIFO0         1u
#define FDCAN_FEC_FIFO1         2u
#define FDCAN_FEC_REJECT        3u
#define FDCAN_FEC_PRIORITY      4u
#define FDCAN_FEC_FIFO0_PRIO    5u
#define FDCAN_FEC_FIFO1_PRIO    6u
#define FDCAN_FEC_RXBUFFER      7u

/* 过滤器类型（SFT/EFT） */
#define FDCAN_FT_RANGE          0u   /* 范围过滤，扩展帧时应用 XIDAM */
#define FDCAN_FT_DUAL           1u   /* 双 ID 过滤                    */
#define FDCAN_FT_MASK           2u   /* 经典过滤：ID1 = 过滤器，ID2 = 掩码 */
#define FDCAN_FT_RANGE_NO_EIDM  3u   /* 范围过滤，不应用 XIDAM          */

/* 标准过滤器元素位域：W0 = SFT[31:30] | SFEC[29:27] | SFID1[26:16] | SFID2[15:0] */
#define FDCAN_SFT_Pos           30u
#define FDCAN_SFT_Msk           (0x3u << FDCAN_SFT_Pos)
#define FDCAN_SFEC_Pos          27u
#define FDCAN_SFEC_Msk          (0x7u << FDCAN_SFEC_Pos)
#define FDCAN_SFID1_Pos         16u
#define FDCAN_SFID1_Msk         (0x7FFu << FDCAN_SFID1_Pos)
#define FDCAN_SFID2_Msk         0x7FFu

/* 扩展过滤器元素位域：F0 = EFEC[31:29] | EFID1[28:0]；F1 = EFT[31:30] | EFID2[28:0] */
#define FDCAN_EFEC_Pos          29u
#define FDCAN_EFEC_Msk          (0x7u << FDCAN_EFEC_Pos)
#define FDCAN_EFID1_Msk         0x1FFFFFFFu
#define FDCAN_EFT_Pos           30u
#define FDCAN_EFT_Msk           (0x3u << FDCAN_EFT_Pos)
#define FDCAN_EFID2_Msk         0x1FFFFFFFu

/* RX 元素（数据区 64 字节时占 18 word） */
#define FDCAN_RX_W0_ESI         (1u << 31)
#define FDCAN_RX_W0_XTD         (1u << 30)
#define FDCAN_RX_W0_RTR         (1u << 29)
#define FDCAN_RX_W0_ID_Msk      0x1FFFFFFFu
#define FDCAN_RX_W1_ANMF        (1u << 31)
#define FDCAN_RX_W1_FIDX_Pos    24u
#define FDCAN_RX_W1_FIDX_Msk    (0x7Fu << FDCAN_RX_W1_FIDX_Pos)
#define FDCAN_RX_W1_FDF         (1u << 21)
#define FDCAN_RX_W1_BRS         (1u << 20)
#define FDCAN_RX_W1_DLC_Pos     16u
#define FDCAN_RX_W1_DLC_Msk     (0xFu << FDCAN_RX_W1_DLC_Pos)
#define FDCAN_RX_W1_RXTS_Msk    0xFFFFu

/* TX 元素（与 RX 元素同构，W1 使用 EFC/TMM 语义） */
#define FDCAN_TX_W1_EFC         (1u << 23)
#define FDCAN_TX_W1_FDF         (1u << 21)
#define FDCAN_TX_W1_BRS         (1u << 20)
#define FDCAN_TX_W1_DLC_Pos     16u
#define FDCAN_TX_W1_DLC_Msk     (0xFu << FDCAN_TX_W1_DLC_Pos)
#define FDCAN_TX_W1_MM_Pos      24u
#define FDCAN_TX_W1_MM_Msk      (0xFFu << FDCAN_TX_W1_MM_Pos)

/* 消息 RAM 元素尺寸（word）：2 word 头 + 数据区 word 数 */
#define FDCAN_ELEM_HDR_WORDS    2u
#define FDCAN_DATA_BYTES_TO_WORDS(b) (((uint32_t)(b) + 3u) / 4u)
#define FDCAN_ELEM_WORDS_64B    18u
#define FDCAN_STD_FILTER_WORDS  1u
#define FDCAN_EXT_FILTER_WORDS  2u

/* ==========================================================================
 * 5. RCC
 * ========================================================================== */
#define RCC_CR                  0x000u
#define RCC_ICSCR               0x004u
#define RCC_CRRCR               0x008u
#define RCC_CFGR                0x010u
#define RCC_D1CFGR              0x018u
#define RCC_D2CFGR              0x01Cu
#define RCC_D3CFGR              0x020u
#define RCC_PLLCKSELR           0x028u
#define RCC_PLLCFGR             0x02Cu
#define RCC_PLL1DIVR            0x030u
#define RCC_PLL2DIVR            0x038u
#define RCC_PLL3DIVR            0x040u
#define RCC_D1CCIPR             0x04Cu
#define RCC_D2CCIP1R            0x050u
#define RCC_D2CCIP2R            0x054u
#define RCC_D3CCIPR             0x058u
#define RCC_BDCR                0x070u
#define RCC_CSR                 0x074u
#define RCC_AHB3RSTR            0x07Cu
#define RCC_AHB1RSTR            0x080u
#define RCC_AHB2RSTR            0x084u
#define RCC_AHB4RSTR            0x088u
#define RCC_APB3RSTR            0x08Cu
#define RCC_APB1LRSTR           0x090u
#define RCC_APB1HRSTR           0x094u
#define RCC_APB2RSTR            0x098u
#define RCC_APB4RSTR            0x09Cu
#define RCC_GCR                 0x0A0u
#define RCC_D3AMR               0x0A8u
#define RCC_RSR                 0x0D0u
#define RCC_AHB3ENR             0x0D4u
#define RCC_AHB1ENR             0x0D8u
#define RCC_AHB2ENR             0x0DCu
#define RCC_AHB4ENR             0x0E0u
#define RCC_APB3ENR             0x0E4u
#define RCC_APB1LENR            0x0E8u
#define RCC_APB1HENR            0x0ECu
#define RCC_APB2ENR             0x0F0u
#define RCC_APB4ENR             0x0F4u

#define RCC_CR_HSION            (1u << 0)
#define RCC_CR_HSIRDY           (1u << 2)
#define RCC_CR_HSIDIV_Pos       3u
#define RCC_CR_HSIDIV_Msk       (0x3u << RCC_CR_HSIDIV_Pos)
#define RCC_CR_CSION            (1u << 7)
#define RCC_CR_CSIRDY           (1u << 8)
#define RCC_CR_HSI48ON          (1u << 12)
#define RCC_CR_HSI48RDY         (1u << 13)
#define RCC_CR_HSEON            (1u << 16)
#define RCC_CR_HSERDY           (1u << 17)
#define RCC_CR_HSEBYP           (1u << 18)
#define RCC_CR_HSEEXT           (1u << 20)
#define RCC_CR_PLL1ON           (1u << 24)
#define RCC_CR_PLL1RDY          (1u << 25)
#define RCC_CR_PLL2ON           (1u << 26)
#define RCC_CR_PLL2RDY          (1u << 27)
#define RCC_CR_PLL3ON           (1u << 28)
#define RCC_CR_PLL3RDY          (1u << 29)

#define RCC_CFGR_SW_Pos         0u
#define RCC_CFGR_SW_Msk         (0x7u << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SW_HSI         0u
#define RCC_CFGR_SW_CSI         1u
#define RCC_CFGR_SW_HSE         2u
#define RCC_CFGR_SW_PLL1        3u
#define RCC_CFGR_SWS_Pos        3u
#define RCC_CFGR_SWS_Msk        (0x7u << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_STOPWUCK       (1u << 6)

/* 域预分频（4 位字段，用于 CPU/AHB 预分频）：0b1000 = /2, 0b1001 = /4, 0b1010 = /8 */
#define RCC_PRESC_DIV1          0x0u
#define RCC_PRESC_DIV2          0x8u
#define RCC_PRESC_DIV4          0x9u
#define RCC_PRESC_DIV8          0xAu
#define RCC_PRESC_DIV16         0xBu

/* APB 预分频（3 位字段 DxPPRE）：0b100 = /2, 0b101 = /4, 0b110 = /8, 0b111 = /16 */
#define RCC_PPRE_DIV1           0x0u
#define RCC_PPRE_DIV2           0x4u
#define RCC_PPRE_DIV4           0x5u
#define RCC_PPRE_DIV8           0x6u
#define RCC_PPRE_DIV16          0x7u

#define RCC_D1CFGR_HPRE_Pos     0u
#define RCC_D1CFGR_HPRE_Msk     (0xFu << RCC_D1CFGR_HPRE_Pos)
#define RCC_D1CFGR_D1PPRE_Pos   4u
#define RCC_D1CFGR_D1PPRE_Msk   (0x7u << RCC_D1CFGR_D1PPRE_Pos)
#define RCC_D1CFGR_D1CPRE_Pos   8u
#define RCC_D1CFGR_D1CPRE_Msk   (0xFu << RCC_D1CFGR_D1CPRE_Pos)
#define RCC_D2CFGR_D2PPRE1_Pos  4u
#define RCC_D2CFGR_D2PPRE1_Msk  (0x7u << RCC_D2CFGR_D2PPRE1_Pos)
#define RCC_D2CFGR_D2PPRE2_Pos  8u
#define RCC_D2CFGR_D2PPRE2_Msk  (0x7u << RCC_D2CFGR_D2PPRE2_Pos)
#define RCC_D3CFGR_D3PPRE_Pos   4u
#define RCC_D3CFGR_D3PPRE_Msk   (0x7u << RCC_D3CFGR_D3PPRE_Pos)

#define RCC_PLLCKSELR_PLLSRC_Pos 0u
#define RCC_PLLCKSELR_PLLSRC_Msk (0x3u << RCC_PLLCKSELR_PLLSRC_Pos)
#define RCC_PLLCKSELR_PLLSRC_HSE 2u
#define RCC_PLLCKSELR_DIVM1_Pos  4u
#define RCC_PLLCKSELR_DIVM1_Msk  (0x3Fu << RCC_PLLCKSELR_DIVM1_Pos)
#define RCC_PLLCKSELR_DIVM2_Pos  8u
#define RCC_PLLCKSELR_DIVM3_Pos  12u

#define RCC_PLLCFGR_PLL1FRACEN  (1u << 0)
#define RCC_PLLCFGR_PLL1VCOSEL  (1u << 1)
#define RCC_PLLCFGR_PLL1RGE_Pos 2u
#define RCC_PLLCFGR_PLL1RGE_Msk (0x3u << RCC_PLLCFGR_PLL1RGE_Pos)
#define RCC_PLLCFGR_DIVP1EN     (1u << 16)
#define RCC_PLLCFGR_DIVQ1EN     (1u << 17)
#define RCC_PLLCFGR_DIVR1EN     (1u << 18)

#define RCC_PLL1DIVR_DIVN_Pos   0u
#define RCC_PLL1DIVR_DIVN_Msk   (0x1FFu << RCC_PLL1DIVR_DIVN_Pos)
#define RCC_PLL1DIVR_DIVP_Pos   9u
#define RCC_PLL1DIVR_DIVP_Msk   (0x7Fu << RCC_PLL1DIVR_DIVP_Pos)
#define RCC_PLL1DIVR_DIVQ_Pos   16u
#define RCC_PLL1DIVR_DIVQ_Msk   (0x7Fu << RCC_PLL1DIVR_DIVQ_Pos)
#define RCC_PLL1DIVR_DIVR_Pos   24u
#define RCC_PLL1DIVR_DIVR_Msk   (0x7Fu << RCC_PLL1DIVR_DIVR_Pos)

#define RCC_D1CCIPR_FMCSEL_Pos  0u
#define RCC_D1CCIPR_FMCSEL_Msk  (0x3u << RCC_D1CCIPR_FMCSEL_Pos)
#define RCC_D1CCIPR_FMCSEL_HCLK3 0u
#define RCC_D1CCIPR_FMCSEL_PLL1Q 1u
#define RCC_D1CCIPR_FMCSEL_PLL2R 2u
#define RCC_D1CCIPR_QSPISEL_Pos 4u
#define RCC_D1CCIPR_QSPISEL_Msk (0x3u << RCC_D1CCIPR_QSPISEL_Pos)
#define RCC_D1CCIPR_QSPISEL_HCLK3 0u
#define RCC_D1CCIPR_QSPISEL_PLL1Q 1u
#define RCC_D1CCIPR_QSPISEL_PLL2R 2u
#define RCC_D1CCIPR_SDMMCSEL    (1u << 16)
#define RCC_D2CCIP1R_FDCANSEL_Pos 28u
#define RCC_D2CCIP1R_FDCANSEL_Msk (0x3u << RCC_D2CCIP1R_FDCANSEL_Pos)
#define RCC_D2CCIP1R_FDCANSEL_HSE 0u
#define RCC_D2CCIP1R_FDCANSEL_PLL1Q 1u
#define RCC_D2CCIP1R_FDCANSEL_PLL2Q 2u

/* 时钟使能位 */
#define RCC_AHB1ENR_DMA1EN      (1u << 0)
#define RCC_AHB1ENR_DMA2EN      (1u << 1)
#define RCC_AHB1ENR_ETH1MACEN   (1u << 15)
#define RCC_AHB1ENR_ETH1TXEN    (1u << 16)
#define RCC_AHB1ENR_ETH1RXEN    (1u << 17)
#define RCC_AHB3ENR_FMCEN       (1u << 12)
#define RCC_AHB3ENR_QSPIEN      (1u << 14)
#define RCC_AHB1ENR_SDMMC1EN    (1u << 16)
#define RCC_AHB4ENR_GPIOAEN     (1u << 0)
#define RCC_AHB4ENR_GPIOBEN     (1u << 1)
#define RCC_AHB4ENR_GPIOCEN     (1u << 2)
#define RCC_AHB4ENR_GPIODEN     (1u << 3)
#define RCC_AHB4ENR_GPIOEEN     (1u << 4)
#define RCC_AHB4ENR_GPIOFEN     (1u << 5)
#define RCC_AHB4ENR_GPIOGEN     (1u << 6)
#define RCC_AHB4ENR_BKPRAMEN    (1u << 28)
#define RCC_APB3ENR_LTDCEN      (1u << 3)
#define RCC_APB1LENR_TIM2EN     (1u << 0)
#define RCC_APB1LENR_I2C4EN     (1u << 24)
#define RCC_APB1HENR_FDCANEN    (1u << 8)
#define RCC_APB2ENR_USART1EN    (1u << 4)

/* ==========================================================================
 * 6. PWR
 * ========================================================================== */
#define PWR_CR1                 0x000u
#define PWR_CSR1                0x004u
#define PWR_CR3                 0x00Cu
#define PWR_CPUCR               0x010u
#define PWR_D3CR                0x018u
#define PWR_CR1_VOS_Pos         14u
#define PWR_CR1_VOS_Msk         (0x3u << PWR_CR1_VOS_Pos)
#define PWR_D3CR_VOS_Pos        14u
#define PWR_D3CR_VOS_Msk        (0x3u << PWR_D3CR_VOS_Pos)
#define PWR_D3CR_VOSRDY         (1u << 13)
#define PWR_CR3_BYPASS          (1u << 0)
#define PWR_CR3_LDOEN           (1u << 1)
#define PWR_CR3_SCUEN           (1u << 2)

/* ==========================================================================
 * 7. FLASH 接口（等待周期 ACR）
 * ========================================================================== */
#define FLASH_ACR               0x000u
#define FLASH_ACR_LATENCY_Pos   0u
#define FLASH_ACR_LATENCY_Msk   (0xFu << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_WRHIGHFREQ_Pos 4u
#define FLASH_ACR_WRHIGHFREQ_Msk (0x3u << FLASH_ACR_WRHIGHFREQ_Pos)

/* ==========================================================================
 * 8. GPIO
 * ========================================================================== */
#define GPIO_MODER              0x00u
#define GPIO_OTYPER             0x04u
#define GPIO_OSPEEDR            0x08u
#define GPIO_PUPDR              0x0Cu
#define GPIO_IDR                0x10u
#define GPIO_ODR                0x14u
#define GPIO_BSRR               0x18u
#define GPIO_LCKR               0x1Cu
#define GPIO_AFRL               0x20u
#define GPIO_AFRH               0x24u
#define GPIO_MODE_INPUT         0u
#define GPIO_MODE_OUTPUT        1u
#define GPIO_MODE_AF            2u
#define GPIO_MODE_ANALOG        3u
#define GPIO_OSPEED_LOW         0u
#define GPIO_OSPEED_MEDIUM      1u
#define GPIO_OSPEED_HIGH        2u
#define GPIO_OSPEED_VERYHIGH    3u

/* ==========================================================================
 * 9. FMC（SDRAM，Bank5/6 寄存器区）
 * ========================================================================== */
#define FMC_SDCR1               0x140u
#define FMC_SDCR2               0x144u
#define FMC_SDTR1               0x148u
#define FMC_SDTR2               0x14Cu
#define FMC_SDCMR               0x150u
#define FMC_SDRTR               0x154u
#define FMC_SDSR                0x158u

#define FMC_SDCR_NC_Pos         0u    /* 列位数 - 8      */
#define FMC_SDCR_NC_Msk         (0x3u << FMC_SDCR_NC_Pos)
#define FMC_SDCR_NR_Pos         2u    /* 行位数 - 11     */
#define FMC_SDCR_NR_Msk         (0x3u << FMC_SDCR_NR_Pos)
#define FMC_SDCR_MWID_Pos       4u    /* 数据宽度 0=8b 1=16b 2=32b */
#define FMC_SDCR_MWID_Msk       (0x3u << FMC_SDCR_MWID_Pos)
#define FMC_SDCR_MWID_8         0u
#define FMC_SDCR_MWID_16        1u
#define FMC_SDCR_MWID_32        2u
#define FMC_SDCR_NB             (1u << 6)   /* 内部 bank 数：0=1 bank,1=4 bank */
#define FMC_SDCR_CAS_Pos        7u          /* CAS 潜伏期 - 1 */
#define FMC_SDCR_CAS_Msk        (0x3u << FMC_SDCR_CAS_Pos)
#define FMC_SDCR_WP             (1u << 9)
#define FMC_SDCR_SDCLK_Pos      10u         /* 00=HCLK, 01=HCLK/2, 10=HCLK/3 */
#define FMC_SDCR_SDCLK_Msk      (0x3u << FMC_SDCR_SDCLK_Pos)
#define FMC_SDCR_SDCLK_DIV1     0u
#define FMC_SDCR_SDCLK_DIV2     1u
#define FMC_SDCR_SDCLK_DIV3     2u
#define FMC_SDCR_RBURST         (1u << 12)
#define FMC_SDCR_RPIPE_Pos      13u
#define FMC_SDCR_RPIPE_Msk      (0x3u << FMC_SDCR_RPIPE_Pos)

/* SDTR 字段均为"周期数 - 1" */
#define FMC_SDTR_TMRD_Pos       0u
#define FMC_SDTR_TMRD_Msk       (0xFu << FMC_SDTR_TMRD_Pos)
#define FMC_SDTR_TXSR_Pos       4u
#define FMC_SDTR_TXSR_Msk       (0xFu << FMC_SDTR_TXSR_Pos)
#define FMC_SDTR_TRAS_Pos       8u
#define FMC_SDTR_TRAS_Msk       (0xFu << FMC_SDTR_TRAS_Pos)
#define FMC_SDTR_TRC_Pos        12u
#define FMC_SDTR_TRC_Msk        (0xFu << FMC_SDTR_TRC_Pos)
#define FMC_SDTR_TWR_Pos        16u
#define FMC_SDTR_TWR_Msk        (0xFu << FMC_SDTR_TWR_Pos)
#define FMC_SDTR_TRP_Pos        20u
#define FMC_SDTR_TRP_Msk        (0xFu << FMC_SDTR_TRP_Pos)
#define FMC_SDTR_TRCD_Pos       24u
#define FMC_SDTR_TRCD_Msk       (0xFu << FMC_SDTR_TRCD_Pos)

#define FMC_SDCMR_MODE_Pos      0u
#define FMC_SDCMR_MODE_Msk      (0x7u << FMC_SDCMR_MODE_Pos)
#define FMC_SDCMR_CTB2          (1u << 3)
#define FMC_SDCMR_CTB1          (1u << 4)
#define FMC_SDCMR_NRFS_Pos      5u
#define FMC_SDCMR_NRFS_Msk      (0xFu << FMC_SDCMR_NRFS_Pos)
#define FMC_SDCMR_MRD_Pos       9u
#define FMC_SDCMR_MRD_Msk       (0x1FFFu << FMC_SDCMR_MRD_Pos)

#define FMC_SDRTR_CRE           (1u << 0)
#define FMC_SDRTR_COUNT_Pos     1u
#define FMC_SDRTR_COUNT_Msk     (0x1FFFu << FMC_SDRTR_COUNT_Pos)
#define FMC_SDRTR_REIE          (1u << 14)

#define FMC_SDSR_BUSY           (1u << 5)
#define FMC_SDSR_MODES1_Pos     1u
#define FMC_SDSR_MODES1_Msk     (0x3u << FMC_SDSR_MODES1_Pos)

/* FMC SDRAM 命令模式（SDCMR.MODE） */
#define FMC_SDCMR_MODE_NORMAL       0u
#define FMC_SDCMR_MODE_CLK_ENABLE   1u
#define FMC_SDCMR_MODE_PALL         2u
#define FMC_SDCMR_MODE_AUTOREFRESH  3u
#define FMC_SDCMR_MODE_LOAD_MODE    4u
#define FMC_SDCMR_MODE_SELFREFRESH  5u
#define FMC_SDCMR_MODE_POWERDOWN    6u

/* SDRAM 模式寄存器（MRD 字段） */
#define SDR_MODE_BURST_LEN_1    0u
#define SDR_MODE_BURST_SEQ      (0u << 3)
#define SDR_MODE_CAS_Pos        4u
#define SDR_MODE_WB              (0u << 9)

/* ==========================================================================
 * 10. QUADSPI
 * ========================================================================== */
#define QSPI_CR                 0x00u
#define QSPI_DCR                0x04u
#define QSPI_SR                 0x08u
#define QSPI_FCR                0x0Cu
#define QSPI_DLR                0x10u
#define QSPI_CCR                0x14u
#define QSPI_AR                 0x18u
#define QSPI_ABR                0x1Cu
#define QSPI_DR                 0x20u
#define QSPI_PSMKR              0x24u
#define QSPI_PSMAR              0x28u
#define QSPI_PIR                0x2Cu
#define QSPI_LPTR               0x30u

#define QSPI_CR_EN              (1u << 0)
#define QSPI_CR_ABORT           (1u << 1)
#define QSPI_CR_DMAEN           (1u << 2)
#define QSPI_CR_TCEN            (1u << 3)
#define QSPI_CR_SSHIFT          (1u << 4)
#define QSPI_CR_FTHRES_Pos      8u
#define QSPI_CR_FTHRES_Msk      (0x1Fu << QSPI_CR_FTHRES_Pos)
#define QSPI_CR_TEIE            (1u << 16)
#define QSPI_CR_TCIE            (1u << 17)
#define QSPI_CR_FTIE            (1u << 18)
#define QSPI_CR_SMIE            (1u << 19)
#define QSPI_CR_PRESCALER_Pos   24u
#define QSPI_CR_PRESCALER_Msk   (0xFFu << QSPI_CR_PRESCALER_Pos)
#define QSPI_CR_PMM             (1u << 23)

#define QSPI_DCR_CKMODE         (1u << 0)
#define QSPI_DCR_CSHT_Pos       8u
#define QSPI_DCR_CSHT_Msk       (0x7u << QSPI_DCR_CSHT_Pos)
#define QSPI_DCR_FSIZE_Pos      16u
#define QSPI_DCR_FSIZE_Msk      (0x1Fu << QSPI_DCR_FSIZE_Pos)

#define QSPI_SR_BUSY            (1u << 5)
#define QSPI_SR_TOF             (1u << 4)
#define QSPI_SR_SMF             (1u << 3)
#define QSPI_SR_FTF             (1u << 2)
#define QSPI_SR_TCF             (1u << 1)
#define QSPI_SR_TEF             (1u << 0)
#define QSPI_FCR_CTEF           (1u << 0)
#define QSPI_FCR_CTCF           (1u << 1)
#define QSPI_FCR_CSMF           (1u << 3)
#define QSPI_FCR_CTOF           (1u << 4)

#define QSPI_CCR_INSTRUCTION_Pos 0u
#define QSPI_CCR_INSTRUCTION_Msk (0xFFu << QSPI_CCR_INSTRUCTION_Pos)
#define QSPI_CCR_IMODE_Pos      8u
#define QSPI_CCR_IMODE_Msk      (0x3u << QSPI_CCR_IMODE_Pos)
#define QSPI_CCR_ADMODE_Pos     10u
#define QSPI_CCR_ADMODE_Msk     (0x3u << QSPI_CCR_ADMODE_Pos)
#define QSPI_CCR_ADSIZE_Pos     12u
#define QSPI_CCR_ADSIZE_Msk     (0x3u << QSPI_CCR_ADSIZE_Pos)
#define QSPI_CCR_ABMODE_Pos     14u
#define QSPI_CCR_ABMODE_Msk     (0x3u << QSPI_CCR_ABMODE_Pos)
#define QSPI_CCR_ABSIZE_Pos     16u
#define QSPI_CCR_ABSIZE_Msk     (0x3u << QSPI_CCR_ABSIZE_Pos)
#define QSPI_CCR_DCYC_Pos       18u
#define QSPI_CCR_DCYC_Msk       (0x1Fu << QSPI_CCR_DCYC_Pos)
#define QSPI_CCR_DMODE_Pos      24u
#define QSPI_CCR_DMODE_Msk      (0x3u << QSPI_CCR_DMODE_Pos)
#define QSPI_CCR_FMODE_Pos      26u
#define QSPI_CCR_FMODE_Msk      (0x3u << QSPI_CCR_FMODE_Pos)
#define QSPI_CCR_DDRM           (1u << 31)
#define QSPI_CCR_DHHC           (1u << 30)
#define QSPI_CCR_SIOO           (1u << 28)

#define QSPI_CCR_IMODE_NONE     0u
#define QSPI_CCR_IMODE_1LINE    1u
#define QSPI_CCR_IMODE_2LINE    2u
#define QSPI_CCR_IMODE_4LINE    3u
#define QSPI_CCR_ADMODE_NONE    0u
#define QSPI_CCR_ADMODE_1LINE   1u
#define QSPI_CCR_ADMODE_2LINE   2u
#define QSPI_CCR_ADMODE_4LINE   3u
#define QSPI_CCR_DMODE_NONE     0u
#define QSPI_CCR_DMODE_1LINE    1u
#define QSPI_CCR_DMODE_2LINE    2u
#define QSPI_CCR_DMODE_4LINE    3u
#define QSPI_CCR_FMODE_INDWR    0u
#define QSPI_CCR_FMODE_INDRD    1u
#define QSPI_CCR_FMODE_MEMRD    3u
#define QSPI_CCR_ADSIZE_8       0u
#define QSPI_CCR_ADSIZE_16      1u
#define QSPI_CCR_ADSIZE_24      2u
#define QSPI_CCR_ADSIZE_32      3u

/* ==========================================================================
 * 11. SDMMC1
 * ========================================================================== */
#define SDMMC_POWER             0x00u
#define SDMMC_CLKCR             0x04u
#define SDMMC_ARG               0x08u
#define SDMMC_CMD               0x0Cu
#define SDMMC_RESPCMD           0x10u
#define SDMMC_RESP1             0x14u
#define SDMMC_DTIMER            0x24u
#define SDMMC_DLEN              0x28u
#define SDMMC_DCTRL             0x2Cu
#define SDMMC_DCOUNT            0x30u
#define SDMMC_STA               0x34u
#define SDMMC_ICR               0x38u
#define SDMMC_MASK              0x3Cu
#define SDMMC_ACKTIME           0x40u
#define SDMMC_IDMACTRL          0x50u
#define SDMMC_IDMABSIZE         0x54u
#define SDMMC_IDMABASE0         0x58u
#define SDMMC_IDMABASE1         0x5Cu
#define SDMMC_FIFO              0x80u

#define SDMMC_POWER_PWRCTRL_Pos 0u
#define SDMMC_POWER_PWRCTRL_Msk (0x3u << SDMMC_POWER_PWRCTRL_Pos)
#define SDMMC_CLKCR_CLKDIV_Pos  0u
#define SDMMC_CLKCR_CLKDIV_Msk  (0x3FFu << SDMMC_CLKCR_CLKDIV_Pos)
#define SDMMC_CLKCR_CLKEN       (1u << 8)
#define SDMMC_CLKCR_PWRSAV      (1u << 9)
#define SDMMC_CLKCR_WIDBUS_Pos  11u
#define SDMMC_CLKCR_WIDBUS_Msk  (0x3u << SDMMC_CLKCR_WIDBUS_Pos)
#define SDMMC_CLKCR_NEGEDGE     (1u << 13)
#define SDMMC_CLKCR_HWFC_EN     (1u << 14)
#define SDMMC_CMD_CMDINDEX_Pos  0u
#define SDMMC_CMD_CMDINDEX_Msk  (0x3Fu << SDMMC_CMD_CMDINDEX_Pos)
#define SDMMC_CMD_WAITRESP_Pos  6u
#define SDMMC_CMD_CPSMEN        (1u << 10)
#define SDMMC_STA_CCRCFAIL      (1u << 0)
#define SDMMC_STA_DCRCFAIL      (1u << 1)
#define SDMMC_STA_CTIMEOUT      (1u << 2)
#define SDMMC_STA_DTIMEOUT      (1u << 3)
#define SDMMC_STA_TXUNDERR      (1u << 4)
#define SDMMC_STA_RXOVERR       (1u << 5)
#define SDMMC_STA_CMDREND       (1u << 6)
#define SDMMC_STA_CMDSENT       (1u << 7)
#define SDMMC_STA_DATAEND       (1u << 8)
#define SDMMC_STA_DBCKEND       (1u << 10)
#define SDMMC_STA_CMDACT        (1u << 11)
#define SDMMC_STA_RXACT         (1u << 13)
#define SDMMC_DCTRL_DTEN        (1u << 0)
#define SDMMC_DCTRL_DTDIR       (1u << 1)
#define SDMMC_DCTRL_DTMODE_Pos  2u
#define SDMMC_DCTRL_DBLOCKSIZE_Pos 4u
#define SDMMC_DCTRL_DBLOCKSIZE_Msk (0xFu << SDMMC_DCTRL_DBLOCKSIZE_Pos)
#define SDMMC_IDMACTRL_IDMAEN   (1u << 0)

/* ==========================================================================
 * 12. 以太网 MAC（RMII）
 * ========================================================================== */
#define ETH_MACCR               0x0000u
#define ETH_MACECR              0x0004u
#define ETH_MACPFR              0x0008u
#define ETH_MACWTR              0x000Cu
#define ETH_MACHT0R             0x0010u
#define ETH_MACHT1R             0x0014u
#define ETH_MACISR              0x00B0u
#define ETH_MACIER              0x00B4u
#define ETH_MACVR               0x0110u
#define ETH_MACDR               0x0114u
#define ETH_MACMDIOAR           0x0200u
#define ETH_MACMDIODR           0x0204u
#define ETH_MACA0HR             0x0300u
#define ETH_MACA0LR             0x0304u
#define ETH_MTLCSR              0x0D00u

#define ETH_MACCR_TE            (1u << 0)
#define ETH_MACCR_RE            (1u << 1)
#define ETH_MACCR_DM            (1u << 13)
#define ETH_MACCR_FES           (1u << 14)
#define ETH_MACCR_PS            (1u << 15)
#define ETH_MACCR_IPC           (1u << 20)

#define ETH_MACPFR_RA           (1u << 31)
#define ETH_MACPFR_PCF_Pos      4u
#define ETH_MACPFR_DBF           (1u << 5)

#define ETH_MACMDIOAR_GB        (1u << 0)
#define ETH_MACMDIOAR_C45E      (1u << 1)
#define ETH_MACMDIOAR_GOC_Pos   2u
#define ETH_MACMDIOAR_GOC_Msk   (0x3u << ETH_MACMDIOAR_GOC_Pos)
#define ETH_MACMDIOAR_GOC_WRITE 1u
#define ETH_MACMDIOAR_GOC_READ  3u
#define ETH_MACMDIOAR_CR_Pos    8u
#define ETH_MACMDIOAR_CR_Msk    (0xFu << ETH_MACMDIOAR_CR_Pos)
#define ETH_MACMDIOAR_RDA_Pos   16u
#define ETH_MACMDIOAR_RDA_Msk   (0x1Fu << ETH_MACMDIOAR_RDA_Pos)
#define ETH_MACMDIOAR_PA_Pos    21u
#define ETH_MACMDIOAR_PA_Msk    (0x1Fu << ETH_MACMDIOAR_PA_Pos)
#define ETH_MACMDIOAR_MB        (1u << 31)
#define ETH_MACMDIODR_GD_Pos    0u
#define ETH_MACMDIODR_GD_Msk    (0xFFFFu << ETH_MACMDIODR_GD_Pos)
#define ETH_MACMDIODR_RA        (1u << 16)

/* DMA 通道寄存器（ETH DMA 基地址 = ETH_BASE + 0x1000） */
#define ETH_DMA_BASE_OFFSET     0x1000u
#define ETH_DMAMR               0x0000u
#define ETH_DMASBMR             0x0004u
#define ETH_DMACCR              0x0100u
#define ETH_DMATXCR             0x0110u
#define ETH_DMARXCR             0x0118u
#define ETH_DMATXDLAR           0x1114u
#define ETH_DMARXDLAR           0x111Cu
#define ETH_DMATXRLR            0x1128u
#define ETH_DMARXRLR            0x1130u
#define ETH_DMATXDRLR           0x112Cu
#define ETH_DMARXDRLR           0x1134u
#define ETH_DMATXDTAR           0x1110u
#define ETH_DMARXDTAR           0x1120u
#define ETH_DMACSR              0x1160u
#define ETH_DMACSR_TPS           (1u << 1)
#define ETH_DMACSR_RPS           (1u << 3)
#define ETH_DMACSR_TI            (1u << 0)
#define ETH_DMACSR_RI            (1u << 6)

/* ==========================================================================
 * 13. DMA（DMA1/DMA2，F4 风格 16 流控制器）与 DMAMUX1
 * ========================================================================== */
#define DMA_LISR                0x00u
#define DMA_HISR                0x04u
#define DMA_LIFCR               0x08u
#define DMA_HIFCR               0x0Cu
#define DMA_SxCR(n)             (0x10u + 0x18u * (uint32_t)(n))
#define DMA_SxNDTR(n)           (0x14u + 0x18u * (uint32_t)(n))
#define DMA_SxPAR(n)            (0x18u + 0x18u * (uint32_t)(n))
#define DMA_SxM0AR(n)           (0x1Cu + 0x18u * (uint32_t)(n))
#define DMA_SxM1AR(n)           (0x20u + 0x18u * (uint32_t)(n))
#define DMA_SxFCR(n)            (0x24u + 0x18u * (uint32_t)(n))

#define DMA_SxCR_EN             (1u << 0)
#define DMA_SxCR_DMEIE          (1u << 1)
#define DMA_SxCR_TEIE           (1u << 2)
#define DMA_SxCR_HTIE           (1u << 3)
#define DMA_SxCR_TCIE           (1u << 4)
#define DMA_SxCR_PFCTRL         (1u << 5)
#define DMA_SxCR_DIR_Pos        6u
#define DMA_SxCR_DIR_Msk        (0x3u << DMA_SxCR_DIR_Pos)
#define DMA_SxCR_DIR_P2M        0u
#define DMA_SxCR_DIR_M2P        1u
#define DMA_SxCR_DIR_M2M        2u
#define DMA_SxCR_CIRC           (1u << 8)
#define DMA_SxCR_PINC           (1u << 9)
#define DMA_SxCR_MINC           (1u << 10)
#define DMA_SxCR_PSIZE_Pos      11u
#define DMA_SxCR_PSIZE_Msk      (0x3u << DMA_SxCR_PSIZE_Pos)
#define DMA_SxCR_MSIZE_Pos      13u
#define DMA_SxCR_MSIZE_Msk      (0x3u << DMA_SxCR_MSIZE_Pos)
#define DMA_SxCR_PL_Pos         16u
#define DMA_SxCR_PL_Msk         (0x3u << DMA_SxCR_PL_Pos)
#define DMA_SxCR_PFCTRL_BIT     5u

#define DMAMUX1_CCR(n)          (0x00u + 0x4u * (uint32_t)(n))
#define DMAMUX1_CSR(n)          (0x80u + 0x4u * (uint32_t)(n))
#define DMAMUX1_CCR_DMAREQ_ID_Pos 0u
#define DMAMUX1_CCR_DMAREQ_ID_Msk (0xFFu << DMAMUX1_CCR_DMAREQ_ID_Pos)
#define DMAMUX1_CCR_SOIE        (1u << 8)
#define DMAMUX1_CCR_EGE         (1u << 9)

/* DMAMUX1 请求编号（本板使用） */
#define DMAMUX_REQ_ETH1_RX      0u
#define DMAMUX_REQ_ETH1_TX      1u
#define DMAMUX_REQ_TIM2_CH1     6u
#define DMAMUX_REQ_SPI1_RX      37u
#define DMAMUX_REQ_SPI1_TX      38u
#define DMAMUX_REQ_USART1_RX    41u
#define DMAMUX_REQ_USART1_TX    42u
#define DMAMUX_REQ_I2C4_RX      49u
#define DMAMUX_REQ_I2C4_TX      50u
#define DMAMUX_REQ_SDMMC1       55u

/* ==========================================================================
 * 14. TIM2 / I2C4 / USART1（子集）
 * ========================================================================== */
#define TIM_CR1                 0x00u
#define TIM_CR2                 0x04u
#define TIM_SMCR                0x08u
#define TIM_DIER                0x0Cu
#define TIM_SR                  0x10u
#define TIM_EGR                 0x14u
#define TIM_CCMR1               0x18u
#define TIM_CCMR2               0x1Cu
#define TIM_CCER                0x20u
#define TIM_CNT                 0x24u
#define TIM_PSC                 0x28u
#define TIM_ARR                 0x2Cu
#define TIM_CCR1                0x34u
#define TIM_CR1_CEN             (1u << 0)
#define TIM_CR1_ARPE            (1u << 7)
#define TIM_CCMR1_OC1M_Pos      4u
#define TIM_CCMR1_OC1M_Msk      (0x7u << TIM_CCMR1_OC1M_Pos)
#define TIM_CCMR1_OC1M_PWM1     6u
#define TIM_CCMR1_OC1PE         (1u << 3)
#define TIM_CCER_CC1E           (1u << 0)
#define TIM_EGR_UG              (1u << 0)

#define I2C_CR1                 0x00u
#define I2C_CR2                 0x04u
#define I2C_OAR1                0x08u
#define I2C_TIMINGR             0x10u
#define I2C_TIMEOUTR            0x14u
#define I2C_ISR                 0x18u
#define I2C_ICR                 0x1Cu
#define I2C_RXDR                0x24u
#define I2C_TXDR                0x28u
#define I2C_CR1_PE              (1u << 0)
#define I2C_CR2_START           (1u << 13)
#define I2C_CR2_STOP            (1u << 14)
#define I2C_CR2_RD_WRN          (1u << 10)
#define I2C_CR2_NBYTES_Pos      16u
#define I2C_CR2_NBYTES_Msk      (0xFFu << I2C_CR2_NBYTES_Pos)
#define I2C_CR2_AUTOEND         (1u << 25)
#define I2C_ISR_TXE             (1u << 0)
#define I2C_ISR_TXIS            (1u << 1)
#define I2C_ISR_RXNE            (1u << 2)
#define I2C_ISR_NACKF           (1u << 4)
#define I2C_ISR_STOPF           (1u << 5)
#define I2C_ISR_TC              (1u << 6)
#define I2C_ISR_BUSY            (1u << 15)

#define USART_CR1               0x00u
#define USART_CR2               0x04u
#define USART_CR3               0x08u
#define USART_BRR               0x0Cu
#define USART_ISR               0x1Cu
#define USART_ICR               0x20u
#define USART_RDR               0x24u
#define USART_TDR               0x28u
#define USART_CR1_UE            (1u << 0)
#define USART_CR1_TE            (1u << 3)
#define USART_CR1_RE            (1u << 2)
#define USART_ISR_TXE           (1u << 7)
#define USART_ISR_TC            (1u << 6)
#define USART_ISR_RXNE          (1u << 5)

/* ==========================================================================
 * 15. LTDC（子集：用于 LCD 诊断界面）
 * ========================================================================== */
#define LTDC_SSCR               0x0008u
#define LTDC_BPCR               0x000Cu
#define LTDC_AWCR               0x0010u
#define LTDC_TWCR               0x0014u
#define LTDC_GCR                0x0018u
#define LTDC_SRCR               0x0024u
#define LTDC_L1CR               0x0084u
#define LTDC_L1WHPCR            0x0088u
#define LTDC_L1WVPCR            0x008Cu
#define LTDC_L1PFCR             0x0094u
#define LTDC_L1CFBAR            0x00ACu
#define LTDC_L1CFBLR            0x00B0u
#define LTDC_L1CFBLNR           0x00B4u
#define LTDC_L1CREN             (1u << 0)
#define LTDC_GCR_LTDCEN         (1u << 0)
#define LTDC_L1PFCR_RGB565      0x2u
#define LTDC_SRCR_VBR           (1u << 1)
#define LTDC_SRCR_IMR           (1u << 0)

#endif /* H750_STM32H750_REGS_H */
