/*
 * lcd_diag.c
 * ---------------------------------------------------------------------------
 * LCD 诊断界面实现。帧缓冲位于 SDRAM 的非缓存/可缓存区（双缓冲），
 * 通过 LTDC 的 L1CFBAR 切换；仿真构建下帧缓冲映射到主机缓冲。
 */
#include <string.h>
#include "lcd_diag.h"
#include "hal_stub.h"
#include "diag_log.h"
#include "sd_file.h"

#define LCD_FB_BYTES    (H750_LCD_WIDTH * H750_LCD_HEIGHT * 2u)

static lcd_ctx_t s_ctx;
static uint16_t *s_fb[2];

int lcd_init(void)
{
    uint32_t i;

    for (i = 0u; i < 2u; i++) {
        s_fb[i] = (uint16_t *)hal_mem_ptr(H750_SDRAM_BASE + H750_SDRAM_FB_OFF +
                                          (i * LCD_FB_BYTES), LCD_FB_BYTES);
        if (s_fb[i] == 0) {
            return -1;
        }
        (void)memset(s_fb[i], 0, LCD_FB_BYTES);
    }
    s_ctx.fb = s_fb[0];
    s_ctx.bytes = LCD_FB_BYTES;
    s_ctx.frames = 0u;
    s_ctx.lit_pixels = 0u;
    s_ctx.buffer_index = 0u;
    s_ctx.ready = 1u;

    /* LTDC 基本配置：800x480，RGB565，单层 */
    H750_REG32(H750_LTDC_BASE + LTDC_SSCR) =
        ((H750_LCD_HSYNC - 1u) << 16) | (H750_LCD_VSYNC - 1u);
    H750_REG32(H750_LTDC_BASE + LTDC_BPCR) =
        ((H750_LCD_HSYNC + H750_LCD_HBP - 1u) << 16) | (H750_LCD_VSYNC + H750_LCD_VBP - 1u);
    H750_REG32(H750_LTDC_BASE + LTDC_AWCR) =
        ((H750_LCD_HSYNC + H750_LCD_HBP + H750_LCD_WIDTH - 1u) << 16) |
        (H750_LCD_VSYNC + H750_LCD_VBP + H750_LCD_HEIGHT - 1u);
    H750_REG32(H750_LTDC_BASE + LTDC_TWCR) =
        ((H750_LCD_HSYNC + H750_LCD_HBP + H750_LCD_WIDTH + H750_LCD_HFP - 1u) << 16) |
        (H750_LCD_VSYNC + H750_LCD_VBP + H750_LCD_HEIGHT + H750_LCD_VFP - 1u);
    H750_REG32(H750_LTDC_BASE + LTDC_L1PFCR) = LTDC_L1PFCR_RGB565;
    H750_REG32(H750_LTDC_BASE + LTDC_L1CFBLR) =
        ((H750_LCD_WIDTH * 2u) << 16) | (H750_LCD_WIDTH * 2u + 3u);
    H750_REG32(H750_LTDC_BASE + LTDC_L1CFBLNR) = H750_LCD_HEIGHT;
    H750_REG32(H750_LTDC_BASE + LTDC_L1CR) = LTDC_L1CREN;
    H750_REG32(H750_LTDC_BASE + LTDC_GCR) = LTDC_GCR_LTDCEN;
    return 0;
}

int lcd_get_ctx(lcd_ctx_t *ctx)
{
    if (ctx == 0) {
        return -1;
    }
    *ctx = s_ctx;
    return 0;
}

void lcd_swap(void)
{
    s_ctx.buffer_index = (uint8_t)(1u - s_ctx.buffer_index);
    s_ctx.fb = s_fb[s_ctx.buffer_index];
    H750_REG32(H750_LTDC_BASE + LTDC_L1CFBAR) =
        (H750_SDRAM_BASE + H750_SDRAM_FB_OFF + (s_ctx.buffer_index * LCD_FB_BYTES));
    H750_REG32(H750_LTDC_BASE + LTDC_SRCR) = LTDC_SRCR_VBR;
    s_ctx.frames++;
}

void lcd_pixel(int32_t x, int32_t y, uint16_t color)
{
    if ((s_ctx.fb == 0) || (x < 0) || (y < 0) ||
        (x >= (int32_t)H750_LCD_WIDTH) || (y >= (int32_t)H750_LCD_HEIGHT)) {
        return;
    }
    s_ctx.fb[(uint32_t)y * H750_LCD_WIDTH + (uint32_t)x] = color;
}

void lcd_hline(int32_t x, int32_t y, int32_t w, uint16_t color)
{
    int32_t i;
    for (i = 0; i < w; i++) {
        lcd_pixel(x + i, y, color);
    }
}

void lcd_vline(int32_t x, int32_t y, int32_t h, uint16_t color)
{
    int32_t i;
    for (i = 0; i < h; i++) {
        lcd_pixel(x, y + i, color);
    }
}

void lcd_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    int32_t j;
    for (j = 0; j < h; j++) {
        lcd_hline(x, y + j, w, color);
    }
}

void lcd_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
    lcd_hline(x, y, w, color);
    lcd_hline(x, y + h - 1, w, color);
    lcd_vline(x, y, h, color);
    lcd_vline(x + w - 1, y, h, color);
}

void lcd_clear(uint16_t color)
{
    uint32_t i;
    if (s_ctx.fb == 0) {
        return;
    }
    for (i = 0u; i < (H750_LCD_WIDTH * H750_LCD_HEIGHT); i++) {
        s_ctx.fb[i] = color;
    }
}

void lcd_bar(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t permille,
             uint16_t fg, uint16_t bg)
{
    int32_t fill;
    if (permille > 1000u) {
        permille = 1000u;
    }
    lcd_fill_rect(x, y, w, h, bg);
    fill = (int32_t)(((uint32_t)w * permille) / 1000u);
    if (fill > 0) {
        lcd_fill_rect(x, y, fill, h, fg);
    }
    lcd_rect(x, y, w, h, LCD_GRAY);
}

/* 七段编码：bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G */
static const uint8_t s_seg_map[16] = {
    0x3Fu, 0x06u, 0x5Bu, 0x4Fu, 0x66u, 0x6Du, 0x7Du, 0x07u,
    0x7Fu, 0x6Fu, 0x77u, 0x7Cu, 0x39u, 0x5Eu, 0x79u, 0x71u
};

void lcd_seg_digit(int32_t x, int32_t y, int32_t scale, uint8_t digit, uint16_t color)
{
    uint8_t seg;
    int32_t t = scale;              /* 笔画粗细 */
    int32_t w = scale * 5;          /* 数字宽 */
    int32_t h = scale * 9;          /* 数字高 */
    int32_t mid = h / 2;

    if (digit > 15u) {
        digit = 15u;
    }
    seg = s_seg_map[digit];
    if ((seg & 0x01u) != 0u) { lcd_fill_rect(x + t, y, w - 2 * t, t, color); }                     /* A */
    if ((seg & 0x02u) != 0u) { lcd_fill_rect(x + w - t, y + t, t, mid - t - t / 2, color); }        /* B */
    if ((seg & 0x04u) != 0u) { lcd_fill_rect(x + w - t, y + mid + t / 2, t, mid - t - t / 2, color);}/* C */
    if ((seg & 0x08u) != 0u) { lcd_fill_rect(x + t, y + h - t, w - 2 * t, t, color); }              /* D */
    if ((seg & 0x10u) != 0u) { lcd_fill_rect(x, y + mid + t / 2, t, mid - t - t / 2, color); }      /* E */
    if ((seg & 0x20u) != 0u) { lcd_fill_rect(x, y + t, t, mid - t - t / 2, color); }                /* F */
    if ((seg & 0x40u) != 0u) { lcd_fill_rect(x + t, y + mid - t / 2, w - 2 * t, t, color); }        /* G */
}

void lcd_seg_number(int32_t x, int32_t y, int32_t scale, uint32_t value,
                    uint32_t digits, uint16_t color)
{
    uint32_t i;
    int32_t step = scale * 7;
    for (i = 0u; i < digits; i++) {
        uint32_t shift = (digits - 1u - i) * 4u;
        uint8_t d = (shift < 32u) ? (uint8_t)((value >> shift) & 0xFu) : 0u;
        lcd_seg_digit(x + (int32_t)i * step, y, scale, d, color);
    }
}

uint32_t lcd_render_status(const lcd_status_t *st)
{
    uint32_t lit = 0u;
    uint32_t i;
    lcd_status_t z;

    if (st == 0) {
        (void)memset(&z, 0, sizeof(z));
        st = &z;
    }
    /* 双缓冲：先渲染到后台缓冲，统计完成后再切换，避免撕裂 */
    s_ctx.fb = s_fb[1u - s_ctx.buffer_index];
    lcd_clear(LCD_DARK);
    /* 顶栏 */
    lcd_fill_rect(0, 0, H750_LCD_WIDTH, 34, LCD_BLACK);
    lcd_hline(0, 34, H750_LCD_WIDTH, LCD_GRAY);
    /* 链路状态块 */
    lcd_fill_rect(12, 8, 18, 18, st->link_up ? LCD_GREEN : LCD_RED);
    lcd_fill_rect(40, 8, 18, 18, st->sd_present ? LCD_GREEN : LCD_RED);
    lcd_fill_rect(68, 8, 18, 18, st->recovering ? LCD_YELLOW : LCD_GREEN);
    lcd_fill_rect(96, 8, 18, 18, st->sniff_mode ? LCD_CYAN : LCD_GRAY);

    /* CAN1 帧率与错误数 */
    lcd_seg_number(30, 60, 3, st->can1_fps, 4u, LCD_WHITE);
    lcd_seg_number(30, 130, 2, st->can1_errors, 4u, LCD_RED);
    lcd_seg_number(30, 180, 3, st->can2_fps, 4u, LCD_WHITE);
    lcd_seg_number(30, 250, 2, st->can2_errors, 4u, LCD_RED);

    /* 缓存水位与丢包计数 */
    lcd_bar(300, 70, 460, 26, st->cache_watermark_pm,
            (st->cache_watermark_pm > 700u) ? LCD_RED : LCD_GREEN, LCD_BLACK);
    lcd_seg_number(300, 120, 3, st->cache_dropped, 5u, LCD_YELLOW);

    /* 文件数与错误日志数 */
    lcd_seg_number(300, 200, 3, st->sd_files, 3u, LCD_CYAN);
    lcd_seg_number(300, 280, 3, st->log_errors, 4u, LCD_RED);

    /* 外框 */
    lcd_rect(0, 0, H750_LCD_WIDTH, H750_LCD_HEIGHT, LCD_GRAY);

    /* 统计非背景像素 */
    for (i = 0u; i < (H750_LCD_WIDTH * H750_LCD_HEIGHT); i++) {
        if (s_ctx.fb[i] != LCD_DARK) {
            lit++;
        }
    }
    s_ctx.lit_pixels = lit;
    lcd_swap();
    diag_log_write(H750_LOG_DEBUG, DIAG_MOD_LCD, 0x0A00u, &lit, 4u);
    return lit;
}

uint32_t lcd_count_color(uint16_t color)
{
    uint32_t i;
    uint32_t n = 0u;
    if (s_ctx.fb == 0) {
        return 0u;
    }
    for (i = 0u; i < (H750_LCD_WIDTH * H750_LCD_HEIGHT); i++) {
        if (s_ctx.fb[i] == color) {
            n++;
        }
    }
    return n;
}

uint32_t lcd_framebuffer_crc(void)
{
    if (s_ctx.fb == 0) {
        return 0u;
    }
    return fs_crc32((const uint8_t *)s_ctx.fb, LCD_FB_BYTES);
}
