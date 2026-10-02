/*
 * lcd_diag.h
 * ---------------------------------------------------------------------------
 * LCD 现场诊断界面：RGB565 帧缓冲 + 基础绘图 + 七段数码管数字渲染 +
 * 诊断状态页组合。不依赖字库文件（七段与色块全部由几何图元绘制），
 * 在没有上位机的现场可以直接看到总线状态、缓存水位与错误计数。
 */
#ifndef H750_LCD_DIAG_H
#define H750_LCD_DIAG_H

#include <stdint.h>
#include "h750_config.h"

#define LCD_RGB565(r, g, b)  ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | (((b) & 0xF8u) >> 3)))
#define LCD_BLACK   LCD_RGB565(0u, 0u, 0u)
#define LCD_WHITE   LCD_RGB565(255u, 255u, 255u)
#define LCD_RED     LCD_RGB565(255u, 0u, 0u)
#define LCD_GREEN   LCD_RGB565(0u, 255u, 0u)
#define LCD_BLUE    LCD_RGB565(0u, 0u, 255u)
#define LCD_YELLOW  LCD_RGB565(255u, 255u, 0u)
#define LCD_CYAN    LCD_RGB565(0u, 255u, 255u)
#define LCD_GRAY    LCD_RGB565(96u, 96u, 96u)
#define LCD_DARK    LCD_RGB565(24u, 32u, 40u)

typedef struct {
    uint16_t *fb;           /* 当前帧缓冲（RGB565） */
    uint32_t  bytes;        /* 帧缓冲字节数 */
    uint32_t  frames;       /* 已渲染帧数 */
    uint32_t  lit_pixels;   /* 最近一帧的非背景像素数 */
    uint8_t   buffer_index; /* 双缓冲索引 */
    uint8_t   ready;
} lcd_ctx_t;

typedef struct {
    uint32_t can1_fps;
    uint32_t can2_fps;
    uint32_t can1_errors;
    uint32_t can2_errors;
    uint32_t cache_watermark_pm;
    uint32_t cache_dropped;
    uint32_t sd_files;
    uint32_t log_errors;
    uint8_t  link_up;
    uint8_t  sd_present;
    uint8_t  bus_off_can1;
    uint8_t  bus_off_can2;
    uint8_t  recovering;
    uint8_t  sniff_mode;
} lcd_status_t;

int      lcd_init(void);
int      lcd_get_ctx(lcd_ctx_t *ctx);
void     lcd_swap(void);
void     lcd_clear(uint16_t color);
void     lcd_pixel(int32_t x, int32_t y, uint16_t color);
void     lcd_hline(int32_t x, int32_t y, int32_t w, uint16_t color);
void     lcd_vline(int32_t x, int32_t y, int32_t h, uint16_t color);
void     lcd_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
void     lcd_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
/* 水位条：permille = 0..1000 */
void     lcd_bar(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t permille,
                 uint16_t fg, uint16_t bg);
/* 七段数码管：digit = 0..15（10=A,11=b,12=C,13=d,14=E,15=F） */
void     lcd_seg_digit(int32_t x, int32_t y, int32_t scale, uint8_t digit, uint16_t color);
void     lcd_seg_number(int32_t x, int32_t y, int32_t scale, uint32_t value,
                        uint32_t digits, uint16_t color);
/* 组合诊断页：返回本帧非背景像素数 */
uint32_t lcd_render_status(const lcd_status_t *st);
/* 帧缓冲校验（用于验证渲染结果稳定） */
uint32_t lcd_framebuffer_crc(void);
uint32_t lcd_count_color(uint16_t color);

#endif /* H750_LCD_DIAG_H */
