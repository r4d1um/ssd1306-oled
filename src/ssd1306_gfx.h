/* SPDX-License-Identifier: MIT */
/*
 * ssd1306_gfx - drawing into the framebuffer: pixels, lines, rectangles, circles, bitmaps.
 * Everything is clipped to the panel and marks only the touched columns dirty; nothing is sent
 * until ssd1306_flush(). Coordinates are int16_t, so shapes may start off-screen.
 */
#ifndef SSD1306_GFX_H
#define SSD1306_GFX_H

#include "ssd1306.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_BLACK   0u
#define SSD1306_WHITE   1u
#define SSD1306_INVERSE 2u /* flip the pixel */

void ssd1306_clear(ssd1306 *d);                       /* all black (marks all dirty) */
void ssd1306_fill(ssd1306 *d, uint8_t color);         /* all black or white */
void ssd1306_pixel(ssd1306 *d, int16_t x, int16_t y, uint8_t color);
int  ssd1306_get_pixel(const ssd1306 *d, int16_t x, int16_t y); /* 0/1, 0 when off-screen */

void ssd1306_hline(ssd1306 *d, int16_t x, int16_t y, int16_t w, uint8_t color);
void ssd1306_vline(ssd1306 *d, int16_t x, int16_t y, int16_t h, uint8_t color);
void ssd1306_line(ssd1306 *d, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color);
void ssd1306_rect(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void ssd1306_fill_rect(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void ssd1306_round_rect(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color);
void ssd1306_fill_round_rect(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color);
void ssd1306_circle(ssd1306 *d, int16_t cx, int16_t cy, int16_t r, uint8_t color);
void ssd1306_fill_circle(ssd1306 *d, int16_t cx, int16_t cy, int16_t r, uint8_t color);
void ssd1306_triangle(ssd1306 *d, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, uint8_t color);
void ssd1306_fill_triangle(ssd1306 *d, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                           int16_t x2, int16_t y2, uint8_t color);

/* Bitmap, rows top to bottom, each row (w+7)/8 bytes, MSB = leftmost pixel (the format of
 * most image converters). Set bits are drawn in color; bg = 0xFF leaves clear bits untouched,
 * otherwise they are drawn in bg. */
void ssd1306_bitmap(ssd1306 *d, int16_t x, int16_t y, const uint8_t *bits, int16_t w, int16_t h,
                    uint8_t color, uint8_t bg);
/* Bar for gauges: outline w x h, filled value/max of the inner width. */
void ssd1306_bar(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t value, uint32_t max);

#ifdef __cplusplus
}
#endif
#endif /* SSD1306_GFX_H */
