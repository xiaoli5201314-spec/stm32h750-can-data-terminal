/*
 * test_cache_coherency.c
 * ---------------------------------------------------------------------------
 * Cache 与 DMA 一致性测试：
 *   1) 复现"不做维护导致数据错乱"的现场故障（RX 与 TX 两个方向）
 *   2) 验证"非缓存区 / 写回 + 维护 / 写通"三种策略都能得到正确数据
 *   3) 验证未对齐无效化会丢失未回写数据（说明 32 字节对齐约束的必要性）
 *   4) 验证 MPU 区域属性到 Cache 行为的联动
 */
#include <string.h>
#include "test_framework.h"
#include "cache_coherency.h"
#include "mpu_config.h"
#include "hal_stub.h"

static CC_ALIGN32 uint8_t s_rx[128];
static CC_ALIGN32 uint8_t s_tx[128];
static CC_ALIGN32 uint8_t s_nc[128];
static CC_ALIGN32 uint8_t s_wt[128];
static CC_ALIGN32 uint8_t s_line[64];
static CC_ALIGN32 uint8_t s_src[128];

void test_cache_coherency(void)
{
    uint32_t i;

    tf_suite_begin("Cache 与 DMA 一致性（非缓存区 / 写回 / 无效化）");

    /* =====================================================================
     * 场景 A：RX 方向 —— DMA 写入内存，CPU 读到旧数据（故障复现）
     * ===================================================================== */
    hal_sim_reset();
    cc_sim_reset();
    (void)cc_sim_set_region(s_rx, sizeof(s_rx), CC_SIM_WB);

    /* CPU 先读一次：D-Cache 填充了该行（现场表现为解析线程上一轮读过的缓冲） */
    (void)cc_sim_cpu_read32(&s_rx[0]);

    /* DMA 把新收到的报文写进物理内存 */
    for (i = 0u; i < 8u; i++) {
        uint32_t v = 0xC0DE0000u + i;
        (void)memcpy(&s_src[i * 4u], &v, 4u);
    }
    cc_sim_dma_write(s_rx, s_src, 32u);

    /* 未做任何维护：CPU 命中缓存中的旧数据 —— 这就是现场"数据错乱" */
    TF_ASSERT_NE(cc_sim_cpu_read32(&s_rx[0]), 0xC0DE0000u);
    TF_ASSERT_EQ(cc_sim_phys_read32(&s_rx[0]), 0xC0DE0000u);   /* 物理内存其实是新值 */
    tf_note("故障复现：物理内存=0x%08X，CPU 读到=0x%08X（不一致）",
            cc_sim_phys_read32(&s_rx[0]), cc_sim_cpu_read32(&s_rx[0]));

    /* 修复 1：读前无效化 */
    cc_dcache_invalidate(s_rx, sizeof(s_rx));
    TF_ASSERT_EQ(cc_sim_cpu_read32(&s_rx[0]), 0xC0DE0000u);
    TF_ASSERT_EQ(cc_sim_cpu_read32(&s_rx[8]), 0xC0DE0000u + 2u);

    /* 修复 2：clean + invalidate（等价于硬件 DCCIMVAC） */
    cc_sim_dma_write(s_rx, s_src, 32u);
    cc_dcache_clean_invalidate(s_rx, sizeof(s_rx));
    TF_ASSERT_EQ(cc_sim_cpu_read32(&s_rx[4]), 0xC0DE0000u + 1u);

    /* 修复 3：把该缓冲划入非缓存区，则完全不需要维护动作 */
    cc_sim_reset();
    (void)cc_sim_set_region(s_nc, sizeof(s_nc), CC_SIM_NC);
    TF_ASSERT_EQ(cc_sim_cpu_write32(&s_nc[0], 0xAA55AA55u), 0);   /* 0 = 旁路 Cache */
    TF_ASSERT_EQ(cc_sim_phys_read32(&s_nc[0]), 0xAA55AA55u);      /* 立即落到物理内存 */
    cc_sim_dma_write(s_nc, s_src, 32u);
    TF_ASSERT_EQ(cc_sim_cpu_read32(&s_nc[0]), 0xC0DE0000u);       /* DMA 写 CPU 立即可见 */
    TF_ASSERT_EQ(cc_sim_dirty_lines(), 0u);                       /* 不产生脏行 */

    /* =====================================================================
     * 场景 B：TX 方向 —— CPU 写入只落在 Cache，DMA 读到旧数据
     * ===================================================================== */
    cc_sim_reset();
    (void)cc_sim_set_region(s_tx, sizeof(s_tx), CC_SIM_WB);
    for (i = 0u; i < 32u; i++) {
        (void)cc_sim_cpu_write32(&s_tx[i * 4u], 0x1000u + i);
    }
    memset(s_src, 0, sizeof(s_src));
    cc_sim_dma_read(s_tx, s_src, 128u);
    TF_ASSERT_NE(*(uint32_t *)&s_src[20], 0x1005u);     /* DMA 读到旧值（0） */
    tf_note("TX 未 Clean：DMA 读到 0x%08X，期望 0x00001005（不一致）",
            *(uint32_t *)&s_src[20]);

    cc_dcache_clean(s_tx, sizeof(s_tx));
    cc_sim_dma_read(s_tx, s_src, 128u);
    TF_ASSERT_EQ(*(uint32_t *)&s_src[20], 0x1005u);     /* Clean 后一致 */
    TF_ASSERT_EQ(*(uint32_t *)&s_src[124], 0x101Fu);
    TF_ASSERT_EQ(cc_sim_dirty_lines(), 0u);
    tf_note("TX Clean 后：DMA 读到 0x%08X（与 CPU 视图一致）",
            *(uint32_t *)&s_src[20]);

    /* 写通策略：CPU 写直达内存，TX 方向免维护；RX 方向仍需无效化 */
    cc_sim_reset();
    (void)cc_sim_set_region(s_wt, sizeof(s_wt), CC_SIM_WT);
    (void)cc_sim_cpu_write32(&s_wt[0], 0x5A5A1234u);
    TF_ASSERT_EQ(cc_sim_phys_read32(&s_wt[0]), 0x5A5A1234u);      /* 立即落盘 */
    TF_ASSERT_EQ(cc_sim_dirty_lines(), 0u);
    (void)cc_sim_cpu_read32(&s_wt[0]);
    cc_sim_dma_write(s_wt, s_src, 4u);
    TF_ASSERT_NE(cc_sim_cpu_read32(&s_wt[0]), *(uint32_t *)s_src); /* 仍会读到旧值 */
    cc_dcache_invalidate(s_wt, sizeof(s_wt));
    TF_ASSERT_EQ(cc_sim_cpu_read32(&s_wt[0]), *(uint32_t *)s_src);

    /* =====================================================================
     * 场景 C：无效化粒度错误会丢失未回写数据（对齐约束的必要性）
     * ===================================================================== */
    cc_sim_reset();
    (void)cc_sim_set_region(s_line, sizeof(s_line), CC_SIM_WB);
    (void)cc_sim_cpu_write32(&s_line[0], 0xDEADBEEFu);   /* 与 s_line[16] 同一条 Cache 行 */
    TF_ASSERT_EQ(cc_sim_dirty_lines(), 1u);
    cc_dcache_invalidate(&s_line[16], 4u);               /* 只"无效化 4 字节" */
    TF_ASSERT_EQ(cc_sim_dirty_discarded(), 1u);          /* 整行脏数据被丢弃 */
    TF_ASSERT_NE(cc_sim_phys_read32(&s_line[0]), 0xDEADBEEFu);
    tf_note("未对齐无效化：丢弃脏行 %u 条，s_line[0] 物理值=0x%08X（数据丢失）",
            cc_sim_dirty_discarded(), cc_sim_phys_read32(&s_line[0]));

    /* 正确做法：先 Clean 再 Invalidate，且按 32 字节对齐操作 */
    cc_sim_reset();
    (void)cc_sim_set_region(s_line, sizeof(s_line), CC_SIM_WB);
    (void)cc_sim_cpu_write32(&s_line[0], 0xDEADBEEFu);
    cc_dcache_clean(s_line, sizeof(s_line));
    TF_ASSERT_EQ(cc_sim_phys_read32(&s_line[0]), 0xDEADBEEFu);
    cc_dcache_invalidate(&s_line[16], 4u);               /* 此时行是干净的 */
    TF_ASSERT_EQ(cc_sim_dirty_discarded(), 0u);
    TF_ASSERT_EQ(cc_sim_phys_read32(&s_line[0]), 0xDEADBEEFu);   /* 数据不丢 */

    /* =====================================================================
     * 场景 D：对齐工具与缓冲描述
     * ===================================================================== */
    TF_ASSERT_EQ(cc_align_up32(0x20000001u), 0x20000020u);
    TF_ASSERT_EQ(cc_align_down32(0x2000003Fu), 0x20000020u);
    TF_ASSERT_EQ(cc_is_aligned32(0x20000020u), 1);
    TF_ASSERT_EQ(cc_is_aligned32(0x20000024u), 0);
    TF_ASSERT_EQ(cc_line_count(0x20000020u, 32u), 1u);
    TF_ASSERT_EQ(cc_line_count(0x20000020u, 33u), 2u);
    TF_ASSERT_EQ(cc_line_count(0x20000024u, 4u), 1u);
    TF_ASSERT_EQ(cc_line_count(0x20000000u, 0u), 0u);
    TF_ASSERT_EQ(cc_range_is_line_aligned(0x20000020u, 64u), 1);
    TF_ASSERT_EQ(cc_range_is_line_aligned(0x20000024u, 64u), 0);
    TF_ASSERT_EQ(cc_range_is_line_aligned(0x20000020u, 33u), 0);

    {
        cc_dma_buffer_t buf;
        TF_ASSERT_EQ(cc_dma_buffer_setup(&buf, s_rx, 128u, CC_ATTR_WRITE_BACK, "rx"), 0);
        TF_ASSERT_EQ(cc_dma_buffer_setup(&buf, &s_rx[1], 128u, CC_ATTR_NON_CACHEABLE, "x"), -2);
        TF_ASSERT_EQ(cc_dma_buffer_setup(&buf, s_rx, 100u, CC_ATTR_NON_CACHEABLE, "x"), -3);
        TF_ASSERT_EQ(cc_dma_buffer_setup(&buf, 0, 32u, CC_ATTR_NON_CACHEABLE, "x"), -1);

        /* 非缓存缓冲的同步动作应为"免维护"（返回 0），写回缓冲返回 1 */
        TF_ASSERT_EQ(cc_dma_buffer_setup(&buf, s_nc, 128u, CC_ATTR_NON_CACHEABLE, "nc"), 0);
        TF_ASSERT_EQ(cc_dma_sync_tx(&buf), 0);
        TF_ASSERT_EQ(cc_dma_sync_rx(&buf), 0);
        TF_ASSERT_EQ(cc_dma_buffer_setup(&buf, s_tx, 128u, CC_ATTR_WRITE_BACK, "wb"), 0);
        TF_ASSERT_EQ(cc_dma_sync_tx(&buf), 1);
        TF_ASSERT_EQ(cc_dma_sync_rx(&buf), 1);
        TF_ASSERT_EQ(buf.tx_syncs, 1u);
        TF_ASSERT_EQ(buf.rx_syncs, 1u);
    }

    /* =====================================================================
     * 场景 E：MPU 属性联动（属性表决定 Cache 行为，而不是靠人记住）
     * ===================================================================== */
    cc_sim_reset();
    /* SDRAM 非缓存窗口 0xC1000000 -> 属性应为非缓存 */
    TF_ASSERT_EQ(cc_sim_set_region_from_mpu(s_nc, 0xC1000000u, sizeof(s_nc)), 0);
    TF_ASSERT_EQ((int)cc_sim_get_region_attr((uintptr_t)s_nc), (int)CC_SIM_NC);
    (void)cc_sim_cpu_write32(&s_nc[0], 0x12345678u);
    TF_ASSERT_EQ(cc_sim_phys_read32(&s_nc[0]), 0x12345678u);

    /* SRAM3（FDCAN 元素层）同样是非缓存 */
    TF_ASSERT_EQ(cc_sim_set_region_from_mpu(s_wt, H750_SRAM3_BASE, sizeof(s_wt)), 0);
    TF_ASSERT_EQ((int)cc_sim_get_region_attr((uintptr_t)s_wt), (int)CC_SIM_NC);

    /* SDRAM 可缓存区 -> 写回 + 需要维护 */
    TF_ASSERT_EQ(cc_sim_set_region_from_mpu(s_rx, 0xC0000000u, sizeof(s_rx)), 0);
    TF_ASSERT_EQ((int)cc_sim_get_region_attr((uintptr_t)s_rx), (int)CC_SIM_WB);
    TF_ASSERT_EQ(mpu_region_is_cacheable(0xC0000000u), 1);

    /* =====================================================================
     * 场景 F：完整 DMA 往返（对齐缓冲 + 方向性维护）数据零错乱
     * ===================================================================== */
    cc_sim_reset();
    (void)cc_sim_set_region(s_tx, sizeof(s_tx), CC_SIM_WB);
    (void)cc_sim_set_region(s_rx, sizeof(s_rx), CC_SIM_WB);
    for (i = 0u; i < 32u; i++) {
        (void)cc_sim_cpu_write32(&s_tx[i * 4u], 0xA5A50000u + i);
    }
    cc_dcache_clean(s_tx, sizeof(s_tx));                     /* TX：CPU 写 -> DMA 读 */
    memset(s_src, 0, sizeof(s_src));
    cc_sim_dma_read(s_tx, s_src, 128u);                      /* 模拟 DMA 搬运 */
    cc_sim_dma_write(s_rx, s_src, 128u);                     /* 模拟 DMA 写入接收缓冲 */
    cc_dcache_invalidate(s_rx, sizeof(s_rx));                /* RX：DMA 写 -> CPU 读 */
    {
        uint32_t errs = 0u;
        for (i = 0u; i < 32u; i++) {
            if (cc_sim_cpu_read32(&s_rx[i * 4u]) != (0xA5A50000u + i)) {
                errs++;
            }
        }
        TF_ASSERT_EQ(errs, 0u);
        tf_note("128 字节 DMA 往返比对：错误 %u 字节组（缓存行 %u 条）",
                errs, cc_line_count((uintptr_t)s_rx, 128u));
    }

    /* 统计口径检查 */
    {
        cc_stats_t st;
        cc_get_stats(&st);
        TF_ASSERT(st.clean_ops > 0u);
        TF_ASSERT(st.invalidate_ops > 0u);
        TF_ASSERT(st.lines_writeback > 0u);
        TF_ASSERT(st.cpu_stores > 0u);
        TF_ASSERT(st.cpu_loads > 0u);
    }
    cc_reset_stats();
    {
        cc_stats_t st;
        cc_get_stats(&st);
        TF_ASSERT_EQ(st.clean_ops, 0u);
    }

    tf_suite_end();
}
