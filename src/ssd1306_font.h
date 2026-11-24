/* SPDX-License-Identifier: MIT */
/* Built-in 5x7 font (6x8 cells): ASCII plus a few symbols (° ± ² ³ µ · Ä Ö Ü ß ä ö ü € ← ↑ → ↓ █). */
#ifndef SSD1306_FONT_H
#define SSD1306_FONT_H

#include <stdint.h>

/* AVR: keep the font in flash (avr-libc, no Arduino headers needed) */
#ifdef __AVR__
#include <avr/pgmspace.h>
#define SSD1306_ROM PROGMEM
#define SSD1306_ROM_BYTE(p) pgm_read_byte(p)
#define SSD1306_ROM_WORD(p) pgm_read_word(p)
#else
#define SSD1306_ROM
#define SSD1306_ROM_BYTE(p) (*(const uint8_t *)(p))
#define SSD1306_ROM_WORD(p) (*(const uint16_t *)(p))
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t  ssd1306_font5x7_ascii[95][5] SSD1306_ROM;
extern const uint16_t ssd1306_font5x7_extra_cp[] SSD1306_ROM;
extern const uint8_t  ssd1306_font5x7_extra[][5] SSD1306_ROM;
extern const uint8_t  ssd1306_font5x7_extra_count;

/* Copy the 5 column bytes (bit 0 = top row) of a code point to out. Returns 1, or 0 when the
 * font has no glyph for it (out untouched). */
int ssd1306_font5x7_glyph(uint32_t cp, uint8_t out[5]);

#ifdef __cplusplus
}
#endif
#endif /* SSD1306_FONT_H */
