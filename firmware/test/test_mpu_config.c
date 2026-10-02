/*
 * test_mpu_config.c
 * ---------------------------------------------------------------------------
 * MPU 区域配置测试：size 编码、RBAR/RASR 位域、区域表合法性、
 * 可缓存性判定、寄存器落地值。
 */
#include <string.h>
#include "test_framework.h"
#include "mpu_config.h"
#include "hal_stub.h"

/* 手工推导的期望值（见 docs/CACHE_COHERENCY.md 的编码计算过程） */
#define EXP_RBAR_AXI     0x24000013u   /* 0x24000000 | VALID | region 3 */
#define EXP_RASR_AXI     0x03070025u   /* XN=0 AP=3 TEX=0 S=1 C=1 B=1 SIZE=18 EN=1 */
#define EXP_RBAR_SRAM3   0x30040015u
#define EXP_RASR_SRAM3   0x130C001Du   /* XN=1 AP=3 TEX=1 S=1 SIZE=14 EN=1 */
#define EXP_RBAR_PERIPH  0x4000001Bu
#define EXP_RASR_PERIPH  0x13050039u   /* Device：TEX=0 S=1 B=1 SIZE=28 */
#define EXP_RBAR_SDRAM   0xC0000019u
#define EXP_RASR_SDRAM   0x030B002Fu   /* WB no WA：TEX=1 C=1 B=1 S=0 SIZE=23 */
#define EXP_RBAR_FLASH   0x08000011u
#define EXP_RASR_FLASH   0x06020021u   /* RO/RO AP=6 C=1 SIZE=16 */
#define EXP_RBAR_ITCM    0x00000010u
#define EXP_RASR_ITCM    0x030C001Fu   /* SIZE=15 (64KB) */
#define EXP_RBAR_SDRAMNC 0xC100001Au
#define EXP_RASR_SDRAMNC 0x130C0027u   /* SIZE=19 (1MB) */

void test_mpu_config(void)
{
    uint32_t field = 0u;
    mpu_stats_t st;
    uint32_t i;

    tf_suite_begin("MPU 区域配置与 RBAR/RASR 编码");

    /* ---- 1. SIZE 字段编码：SIZE = log2(size) - 1 ---- */
    TF_ASSERT_EQ(mpu_encode_size(32u, &field), 0);
    TF_ASSERT_EQ(field, 4u);          /* 最小区域 32B */
    TF_ASSERT_EQ(mpu_encode_size(4096u, &field), 0);
    TF_ASSERT_EQ(field, 11u);
    TF_ASSERT_EQ(mpu_encode_size(65536u, &field), 0);
    TF_ASSERT_EQ(field, 15u);
    TF_ASSERT_EQ(mpu_encode_size(524288u, &field), 0);
    TF_ASSERT_EQ(field, 18u);         /* AXI SRAM 512KB */
    TF_ASSERT_EQ(mpu_encode_size(16u * 1024u * 1024u, &field), 0);
    TF_ASSERT_EQ(field, 23u);         /* SDRAM 16MB */
    TF_ASSERT_EQ(mpu_encode_size(512u * 1024u * 1024u, &field), 0);
    TF_ASSERT_EQ(field, 28u);         /* 外设区 512MB */
    TF_ASSERT_EQ(mpu_encode_size(4096u, &field), 0);
    TF_ASSERT_EQ(mpu_rasr_size_bits(4096u), (11u << MPU_RASR_SIZE_Pos));

    /* 非法输入 */
    TF_ASSERT(mpu_encode_size(16u, &field) != 0);        /* 小于 32 字节 */
    TF_ASSERT(mpu_encode_size(96u, &field) != 0);        /* 非 2 的幂   */
    TF_ASSERT(mpu_encode_size(0u, &field) != 0);
    TF_ASSERT(mpu_encode_size(1024u, 0) != 0);           /* 空指针      */

    /* ---- 2. 关键区域的 RBAR / RASR 期望值 ---- */
    {
        const mpu_region_t *r_axi = &h750_mpu_table[3];
        const mpu_region_t *r_sram3 = &h750_mpu_table[5];
        const mpu_region_t *r_periph = &h750_mpu_table[11];
        const mpu_region_t *r_sdram = &h750_mpu_table[9];
        const mpu_region_t *r_flash = &h750_mpu_table[1];
        const mpu_region_t *r_itcm = &h750_mpu_table[0];
        const mpu_region_t *r_sdramnc = &h750_mpu_table[10];

        TF_ASSERT_EQ(r_axi->base, H750_AXI_SRAM_BASE);
        TF_ASSERT_EQ(mpu_rbar_value(r_axi), EXP_RBAR_AXI);
        TF_ASSERT_EQ(mpu_rasr_value(r_axi), EXP_RASR_AXI);
        TF_ASSERT_EQ(mpu_rbar_value(r_sram3), EXP_RBAR_SRAM3);
        TF_ASSERT_EQ(mpu_rasr_value(r_sram3), EXP_RASR_SRAM3);
        TF_ASSERT_EQ(mpu_rbar_value(r_periph), EXP_RBAR_PERIPH);
        TF_ASSERT_EQ(mpu_rasr_value(r_periph), EXP_RASR_PERIPH);
        TF_ASSERT_EQ(mpu_rbar_value(r_sdram), EXP_RBAR_SDRAM);
        TF_ASSERT_EQ(mpu_rasr_value(r_sdram), EXP_RASR_SDRAM);
        TF_ASSERT_EQ(mpu_rbar_value(r_flash), EXP_RBAR_FLASH);
        TF_ASSERT_EQ(mpu_rasr_value(r_flash), EXP_RASR_FLASH);
        TF_ASSERT_EQ(mpu_rbar_value(r_itcm), EXP_RBAR_ITCM);
        TF_ASSERT_EQ(mpu_rasr_value(r_itcm), EXP_RASR_ITCM);
        TF_ASSERT_EQ(mpu_rbar_value(r_sdramnc), EXP_RBAR_SDRAMNC);
        TF_ASSERT_EQ(mpu_rasr_value(r_sdramnc), EXP_RASR_SDRAMNC);

        /* RBAR.VALID 必须置位，否则 REGION 字段被忽略 */
        TF_ASSERT((mpu_rbar_value(r_axi) & MPU_RBAR_VALID) != 0u);
        /* 外设区必须禁止取指（XN=1），避免对 FIFO 的投机读 */
        TF_ASSERT((mpu_rasr_value(r_periph) & MPU_RASR_XN) != 0u);
        /* SDRAM 可缓存区不得共享（单核无其他 Cache 主体，S=0 减少监听开销） */
        TF_ASSERT((mpu_rasr_value(r_sdram) & MPU_RASR_S) == 0u);
        /* Flash 区域必须只读 */
        TF_ASSERT_EQ(r_flash->ap, MPU_AP_PRIV_RO_URO);
    }

    /* ---- 3. 区域表整体合法性 ---- */
    for (i = 0u; i < h750_mpu_table_size; i++) {
        int v = mpu_region_validate(&h750_mpu_table[i]);
        if (v != 0) {
            tf_note("区域 %u (%s) 校验失败 rc=%d", i, h750_mpu_table[i].name, v);
        }
        TF_ASSERT_EQ(v, 0);
    }
    TF_ASSERT(h750_mpu_table_size <= 16u);         /* M7 共 16 个数据区域 */
    TF_ASSERT_EQ(mpu_table_find_overlaps(), 0);    /* 区域之间不得重叠 */

    /* ---- 4. 可缓存性判定 ---- */
    TF_ASSERT_EQ(mpu_region_is_cacheable(0xC0000000u), 1);    /* SDRAM 可缓存 */
    TF_ASSERT_EQ(mpu_region_is_cacheable(0xC0FFFFFFu), 1);
    TF_ASSERT_EQ(mpu_region_is_cacheable(0xC1000000u), 0);    /* SDRAM 非缓存窗口 */
    TF_ASSERT_EQ(mpu_region_is_cacheable(0x30040000u), 0);    /* SRAM3 非缓存 */
    TF_ASSERT_EQ(mpu_region_is_cacheable(0x30000000u), 0);    /* SRAM1/2 非缓存 */
    TF_ASSERT_EQ(mpu_region_is_cacheable(0x38000000u), 0);    /* SRAM4 非缓存 */
    TF_ASSERT_EQ(mpu_region_is_cacheable(0x24000000u), 1);    /* AXI SRAM */
    TF_ASSERT_EQ(mpu_region_is_cacheable(0x08000000u), 1);    /* 内部 Flash */
    TF_ASSERT_EQ(mpu_region_is_cacheable(0x40000000u), 0);    /* 外设 Device */
    TF_ASSERT_EQ(mpu_region_is_cacheable(0x60000000u), -1);   /* 未覆盖 */

    /* ---- 5. 区域查找与属性描述 ---- */
    TF_ASSERT(mpu_region_lookup(0xC0000000u) != 0);
    TF_ASSERT_EQ(mpu_region_lookup(0xC0000000u)->number, 9u);
    TF_ASSERT(strcmp(mpu_memory_type_str(&h750_mpu_table[5]), "Normal Non-cacheable") == 0);
    TF_ASSERT(strcmp(mpu_memory_type_str(&h750_mpu_table[9]), "Normal WB no WA") == 0);
    TF_ASSERT(strcmp(mpu_memory_type_str(&h750_mpu_table[11]), "Device") == 0);
    TF_ASSERT(strcmp(mpu_ap_str(MPU_AP_PRIV_RO_URO), "RO/RO") == 0);

    /* ---- 6. 寄存器落地：逐区域写 RNR/RBAR/RASR ---- */
    hal_sim_reset();
    mpu_configure_all();
    {
        uint32_t last = h750_mpu_table_size - 1u;
        const mpu_region_t *r = &h750_mpu_table[last];
        TF_ASSERT_EQ(H750_REG32(H750_MPU_RNR), (uint32_t)r->number);
        TF_ASSERT_EQ(H750_REG32(H750_MPU_RBAR), mpu_rbar_value(r));
        TF_ASSERT_EQ(H750_REG32(H750_MPU_RASR), mpu_rasr_value(r));
    }
    mpu_enable(0u);
    TF_ASSERT((mpu_ctrl_read() & MPU_CTRL_ENABLE) != 0u);
    TF_ASSERT((mpu_ctrl_read() & MPU_CTRL_PRIVDEFENA) == 0u);
    mpu_disable();
    TF_ASSERT_EQ(mpu_ctrl_read(), 0u);

    /* ---- 7. 统计 ---- */
    mpu_get_stats(&st);
    TF_ASSERT_EQ(st.regions_total, h750_mpu_table_size);
    TF_ASSERT_EQ(st.regions_enabled, 12u);
    TF_ASSERT_EQ(st.table_overlaps, 0u);
    TF_ASSERT_EQ(st.hw_regions, 16u);          /* Cortex-M7 数据区域数 */
    TF_ASSERT(st.bytes_non_cacheable > st.bytes_cacheable);
    tf_note("可缓存区域 %u 个 / %u 字节，非缓存区域 %u 个 / %u 字节",
            st.regions_cacheable, st.bytes_cacheable,
            st.regions_non_cacheable, st.bytes_non_cacheable);

    tf_suite_end();
}
