/* SPDX-License-Identifier: MIT */
#include "ssd1306_text.h"

#include <string.h>

#include "ssd1306_font.h"
#include "ssd1306_gfx.h"

int ssd1306_font5x7_glyph(uint32_t cp, uint8_t out[5])
{
    const uint8_t *src = NULL;
    uint8_t i;
    if (cp >= 0x20 && cp <= 0x7E) {
        src = ssd1306_font5x7_ascii[cp - 0x20];
    } else {
        int lo = 0, hi = (int)ssd1306_font5x7_extra_count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            uint16_t c = SSD1306_ROM_WORD(&ssd1306_font5x7_extra_cp[mid]);
            if (c == cp) {
                src = ssd1306_font5x7_extra[mid];
                break;
            }
            if (c < cp)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
    }
    if (!src)
        return 0;
    for (i = 0; i < 5; i++)
        out[i] = SSD1306_ROM_BYTE(src + i);
    return 1;
}

/* --- UTF-8 ------------------------------------------------------------------------------- */

typedef struct {
    uint8_t  need;
    uint32_t cp;
} utf8_dec;

/* Feed one byte; writes 0..2 finished code points to out (2 when a broken sequence is
 * followed by a new start byte). Broken input becomes '?'. */
static int utf8_feed(utf8_dec *st, uint8_t b, uint32_t out[2])
{
    int n = 0;
    if (st->need) {
        if ((b & 0xC0) == 0x80) {
            st->cp = (st->cp << 6) | (b & 0x3Fu);
            if (--st->need == 0)
                out[n++] = st->cp;
            return n;
        }
        st->need = 0;
        out[n++] = '?';
    }
    if (b < 0x80) {
        out[n++] = b;
    } else if ((b & 0xE0) == 0xC0) {
        st->need = 1;
        st->cp = b & 0x1Fu;
    } else if ((b & 0xF0) == 0xE0) {
        st->need = 2;
        st->cp = b & 0x0Fu;
    } else if ((b & 0xF8) == 0xF0) {
        st->need = 3;
        st->cp = b & 0x07u;
    } else {
        out[n++] = '?';
    }
    return n;
}

/* --- drawing ----------------------------------------------------------------------------- */

static void cell(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    if (w == 1 && h == 1)
        ssd1306_pixel(d, x, y, color);
    else
        ssd1306_fill_rect(d, x, y, w, h, color);
}

void ssd1306_glyph(ssd1306 *d, int16_t x, int16_t y, uint32_t cp, uint8_t scale, uint8_t fg, uint8_t bg)
{
    uint8_t cols[5];
    int16_t s = (int16_t)(scale ? scale : 1), c, r;
    int with_bg = bg != SSD1306_TRANSPARENT && bg != fg;
    if (!ssd1306_font5x7_glyph(cp, cols))
        ssd1306_font5x7_glyph('?', cols);
    /* skip glyphs that are completely off-screen */
    if (x >= (int16_t)d->cfg.width || y >= (int16_t)d->cfg.height ||
        x + 6 * s <= 0 || y + 8 * s <= 0)
        return;
    /* every pixel of the cell is written once (fg or bg): unchanged text marks nothing dirty */
    for (c = 0; c < 6; c++) {
        uint8_t bits = c < 5 ? cols[c] : 0;
        for (r = 0; r < 8; r++) {
            if ((bits >> r) & 1)
                cell(d, (int16_t)(x + c * s), (int16_t)(y + r * s), s, s, fg);
            else if (with_bg)
                cell(d, (int16_t)(x + c * s), (int16_t)(y + r * s), s, s, bg);
        }
    }
}

void ssd1306_set_cursor(ssd1306 *d, int16_t x, int16_t y)
{
    d->cur_x = x;
    d->cur_y = y;
}

void ssd1306_set_text_scale(ssd1306 *d, uint8_t scale)
{
    d->text_scale = (uint8_t)(scale < 1 ? 1 : scale > 8 ? 8 : scale);
}

void ssd1306_set_text_color(ssd1306 *d, uint8_t fg, uint8_t bg)
{
    d->text_fg = fg;
    d->text_bg = bg;
}

void ssd1306_set_text_wrap(ssd1306 *d, int on)
{
    d->text_wrap = (uint8_t)(on != 0);
}

static void emit(ssd1306 *d, uint32_t cp)
{
    int16_t s = d->text_scale;
    if (cp == '\n') {
        d->cur_x = 0;
        d->cur_y = (int16_t)(d->cur_y + 8 * s);
        return;
    }
    if (cp == '\r')
        return;
    if (d->text_wrap && d->cur_x > 0 && d->cur_x + 5 * s > (int16_t)d->cfg.width) {
        d->cur_x = 0;
        d->cur_y = (int16_t)(d->cur_y + 8 * s);
    }
    ssd1306_glyph(d, d->cur_x, d->cur_y, cp, (uint8_t)s, d->text_fg, d->text_bg);
    d->cur_x = (int16_t)(d->cur_x + 6 * s);
}

void ssd1306_put_byte(ssd1306 *d, uint8_t b)
{
    utf8_dec st;
    uint32_t cps[2];
    int i, n;
    st.need = d->utf8_need;
    st.cp = d->utf8_cp;
    n = utf8_feed(&st, b, cps);
    d->utf8_need = st.need;
    d->utf8_cp = st.cp;
    for (i = 0; i < n; i++)
        emit(d, cps[i]);
}

size_t ssd1306_print_n(ssd1306 *d, const char *utf8, size_t len)
{
    utf8_dec st;
    uint32_t cps[2];
    size_t count = 0, i;
    int k, n;
    st.need = d->utf8_need;
    st.cp = d->utf8_cp;
    for (i = 0; i < len; i++) {
        n = utf8_feed(&st, (uint8_t)utf8[i], cps);
        for (k = 0; k < n; k++) {
            emit(d, cps[k]);
            if (cps[k] != '\n' && cps[k] != '\r')
                count++;
        }
    }
    d->utf8_need = st.need;
    d->utf8_cp = st.cp;
    return count;
}

size_t ssd1306_print(ssd1306 *d, const char *utf8)
{
    return ssd1306_print_n(d, utf8, strlen(utf8));
}

size_t ssd1306_text_length(const char *utf8)
{
    utf8_dec st = { 0, 0 };
    uint32_t cps[2];
    size_t count = 0;
    int k, n;
    for (; *utf8; utf8++) {
        n = utf8_feed(&st, (uint8_t)*utf8, cps);
        for (k = 0; k < n; k++) {
            if (cps[k] == '\n')
                return count;
            if (cps[k] != '\r')
                count++;
        }
    }
    return count + (st.need ? 1u : 0u); /* a cut-off sequence is drawn as '?' too */
}

uint16_t ssd1306_text_width(const char *utf8, uint8_t scale)
{
    size_t n = ssd1306_text_length(utf8);
    if (scale < 1)
        scale = 1;
    return n ? (uint16_t)(n * 6u * scale - scale) : 0;
}

size_t ssd1306_print_at(ssd1306 *d, int16_t x, int16_t y, ssd1306_align align, const char *utf8)
{
    uint16_t w = ssd1306_text_width(utf8, d->text_scale);
    uint8_t wrap = d->text_wrap;
    size_t n, len = 0;
    if (align == SSD1306_CENTER)
        x = (int16_t)(x - (int16_t)(w / 2));
    else if (align == SSD1306_RIGHT)
        x = (int16_t)(x - (int16_t)w);
    while (utf8[len] && utf8[len] != '\n')
        len++;
    d->text_wrap = 0;
    d->utf8_need = 0;
    ssd1306_set_cursor(d, x, y);
    n = ssd1306_print_n(d, utf8, len);
    if (d->utf8_need) { /* cut-off sequence at the end */
        d->utf8_need = 0;
        emit(d, '?');
        n++;
    }
    d->text_wrap = wrap;
    return n;
}
