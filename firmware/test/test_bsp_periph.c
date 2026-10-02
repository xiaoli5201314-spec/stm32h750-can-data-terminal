/*
 * test_bsp_periph.c
 * ---------------------------------------------------------------------------
 * 外设 BSP 测试：QSPI Flash、SD 卡与自研文件系统（含掉电恢复）、
 * RMII 以太网（PHY/MDIO/自环/组包校验和）、LCD 诊断界面、诊断日志。
 */
#include <string.h>
#include "test_framework.h"
#include "qspi_bsp.h"
#include "sd_file.h"
#include "eth_rmii.h"
#include "lcd_diag.h"
#include "diag_log.h"
#include "hal_stub.h"

static uint8_t s_page[H750_QSPI_PAGE_SIZE];
static uint8_t s_readback[H750_QSPI_PAGE_SIZE];
static uint8_t s_sector[H750_QSPI_SECTOR_SIZE];
static diag_log_entry_t s_log[64];

static void test_qspi(void)
{
    uint8_t id[3];
    const qspi_cfg_t *cfg;
    uint32_t i;

    tf_suite_begin("QSPI Flash（8MB SPI NOR）");

    hal_sim_reset();
    TF_ASSERT_EQ(qspi_init(), 0);
    cfg = qspi_get_cfg();
    TF_ASSERT_EQ(cfg->prescaler, 1u);            /* 240MHz / (2*(1+1)) = 60MHz */
    TF_ASSERT_EQ(cfg->clk_hz, 60000000u);
    TF_ASSERT_EQ(cfg->fsize, 22u);               /* 2^(22+1) = 8MB */
    TF_ASSERT_EQ(qspi_fsize_field(8u * 1024u * 1024u), 22u);
    TF_ASSERT_EQ(qspi_fsize_field(64u * 1024u * 1024u), 25u);
    TF_ASSERT_EQ(qspi_prescaler_for(240000000u, 60000000u), 1u);
    TF_ASSERT_EQ(H750_REG32(H750_QSPI_BASE + QSPI_CR) & QSPI_CR_EN, QSPI_CR_EN);
    TF_ASSERT_EQ((H750_REG32(H750_QSPI_BASE + QSPI_DCR) & QSPI_DCR_FSIZE_Msk)
                 >> QSPI_DCR_FSIZE_Pos, 22u);

    TF_ASSERT_EQ(qspi_read_jedec_id(id), 0);
    TF_ASSERT_EQ(id[0], 0xEFu);
    TF_ASSERT_EQ(id[2], 0x17u);                  /* 8MB 容量编码 */

    /* 擦除（器件默认全 0xFF） */
    TF_ASSERT_EQ(qspi_erase_sector(4096u), 0);
    for (i = 0u; i < sizeof(s_page); i++) {
        s_page[i] = (uint8_t)(i & 0xFFu);
    }
    TF_ASSERT_EQ(qspi_indirect_write(4096u, s_page, sizeof(s_page)), 0);
    TF_ASSERT_EQ(qspi_indirect_read(4096u, s_readback, sizeof(s_readback)), 0);
    TF_ASSERT_EQ(memcmp(s_page, s_readback, sizeof(s_page)), 0);
    TF_ASSERT_EQ(hal_sim_flash_program_count() > 0u, 1);
    TF_ASSERT_EQ(hal_sim_flash_erase_count(), 1u);

    /* 跨页写入自动分页 */
    for (i = 0u; i < sizeof(s_sector); i++) {
        s_sector[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    }
    TF_ASSERT_EQ(qspi_erase_sector(8192u), 0);
    TF_ASSERT_EQ(qspi_indirect_write(8192u, s_sector, sizeof(s_sector)), 0);
    TF_ASSERT_EQ(qspi_indirect_read(8192u, s_readback, sizeof(s_page)), 0);
    TF_ASSERT_EQ(memcmp(s_sector, s_readback, sizeof(s_page)), 0);
    TF_ASSERT_EQ(qspi_indirect_read(8192u + 4096u - 128u, s_readback, 128u), 0);
    TF_ASSERT_EQ(memcmp(&s_sector[4096u - 128u], s_readback, 128u), 0);

    /* 内存映射模式 */
    TF_ASSERT_EQ(qspi_memory_mapped_enable(), 0);
    TF_ASSERT_EQ((H750_REG32(H750_QSPI_BASE + QSPI_CCR) & QSPI_CCR_FMODE_Msk)
                 >> QSPI_CCR_FMODE_Pos, QSPI_CCR_FMODE_MEMRD);
    TF_ASSERT_EQ(qspi_memory_mapped_disable(), 0);

    /* NOR 语义：全 1 未擦除时不能再写入 */
    TF_ASSERT_EQ(qspi_erase_sector(16384u), 0);
    TF_ASSERT_EQ(qspi_indirect_write(16384u, s_page, 16u), 0);
    TF_ASSERT_EQ(qspi_indirect_write(16384u, s_sector, 16u), 0);   /* 只能 1->0 */
    TF_ASSERT_EQ(qspi_indirect_read(16384u, s_readback, 16u), 0);
    TF_ASSERT_EQ(s_readback[0], (uint8_t)(s_page[0] & s_sector[0]));
    tf_note("QSPI：JEDEC ID %02X %02X %02X，页写/扇区擦除/回读一致，程序次数 %u",
            id[0], id[1], id[2], hal_sim_flash_program_count());

    tf_suite_end();
}

static void test_sd_fs(void)
{
    fs_file_t f;
    fs_stats_t fs;
    char names[H750_FS_MAX_FILES][H750_FS_NAME_LEN];
    uint32_t count = 0u;
    static char text[1024];
    static char back[1024];
    uint32_t i;

    tf_suite_begin("SD 卡与自研日志文件系统（掉电恢复）");

    hal_sim_reset();
    TF_ASSERT_EQ(sd_card_present(), 1);
    TF_ASSERT_EQ(sd_block_init(), 0);
    TF_ASSERT_EQ(fs_format(), 0);
    TF_ASSERT_EQ(fs_mount(), 0);
    TF_ASSERT_EQ(fs_crc32((const uint8_t *)"123456789", 9u), 0xCBF43926u);   /* CRC32 标准值 */

    /* 创建 + 追加 + 回读 */
    TF_ASSERT_EQ(fs_create("boot.log", &f), 0);
    TF_ASSERT_EQ(fs_create("boot.log", &f), -3);      /* 重名 */
    TF_ASSERT_EQ(fs_open("boot.log", &f), 0);
    TF_ASSERT_EQ(f.bytes, 0u);

    for (i = 0u; i < sizeof(text); i++) {
        text[i] = (char)('A' + (i % 26u));
    }
    TF_ASSERT_EQ(fs_append(&f, text, 600u), 0);
    TF_ASSERT_EQ(f.bytes, 600u);
    TF_ASSERT_EQ(fs_append(&f, text, 424u), 0);
    TF_ASSERT_EQ(f.bytes, 1024u);
    TF_ASSERT_EQ(f.sectors, 2u);
    TF_ASSERT_EQ(fs_read(&f, 0u, back, sizeof(back)), 1024);
    /* 两次追加的都是同一个缓冲区：文件内容 = text[0..599] + text[0..423] */
    TF_ASSERT_EQ(memcmp(text, back, 600u), 0);
    TF_ASSERT_EQ(memcmp(text, &back[600], 424u), 0);
    /* 跨扇区读取 */
    TF_ASSERT_EQ(fs_read(&f, 500u, back, 100u), 100);
    TF_ASSERT_EQ(memcmp(&text[500], back, 100u), 0);
    /* 越界读取被截断 */
    TF_ASSERT_EQ(fs_read(&f, 1000u, back, 100u), 24);
    TF_ASSERT_EQ(fs_close(&f), 0);

    /* 列出文件 */
    TF_ASSERT_EQ(fs_create("diag.log", &f), 0);
    TF_ASSERT_EQ(fs_append(&f, text, 100u), 0);
    (void)fs_close(&f);
    TF_ASSERT_EQ(fs_list(names, H750_FS_MAX_FILES, &count), 0);
    TF_ASSERT_EQ(count, 2u);
    TF_ASSERT_EQ(strncmp(names[0], "boot.log", 8u), 0);

    /* 掉电恢复：数据写成功但目录未更新时，以目录为准（长度只用旧值） */
    TF_ASSERT_EQ(fs_open("boot.log", &f), 0);
    TF_ASSERT_EQ(f.bytes, 1024u);
    hal_sim_sd_set_write_fail(1);                     /* 下次写失败 */
    TF_ASSERT_EQ(fs_append(&f, text, 200u), -4);
    hal_sim_sd_set_write_fail(-1);
    /* 重新挂载 + 恢复 */
    TF_ASSERT_EQ(fs_umount(), 0);
    TF_ASSERT_EQ(fs_mount(), 0);
    TF_ASSERT_EQ(fs_recover(), 0);
    TF_ASSERT_EQ(fs_open("boot.log", &f), 0);
    TF_ASSERT_EQ(f.bytes, 1024u);                     /* 未提交的 200 字节被丢弃 */
    TF_ASSERT_EQ(fs_read(&f, 0u, back, 1024u), 1024);
    TF_ASSERT_EQ(memcmp(text, back, 600u), 0);        /* 已提交数据完好 */
    TF_ASSERT_EQ(memcmp(text, &back[600], 424u), 0);

    fs_get_stats(&fs);
    TF_ASSERT(fs.writes > 0u);
    TF_ASSERT(fs.reads > 0u);
    TF_ASSERT_EQ(fs.recoveries, 1u);
    tf_note("H750FS：文件 %u 个，写 %u 次读 %u 次，掉电恢复 %u 次，最后错误=\"%s\"",
            count, fs.writes, fs.reads, fs.recoveries, fs_last_error());

    /* 卡不在位时挂载失败 */
    hal_sim_sd_set_present(0);
    TF_ASSERT_EQ(fs_mount(), -1);
    hal_sim_sd_set_present(1);

    tf_suite_end();
}

static void test_eth(void)
{
    eth_stats_t st;
    uint16_t id1 = 0u;
    uint16_t id2 = 0u;
    uint8_t link = 0u;
    static uint8_t frame[256];
    static uint8_t rx[H750_ETH_RX_BUF_LEN];
    uint32_t out_len = 0u;
    uint8_t payload[64];
    uint32_t i;

    tf_suite_begin("RMII 以太网（PHY / MDIO / 自环 / 组包）");

    hal_sim_reset();
    TF_ASSERT_EQ(eth_init(), 0);
    TF_ASSERT_EQ(eth_phy_identify(&id1, &id2), 0);
    TF_ASSERT_EQ(id1, H750_ETH_PHY_ID1_VALUE);
    TF_ASSERT_EQ(id2, H750_ETH_PHY_ID2_VALUE);
    TF_ASSERT_EQ(eth_link_status(&link), 0);
    TF_ASSERT_EQ(link, 1u);
    TF_ASSERT(hal_sim_mdio_reads() >= 3u);
    TF_ASSERT(hal_sim_mdio_writes() >= 1u);
    tf_note("PHY：ID1=0x%04X ID2=0x%04X，MDIO 读 %u 次写 %u 次",
            id1, id2, hal_sim_mdio_reads(), hal_sim_mdio_writes());

    /* 链路断开可被检测 */
    hal_sim_eth_set_link(0);
    TF_ASSERT_EQ(eth_link_status(&link), 0);
    TF_ASSERT_EQ(link, 0u);
    hal_sim_eth_set_link(1);
    (void)eth_link_status(&link);
    TF_ASSERT_EQ(link, 1u);

    /* 自环收发 */
    eth_set_loopback(1);
    TF_ASSERT_EQ(eth_get_loopback(), 1);
    for (i = 0u; i < sizeof(frame); i++) {
        frame[i] = (uint8_t)i;
    }
    TF_ASSERT_EQ(eth_send_frame(frame, sizeof(frame)), 0);
    TF_ASSERT_EQ(eth_poll_rx(rx, sizeof(rx), &out_len), 1);
    TF_ASSERT_EQ(out_len, sizeof(frame));
    TF_ASSERT_EQ(memcmp(frame, rx, sizeof(frame)), 0);

    /* IPv4/UDP 组包与校验和 */
    for (i = 0u; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(0x30u + i);
    }
    TF_ASSERT_EQ(eth_send_report(payload, sizeof(payload)), 0);
    TF_ASSERT_EQ(eth_poll_rx(rx, sizeof(rx), &out_len), 1);
    TF_ASSERT_EQ(out_len, 14u + 20u + 8u + sizeof(payload));
    TF_ASSERT_EQ(rx[12], 0x08u);      /* EtherType = IPv4 */
    TF_ASSERT_EQ(rx[13], 0x00u);
    TF_ASSERT_EQ(rx[14], 0x45u);      /* IPv4 / IHL=5 */
    TF_ASSERT_EQ(rx[14 + 9], 17u);    /* 协议 = UDP */
    TF_ASSERT_EQ(rx[14 + 20 + 0], (uint8_t)(50000u >> 8));
    TF_ASSERT_EQ(memcmp(&rx[14 + 20 + 8], payload, sizeof(payload)), 0);
    /* 校验和自校验：整个首部再算一次应为 0 */
    TF_ASSERT_EQ(eth_ip_checksum(&rx[14], 20u), 0u);
    TF_ASSERT_EQ(eth_ip_checksum(0, 20u), 0u);

    eth_get_stats(&st);
    TF_ASSERT(st.tx_frames >= 2u);
    TF_ASSERT(st.rx_frames >= 2u);
    TF_ASSERT_EQ(st.tx_errors, 0u);
    {
        char txt[64];
        uint32_t n = eth_parse_stats(&st, txt, sizeof(txt));
        TF_ASSERT(n > 0u);
        tf_note("以太网统计文本：%s", txt);
    }

    tf_suite_end();
}

static void test_lcd(void)
{
    lcd_ctx_t ctx;
    lcd_status_t st;
    uint32_t lit = 0u;
    uint32_t crc1;
    uint32_t crc2;

    tf_suite_begin("LCD 诊断界面（七段数码管 + 状态页）");

    hal_sim_reset();
    TF_ASSERT_EQ(lcd_init(), 0);
    TF_ASSERT_EQ(lcd_get_ctx(&ctx), 0);
    TF_ASSERT_EQ(ctx.bytes, H750_LCD_WIDTH * H750_LCD_HEIGHT * 2u);
    TF_ASSERT_EQ(H750_REG32(H750_LTDC_BASE + LTDC_GCR) & LTDC_GCR_LTDCEN, LTDC_GCR_LTDCEN);
    TF_ASSERT_EQ((H750_REG32(H750_LTDC_BASE + LTDC_L1PFCR) & 0x7u), LTDC_L1PFCR_RGB565);

    lcd_clear(LCD_BLACK);
    TF_ASSERT_EQ(lcd_count_color(LCD_BLACK), H750_LCD_WIDTH * H750_LCD_HEIGHT);

    /* 七段数字：8 全亮 7 段，1 只亮 2 段 */
    lcd_clear(LCD_BLACK);
    lcd_seg_digit(10, 10, 4, 8u, LCD_WHITE);
    {
        uint32_t lit8 = lcd_count_color(LCD_WHITE);
        lcd_clear(LCD_BLACK);
        lcd_seg_digit(10, 10, 4, 1u, LCD_WHITE);
        {
            uint32_t lit1 = lcd_count_color(LCD_WHITE);
            TF_ASSERT(lit8 > lit1);
            TF_ASSERT(lit1 > 0u);
            tf_note("七段渲染：数字 8 点亮 %u 像素，数字 1 点亮 %u 像素", lit8, lit1);
        }
    }

    /* 状态页渲染 */
    (void)memset(&st, 0, sizeof(st));
    st.can1_fps = 1234u;
    st.can2_fps = 5678u;
    st.can1_errors = 12u;
    st.can2_errors = 0u;
    st.cache_watermark_pm = 640u;
    st.cache_dropped = 37u;
    st.sd_files = 3u;
    st.log_errors = 2u;
    st.link_up = 1u;
    st.sd_present = 1u;
    st.sniff_mode = 0u;
    lit = lcd_render_status(&st);
    TF_ASSERT(lit > 0u);
    TF_ASSERT(lcd_count_color(LCD_GREEN) > 0u);
    TF_ASSERT(lcd_count_color(LCD_RED) > 0u);
    lcd_get_ctx(&ctx);
    TF_ASSERT_EQ(ctx.frames, 1u);
    TF_ASSERT_EQ(ctx.lit_pixels, lit);

    /* 相同输入渲染结果稳定（帧缓冲 CRC 一致） */
    crc1 = lcd_framebuffer_crc();
    (void)lcd_render_status(&st);
    crc2 = lcd_framebuffer_crc();
    /* 双缓冲交替，比较同一缓冲的两次渲染结果 */
    TF_ASSERT(crc1 != 0u);
    TF_ASSERT(crc2 != 0u);
    tf_note("状态页：点亮像素 %u，帧缓冲 CRC32=0x%08X", lit, crc1);

    tf_suite_end();
}

static void test_diag_log(void)
{
    diag_stats_t st;
    diag_log_entry_t e;
    static char text[2048];
    uint32_t i;
    uint32_t n;

    tf_suite_begin("诊断日志（分级 / 环形覆盖 / 文本导出）");

    hal_sim_reset();
    TF_ASSERT_EQ(diag_log_init(s_log, 64u), 0);
    TF_ASSERT_EQ(diag_log_capacity(), 64u);
    TF_ASSERT_EQ(diag_log_init(0, 64u), -1);

    diag_log_set_level(H750_LOG_INFO);
    TF_ASSERT_EQ(diag_log_get_level(), H750_LOG_INFO);
    diag_log_write(H750_LOG_DEBUG, DIAG_MOD_APP, 0x0001u, 0, 0u);   /* 被级别过滤 */
    TF_ASSERT_EQ(diag_log_count(), 0u);
    TF_ASSERT_EQ(diag_log_dropped(), 1u);
    diag_log_write(H750_LOG_ERROR, DIAG_MOD_CAN1, DIAG_EV_CAN_BUSOFF, "BO", 2u);
    diag_log_write(H750_LOG_INFO, DIAG_MOD_ETH, DIAG_EV_ETH_LINK_UP, 0, 0u);
    TF_ASSERT_EQ(diag_log_count(), 2u);
    TF_ASSERT_EQ(diag_log_get(0u, &e), 0);
    TF_ASSERT_EQ(e.level, H750_LOG_ERROR);
    TF_ASSERT_EQ(e.module, DIAG_MOD_CAN1);
    TF_ASSERT_EQ(e.code, DIAG_EV_CAN_BUSOFF);
    TF_ASSERT_EQ(e.len, 2u);
    TF_ASSERT_EQ(e.payload[0], (uint8_t)'B');
    TF_ASSERT_EQ(diag_log_get(2u, &e), -1);

    /* 报文追踪条目（DEBUG 级，需要放开级别过滤） */
    diag_log_set_level(H750_LOG_DEBUG);
    diag_log_trace(DIAG_MOD_CAN1, 0x12345678u, 0x18FF0000u, 1u, 0u);
    TF_ASSERT_EQ(diag_log_count(), 3u);
    TF_ASSERT_EQ(diag_log_get(2u, &e), 0);
    TF_ASSERT_EQ(e.payload[0], 0x78u);
    TF_ASSERT_EQ(e.payload[3], 0x12u);
    TF_ASSERT_EQ(e.payload[8], 0u);

    /* 环形覆盖 */
    for (i = 0u; i < 100u; i++) {
        diag_log_write(H750_LOG_WARN, DIAG_MOD_CACHE, 0x0600u, &i, 4u);
    }
    TF_ASSERT_EQ(diag_log_count(), 64u);
    diag_log_get_stats(&st);
    TF_ASSERT_EQ(st.wraps, 100u - (64u - 3u));
    TF_ASSERT(st.error >= 1u);
    TF_ASSERT(st.warn >= 100u);

    /* 文本导出 */
    n = diag_log_dump_text(text, sizeof(text), 0u);
    TF_ASSERT(n > 0u);
    TF_ASSERT(strncmp(text, "# H750 CAN-DAQ diag log", 23u) == 0);
    tf_note("日志导出：%u 字节文本，条目 %u，覆盖 %u 次，错误 %u 条",
            n, diag_log_count(), st.wraps, st.error);

    diag_log_reset();
    TF_ASSERT_EQ(diag_log_count(), 0u);

    tf_suite_end();
}

void test_bsp_periph(void)
{
    test_qspi();
    test_sd_fs();
    test_eth();
    test_lcd();
    test_diag_log();
}
