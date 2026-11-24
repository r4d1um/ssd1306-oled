/* SPDX-License-Identifier: MIT */
/*
 * ssd1306_text - text with the built-in 5x7 font (6x8 pixel cells, 21 x 8 characters on a
 * 128x64 panel at scale 1). Strings are UTF-8: "23.5 °C", "Grüße", "→ 5 €" come out right.
 * Code points without a glyph are drawn as '?'. Scale 1..8 multiplies the cell size.
 *
 * The cursor (top-left corner of the next cell), scale, colours and line wrap live in the
 * ssd1306 struct. ssd1306_put_byte() takes the UTF-8 stream byte by byte (for Arduino Print).
 */
#ifndef SSD1306_TEXT_H
#define SSD1306_TEXT_H

#include "ssd1306.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_CHAR_W 6u /* cell width at scale 1 (5 columns + 1 spacing) */
#define SSD1306_CHAR_H 8u /* cell height at scale 1 */
#define SSD1306_TRANSPARENT 0xFFu

typedef enum { SSD1306_LEFT = 0, SSD1306_CENTER = 1, SSD1306_RIGHT = 2 } ssd1306_align;

void ssd1306_set_cursor(ssd1306 *d, int16_t x, int16_t y);
void ssd1306_set_text_scale(ssd1306 *d, uint8_t scale);       /* 1..8, default 1 */
/* fg: SSD1306_WHITE/BLACK/INVERSE; bg: colour of the rest of each cell, or SSD1306_TRANSPARENT
 * (default) to leave it untouched. */
void ssd1306_set_text_color(ssd1306 *d, uint8_t fg, uint8_t bg);
void ssd1306_set_text_wrap(ssd1306 *d, int on);                /* wrap at the right edge, default on */

/* UTF-8 text at the cursor; '\n' goes to the start of the next line, '\r' is ignored.
 * Returns the number of characters drawn. */
size_t ssd1306_print(ssd1306 *d, const char *utf8);
size_t ssd1306_print_n(ssd1306 *d, const char *utf8, size_t len);
/* One byte of a UTF-8 stream (keeps partial sequences between calls). */
void ssd1306_put_byte(ssd1306 *d, uint8_t b);
/* Text at (x, y) aligned left/centred/right to x, cursor left after it. No wrap. */
size_t ssd1306_print_at(ssd1306 *d, int16_t x, int16_t y, ssd1306_align align, const char *utf8);

/* One glyph at (x, y) without touching the cursor. */
void ssd1306_glyph(ssd1306 *d, int16_t x, int16_t y, uint32_t cp, uint8_t scale, uint8_t fg, uint8_t bg);

/* Width in pixels of the first line of utf8 at scale (cells * 6 * scale, minus the trailing
 * spacing column). */
uint16_t ssd1306_text_width(const char *utf8, uint8_t scale);
/* Number of characters (code points) in the first line. */
size_t ssd1306_text_length(const char *utf8);

#ifdef __cplusplus
}
#endif
#endif /* SSD1306_TEXT_H */
