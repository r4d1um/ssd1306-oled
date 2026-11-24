/* SPDX-License-Identifier: MIT */
#include "ssd1306_gfx.h"

#include <string.h>

/* Framebuffer layout: byte (page * width + x), bit (y % 8), bit 0 = top row of the page -
 * exactly what the controller expects, so a flush is a plain copy. */

/* set one pixel, returns 1 when the byte changed */
static int put(ssd1306 *d, int16_t x, int16_t y, uint8_t color)
{
    uint8_t *b = d->fb + (size_t)(y >> 3) * d->cfg.width + x;
    uint8_t m = (uint8_t)(1u << (y & 7)), old = *b;
    if (color == SSD1306_WHITE)
        *b = (uint8_t)(old | m);
    else if (color == SSD1306_INVERSE)
        *b = (uint8_t)(old ^ m);
    else
        *b = (uint8_t)(old & (uint8_t)~m);
    return *b != old;
}

static int16_t W(const ssd1306 *d) { return (int16_t)d->cfg.width; }
static int16_t H(const ssd1306 *d) { return (int16_t)d->cfg.height; }

/* Only bytes that really change are marked dirty, so "clear, draw everything again,
 * flush" sends just the difference to the last frame. */
void ssd1306_fill(ssd1306 *d, uint8_t color)
{
    uint8_t v = color ? 0xFF : 0x00, p;
    for (p = 0; p < d->pages; p++) {
        uint8_t *row = d->fb + (size_t)p * d->cfg.width;
        int16_t x, first = -1, last = -1;
        for (x = 0; x < W(d); x++) {
            if (row[x] != v) {
                if (first < 0)
                    first = x;
                last = x;
                row[x] = v;
            }
        }
        if (first >= 0)
            ssd1306_mark_dirty(d, p, (uint8_t)first, (uint8_t)last);
    }
}

void ssd1306_clear(ssd1306 *d)
{
    ssd1306_fill(d, SSD1306_BLACK);
    d->cur_x = 0;
    d->cur_y = 0;
}

void ssd1306_pixel(ssd1306 *d, int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || y < 0 || x >= W(d) || y >= H(d))
        return;
    if (put(d, x, y, color))
        ssd1306_mark_dirty(d, (uint8_t)(y >> 3), (uint8_t)x, (uint8_t)x);
}

int ssd1306_get_pixel(const ssd1306 *d, int16_t x, int16_t y)
{
    if (x < 0 || y < 0 || x >= W(d) || y >= H(d))
        return 0;
    return (d->fb[(size_t)(y >> 3) * d->cfg.width + x] >> (y & 7)) & 1;
}

void ssd1306_hline(ssd1306 *d, int16_t x, int16_t y, int16_t w, uint8_t color)
{
    int16_t x1;
    if (w < 0) {
        x = (int16_t)(x + w + 1);
        w = (int16_t)-w;
    }
    if (y < 0 || y >= H(d) || w == 0)
        return;
    x1 = (int16_t)(x + w - 1);
    if (x < 0)
        x = 0;
    if (x1 >= W(d))
        x1 = (int16_t)(W(d) - 1);
    if (x > x1)
        return;
    for (; x <= x1; x++)
        if (put(d, x, y, color))
            ssd1306_mark_dirty(d, (uint8_t)(y >> 3), (uint8_t)x, (uint8_t)x);
}

/* vertical run in one column, y0..y1 already clipped; whole bytes where possible */
static void column(ssd1306 *d, int16_t x, int16_t y0, int16_t y1, uint8_t color)
{
    while (y0 <= y1) {
        int16_t page = (int16_t)(y0 >> 3);
        int16_t end = (int16_t)(page * 8 + 7) < y1 ? (int16_t)(page * 8 + 7) : y1;
        uint8_t m = (uint8_t)((0xFFu << (y0 & 7)) & (0xFFu >> (7 - (end & 7))));
        uint8_t *b = d->fb + (size_t)page * d->cfg.width + x, old = *b;
        if (color == SSD1306_WHITE)
            *b = (uint8_t)(old | m);
        else if (color == SSD1306_INVERSE)
            *b = (uint8_t)(old ^ m);
        else
            *b = (uint8_t)(old & (uint8_t)~m);
        if (*b != old)
            ssd1306_mark_dirty(d, (uint8_t)page, (uint8_t)x, (uint8_t)x);
        y0 = (int16_t)(end + 1);
    }
}

void ssd1306_fill_rect(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    int16_t x1, y1;
    if (w <= 0 || h <= 0)
        return;
    x1 = (int16_t)(x + w - 1);
    y1 = (int16_t)(y + h - 1);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 >= W(d)) x1 = (int16_t)(W(d) - 1);
    if (y1 >= H(d)) y1 = (int16_t)(H(d) - 1);
    if (x > x1 || y > y1)
        return;
    for (; x <= x1; x++)
        column(d, x, y, y1, color);
}

void ssd1306_vline(ssd1306 *d, int16_t x, int16_t y, int16_t h, uint8_t color)
{
    if (h < 0) {
        y = (int16_t)(y + h + 1);
        h = (int16_t)-h;
    }
    ssd1306_fill_rect(d, x, y, 1, h, color);
}

void ssd1306_line(ssd1306 *d, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color)
{
    int16_t dx, dy, sx, sy;
    int32_t err;
    if (y0 == y1) {
        ssd1306_hline(d, x0 < x1 ? x0 : x1, y0, (int16_t)((x0 < x1 ? x1 - x0 : x0 - x1) + 1), color);
        return;
    }
    if (x0 == x1) {
        ssd1306_vline(d, x0, y0 < y1 ? y0 : y1, (int16_t)((y0 < y1 ? y1 - y0 : y0 - y1) + 1), color);
        return;
    }
    dx = (int16_t)(x1 > x0 ? x1 - x0 : x0 - x1);
    dy = (int16_t)-(y1 > y0 ? y1 - y0 : y0 - y1);
    sx = (int16_t)(x0 < x1 ? 1 : -1);
    sy = (int16_t)(y0 < y1 ? 1 : -1);
    err = dx + dy;
    for (;;) {
        int32_t e2;
        ssd1306_pixel(d, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 = (int16_t)(x0 + sx);
        }
        if (e2 <= dx) {
            err += dx;
            y0 = (int16_t)(y0 + sy);
        }
    }
}

void ssd1306_rect(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    if (w <= 0 || h <= 0)
        return;
    ssd1306_hline(d, x, y, w, color);
    if (h > 1)
        ssd1306_hline(d, x, (int16_t)(y + h - 1), w, color);
    if (h > 2) {
        ssd1306_vline(d, x, (int16_t)(y + 1), (int16_t)(h - 2), color);
        if (w > 1)
            ssd1306_vline(d, (int16_t)(x + w - 1), (int16_t)(y + 1), (int16_t)(h - 2), color);
    }
}

/* Midpoint circle, octant points for x >= 1 (the 4 axis points are left to the caller, so
 * nothing is drawn twice and INVERSE works). corners: bit0 top-left, bit1 top-right,
 * bit2 bottom-right, bit3 bottom-left; corner centers cx0/cx1, cy0/cy1 (all equal for a circle). */
static void arcs(ssd1306 *d, int16_t cx0, int16_t cy0, int16_t cx1, int16_t cy1, int16_t r,
                 uint8_t corners, uint8_t color)
{
    int16_t x = 0, y = r;
    int32_t f = 1 - r, ddx = 1, ddy = -2 * (int32_t)r;
    for (;;) {
        if (f >= 0) {
            y--;
            ddy += 2;
            f += ddy;
        }
        x++;
        ddx += 2;
        f += ddx;
        if (x > y)
            break;
        if (corners & 1) {
            ssd1306_pixel(d, (int16_t)(cx0 - x), (int16_t)(cy0 - y), color);
            if (x != y) ssd1306_pixel(d, (int16_t)(cx0 - y), (int16_t)(cy0 - x), color);
        }
        if (corners & 2) {
            ssd1306_pixel(d, (int16_t)(cx1 + x), (int16_t)(cy0 - y), color);
            if (x != y) ssd1306_pixel(d, (int16_t)(cx1 + y), (int16_t)(cy0 - x), color);
        }
        if (corners & 4) {
            ssd1306_pixel(d, (int16_t)(cx1 + x), (int16_t)(cy1 + y), color);
            if (x != y) ssd1306_pixel(d, (int16_t)(cx1 + y), (int16_t)(cy1 + x), color);
        }
        if (corners & 8) {
            ssd1306_pixel(d, (int16_t)(cx0 - x), (int16_t)(cy1 + y), color);
            if (x != y) ssd1306_pixel(d, (int16_t)(cx0 - y), (int16_t)(cy1 + x), color);
        }
    }
}

void ssd1306_circle(ssd1306 *d, int16_t cx, int16_t cy, int16_t r, uint8_t color)
{
    if (r < 0)
        return;
    if (r == 0) {
        ssd1306_pixel(d, cx, cy, color);
        return;
    }
    ssd1306_pixel(d, cx, (int16_t)(cy - r), color);
    ssd1306_pixel(d, cx, (int16_t)(cy + r), color);
    ssd1306_pixel(d, (int16_t)(cx - r), cy, color);
    ssd1306_pixel(d, (int16_t)(cx + r), cy, color);
    arcs(d, cx, cy, cx, cy, r, 0x0F, color);
}

/* filled circle / rounded corners as vertical spans; spans never overlap (INVERSE-safe):
 * for each column offset dx the half height is the largest dy with dx²+dy² <= r²+r */
static int16_t half_height(int16_t r, int16_t dx)
{
    int32_t lim = (int32_t)r * r + r;
    int16_t dy = r;
    while (dy > 0 && (int32_t)dx * dx + (int32_t)dy * dy > lim)
        dy--;
    return dy;
}

void ssd1306_fill_circle(ssd1306 *d, int16_t cx, int16_t cy, int16_t r, uint8_t color)
{
    int16_t dx;
    if (r < 0)
        return;
    for (dx = (int16_t)-r; dx <= r; dx++) {
        int16_t hh = half_height(r, (int16_t)(dx < 0 ? -dx : dx));
        ssd1306_vline(d, (int16_t)(cx + dx), (int16_t)(cy - hh), (int16_t)(2 * hh + 1), color);
    }
}

void ssd1306_round_rect(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color)
{
    int16_t max = (int16_t)((w < h ? w : h) / 2);
    if (w <= 0 || h <= 0)
        return;
    if (r > max) r = max;
    if (r <= 0) {
        ssd1306_rect(d, x, y, w, h, color);
        return;
    }
    ssd1306_hline(d, (int16_t)(x + r), y, (int16_t)(w - 2 * r), color);
    ssd1306_hline(d, (int16_t)(x + r), (int16_t)(y + h - 1), (int16_t)(w - 2 * r), color);
    ssd1306_vline(d, x, (int16_t)(y + r), (int16_t)(h - 2 * r), color);
    ssd1306_vline(d, (int16_t)(x + w - 1), (int16_t)(y + r), (int16_t)(h - 2 * r), color);
    arcs(d, (int16_t)(x + r), (int16_t)(y + r), (int16_t)(x + w - 1 - r), (int16_t)(y + h - 1 - r), r, 0x0F, color);
}

void ssd1306_fill_round_rect(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color)
{
    int16_t max = (int16_t)((w < h ? w : h) / 2), i;
    if (w <= 0 || h <= 0)
        return;
    if (r > max) r = max;
    if (r <= 0) {
        ssd1306_fill_rect(d, x, y, w, h, color);
        return;
    }
    ssd1306_fill_rect(d, (int16_t)(x + r), y, (int16_t)(w - 2 * r), h, color);
    for (i = 1; i <= r; i++) {
        int16_t hh = half_height(r, i);
        int16_t top = (int16_t)(y + r - hh), len = (int16_t)(h - 2 * r + 2 * hh);
        ssd1306_vline(d, (int16_t)(x + r - i), top, len, color);
        ssd1306_vline(d, (int16_t)(x + w - 1 - r + i), top, len, color);
    }
}

void ssd1306_triangle(ssd1306 *d, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, uint8_t color)
{
    ssd1306_line(d, x0, y0, x1, y1, color);
    ssd1306_line(d, x1, y1, x2, y2, color);
    ssd1306_line(d, x2, y2, x0, y0, color);
}

static void swap16(int16_t *a, int16_t *b)
{
    int16_t t = *a;
    *a = *b;
    *b = t;
}

/* x on edge (xa,ya)-(xb,yb) at row y, rounded to nearest */
static int16_t edge_x(int16_t xa, int16_t ya, int16_t xb, int16_t yb, int16_t y)
{
    int32_t num, den = yb - ya;
    if (den == 0)
        return xa;
    num = (int32_t)(xb - xa) * (y - ya) * 2 + den;
    return (int16_t)(xa + (num >= 0 ? num / (2 * den) : -((-num + 2 * den - 1) / (2 * den))));
}

void ssd1306_fill_triangle(ssd1306 *d, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                           int16_t x2, int16_t y2, uint8_t color)
{
    int16_t y;
    /* sort by y */
    if (y0 > y1) { swap16(&y0, &y1); swap16(&x0, &x1); }
    if (y1 > y2) { swap16(&y1, &y2); swap16(&x1, &x2); }
    if (y0 > y1) { swap16(&y0, &y1); swap16(&x0, &x1); }
    for (y = y0; y <= y2; y++) {
        int16_t a = edge_x(x0, y0, x2, y2, y);
        int16_t b = y < y1 ? edge_x(x0, y0, x1, y1, y) : edge_x(x1, y1, x2, y2, y);
        if (y0 == y2) { /* flat: span of all three */
            a = x0 < x1 ? x0 : x1;
            a = a < x2 ? a : x2;
            b = x0 > x1 ? x0 : x1;
            b = b > x2 ? b : x2;
        }
        if (a > b)
            swap16(&a, &b);
        ssd1306_hline(d, a, y, (int16_t)(b - a + 1), color);
    }
}

void ssd1306_bitmap(ssd1306 *d, int16_t x, int16_t y, const uint8_t *bits, int16_t w, int16_t h,
                    uint8_t color, uint8_t bg)
{
    int16_t row, col, stride = (int16_t)((w + 7) / 8);
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            int on = (bits[(size_t)row * stride + col / 8] >> (7 - (col & 7))) & 1;
            if (on)
                ssd1306_pixel(d, (int16_t)(x + col), (int16_t)(y + row), color);
            else if (bg != 0xFF)
                ssd1306_pixel(d, (int16_t)(x + col), (int16_t)(y + row), bg);
        }
    }
}

void ssd1306_bar(ssd1306 *d, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t value, uint32_t max)
{
    int16_t inner = (int16_t)(w - 4), filled;
    if (w < 5 || h < 5)
        return;
    if (max == 0 || value > max)
        value = max;
    filled = max ? (int16_t)(((uint32_t)inner * value + max / 2) / max) : 0;
    ssd1306_rect(d, x, y, w, h, SSD1306_WHITE);
    ssd1306_fill_rect(d, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(w - 2), (int16_t)(h - 2), SSD1306_BLACK);
    ssd1306_fill_rect(d, (int16_t)(x + 2), (int16_t)(y + 2), filled, (int16_t)(h - 4), SSD1306_WHITE);
}
