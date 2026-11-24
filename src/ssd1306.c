/* SPDX-License-Identifier: MIT */
#include "ssd1306.h"

#include <string.h>

/* Both controllers are driven in page addressing mode: 0xB0|page, then the column start as two
 * nibble commands. The SH1106 knows nothing else, and it keeps the flush code the same. */

static int send(ssd1306 *d, uint8_t data, const uint8_t *buf, size_t len)
{
    size_t max = d->bus.max_chunk ? d->bus.max_chunk : len;
    while (len) {
        size_t n = len < max ? len : max;
        int err = d->bus.write(d->bus.ctx, data, buf, n);
        if (err) {
            if (!d->error)
                d->error = err;
            return err;
        }
        if (data)
            d->bytes_sent += (uint32_t)n;
        buf += n;
        len -= n;
    }
    return 0;
}

void ssd1306_command(ssd1306 *d, const uint8_t *cmds, size_t n)
{
    send(d, 0, cmds, n);
}

static void cmd1(ssd1306 *d, uint8_t c)
{
    send(d, 0, &c, 1);
}

static void cmd2(ssd1306 *d, uint8_t c, uint8_t arg)
{
    uint8_t b[2] = { c, arg };
    send(d, 0, b, 2);
}

static uint8_t known_offset(uint8_t width, uint8_t height)
{
    if (width == 64 && (height == 48 || height == 32))
        return 32;
    if (width == 72 && height == 40)
        return 28;
    return 0;
}

void ssd1306_default_config(ssd1306_config *cfg, uint8_t width, uint8_t height)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->width = width;
    cfg->height = height;
    cfg->controller = SSD1306_CTRL_SSD1306;
    cfg->col_offset = known_offset(width, height);
    cfg->contrast = 0x7F;
}

void ssd1306_default_config_sh1106(ssd1306_config *cfg, uint8_t width, uint8_t height)
{
    ssd1306_default_config(cfg, width, height);
    cfg->controller = SSD1306_CTRL_SH1106;
    /* 132-column RAM, the panel sits in the middle */
    cfg->col_offset = (uint8_t)(width <= 132 ? (132 - width) / 2 : 0);
}

static uint8_t com_pins(const ssd1306_config *cfg)
{
    if (cfg->com_pins)
        return cfg->com_pins;
    /* 0x02: sequential COM for the 128x32 and 96x16 panels; 0x12: alternative COM (the rest,
     * including the small 64x32) */
    return ((cfg->height == 32 && cfg->width == 128) || cfg->height == 16) ? 0x02 : 0x12;
}

/* first RAM column of the visible area: with the segment remap off (rotate180) the panel is
 * seen from the other end of the RAM, which matters for panels not centred in it (96x16) */
static uint8_t col_base(const ssd1306 *d)
{
    unsigned ram = d->cfg.controller == SSD1306_CTRL_SH1106 ? 132u : 128u;
    if (d->cfg.rotate180)
        return (uint8_t)(ram - d->cfg.width - d->cfg.col_offset);
    return d->cfg.col_offset;
}

static void orientation(ssd1306 *d)
{
    /* 0xA1 + 0xC8: column 0 on the left with the pin header on top (how the modules are sold) */
    if (d->cfg.rotate180) {
        cmd1(d, 0xA0);
        cmd1(d, 0xC0);
    } else {
        cmd1(d, 0xA1);
        cmd1(d, 0xC8);
    }
}

int ssd1306_init(ssd1306 *d, const ssd1306_bus *bus, const ssd1306_config *cfg, uint8_t *fb)
{
    unsigned ram = cfg->controller == SSD1306_CTRL_SH1106 ? 132u : 128u;
    memset(d, 0, sizeof *d);
    if (!bus || !bus->write || !fb || cfg->width == 0 || cfg->height < 8 || cfg->height > 64 ||
        (cfg->height % 8) != 0 || (unsigned)cfg->width + cfg->col_offset > ram)
        return -1;
    d->bus = *bus;
    d->cfg = *cfg;
    d->fb = fb;
    d->pages = (uint8_t)(cfg->height / 8);
    d->text_scale = 1;
    d->text_fg = 1;
    d->text_bg = 0xFF; /* transparent */
    d->text_wrap = 1;
    memset(fb, 0, SSD1306_FB_BYTES(cfg->width, cfg->height));
    memset(d->dirty_lo, 1, sizeof d->dirty_lo); /* all clean: lo > hi */

    if (d->bus.delay_ms)
        d->bus.delay_ms(d->bus.ctx, 100); /* VDD stable -> first command */

    cmd1(d, 0xAE);                                  /* display off */
    cmd2(d, 0xD5, 0x80);                            /* clock divide / oscillator: default */
    cmd2(d, 0xA8, (uint8_t)(cfg->height - 1));      /* multiplex ratio */
    cmd2(d, 0xD3, 0x00);                            /* display offset */
    cmd1(d, 0x40);                                  /* start line 0 */
    if (cfg->controller == SSD1306_CTRL_SH1106) {
        cmd2(d, 0xAD, cfg->external_vcc ? 0x8A : 0x8B); /* DC-DC converter */
        cmd1(d, 0x32);                              /* pump voltage 8.0 V */
    } else {
        cmd2(d, 0x8D, cfg->external_vcc ? 0x10 : 0x14); /* charge pump */
        cmd2(d, 0x20, 0x02);                        /* page addressing mode */
    }
    orientation(d);
    cmd2(d, 0xDA, com_pins(cfg));
    cmd2(d, 0x81, cfg->contrast);
    cmd2(d, 0xD9, cfg->external_vcc ? 0x22 : 0xF1); /* pre-charge period */
    cmd2(d, 0xDB, 0x40);                            /* VCOMH deselect level */
    cmd1(d, 0x2E);                                  /* no scrolling (SH1106 ignores it) */
    cmd1(d, 0xA4);                                  /* show RAM contents */
    cmd1(d, 0xA6);                                  /* not inverted */
    if (d->error)
        return d->error;

    /* clear the whole RAM, including the SH1106's hidden columns, before switching on */
    {
        static const uint8_t zeros[16] = { 0 };
        uint8_t p;
        for (p = 0; p < d->pages; p++) {
            unsigned left = ram;
            cmd1(d, (uint8_t)(0xB0 | p));
            cmd1(d, 0x00);
            cmd1(d, 0x10);
            while (left) {
                unsigned n = left < sizeof zeros ? left : (unsigned)sizeof zeros;
                send(d, 1, zeros, n);
                left -= n;
            }
        }
    }
    cmd1(d, 0xAF); /* display on */
    return d->error;
}

void ssd1306_mark_dirty(ssd1306 *d, uint8_t page, uint8_t x0, uint8_t x1)
{
    if (page >= d->pages)
        return;
    if (x1 >= d->cfg.width)
        x1 = (uint8_t)(d->cfg.width - 1);
    if (x0 > x1)
        return;
    if (d->dirty_lo[page] > d->dirty_hi[page]) { /* clean */
        d->dirty_lo[page] = x0;
        d->dirty_hi[page] = x1;
        return;
    }
    if (x0 < d->dirty_lo[page])
        d->dirty_lo[page] = x0;
    if (x1 > d->dirty_hi[page])
        d->dirty_hi[page] = x1;
}

void ssd1306_mark_all_dirty(ssd1306 *d)
{
    uint8_t p;
    for (p = 0; p < d->pages; p++) {
        d->dirty_lo[p] = 0;
        d->dirty_hi[p] = (uint8_t)(d->cfg.width - 1);
    }
}

/* columns of page p that must be sent: dirty range, narrowed to real differences when a
 * shadow exists. Returns 0 when nothing has to go out. */
static int page_range(const ssd1306 *d, uint8_t p, uint8_t *lo, uint8_t *hi)
{
    uint8_t a = d->dirty_lo[p], b = d->dirty_hi[p];
    if (a > b)
        return 0;
    if (d->shadow) {
        const uint8_t *f = d->fb + (size_t)p * d->cfg.width, *s = d->shadow + (size_t)p * d->cfg.width;
        while (a <= b && f[a] == s[a])
            a++;
        if (a > b)
            return 0;
        while (f[b] == s[b])
            b--;
    }
    *lo = a;
    *hi = b;
    return 1;
}

size_t ssd1306_pending_bytes(const ssd1306 *d)
{
    size_t n = 0;
    uint8_t p, lo, hi;
    for (p = 0; p < d->pages; p++)
        if (page_range(d, p, &lo, &hi))
            n += (size_t)(hi - lo + 1);
    return n;
}

int ssd1306_flush(ssd1306 *d)
{
    uint8_t p;
    int err;
    for (p = 0; p < d->pages; p++) {
        uint8_t lo, hi, col, addr[3];
        size_t at;
        if (!page_range(d, p, &lo, &hi)) {
            d->dirty_lo[p] = 1; /* clean: lo > hi */
            d->dirty_hi[p] = 0;
            continue;
        }
        at = (size_t)p * d->cfg.width + lo;
        col = (uint8_t)(lo + col_base(d));
        addr[0] = (uint8_t)(0xB0 | p);
        addr[1] = (uint8_t)(col & 0x0F);
        addr[2] = (uint8_t)(0x10 | (col >> 4));
        err = send(d, 0, addr, 3);
        if (!err)
            err = send(d, 1, d->fb + at, (size_t)(hi - lo + 1));
        if (err)
            return err; /* the page stays dirty, a later flush retries it */
        if (d->shadow)
            memcpy(d->shadow + at, d->fb + at, (size_t)(hi - lo + 1));
        d->dirty_lo[p] = 1;
        d->dirty_hi[p] = 0;
    }
    return 0;
}

int ssd1306_set_shadow(ssd1306 *d, uint8_t *shadow)
{
    d->shadow = NULL;
    if (!shadow)
        return 0;
    ssd1306_mark_all_dirty(d);
    if (ssd1306_flush(d) != 0)
        return d->error;
    memcpy(shadow, d->fb, SSD1306_FB_BYTES(d->cfg.width, d->cfg.height));
    d->shadow = shadow;
    return 0;
}

int ssd1306_flush_all(ssd1306 *d)
{
    uint8_t *shadow = d->shadow;
    int err;
    ssd1306_mark_all_dirty(d);
    d->shadow = NULL; /* really everything */
    err = ssd1306_flush(d);
    d->shadow = shadow;
    if (!err && shadow)
        memcpy(shadow, d->fb, SSD1306_FB_BYTES(d->cfg.width, d->cfg.height));
    return err;
}

void ssd1306_display_on(ssd1306 *d, int on)
{
    cmd1(d, on ? 0xAF : 0xAE);
}

void ssd1306_set_contrast(ssd1306 *d, uint8_t contrast)
{
    d->cfg.contrast = contrast;
    cmd2(d, 0x81, contrast);
}

void ssd1306_invert(ssd1306 *d, int on)
{
    cmd1(d, on ? 0xA7 : 0xA6);
}

void ssd1306_rotate180(ssd1306 *d, int on)
{
    d->cfg.rotate180 = (uint8_t)(on != 0);
    orientation(d);
    /* the segment remap only applies to RAM written afterwards */
    ssd1306_flush_all(d);
}

void ssd1306_scroll(ssd1306 *d, int dir, uint8_t frames_code)
{
    uint8_t c[8];
    if (d->cfg.controller != SSD1306_CTRL_SSD1306)
        return;
    cmd1(d, 0x2E); /* stop before changing the setup */
    if (dir == 0) {
        ssd1306_flush_all(d); /* scrolling moved the RAM contents */
        return;
    }
    c[0] = dir > 0 ? 0x26 : 0x27;
    c[1] = 0x00;
    c[2] = 0x00;                         /* start page */
    c[3] = (uint8_t)(frames_code & 0x07);
    c[4] = (uint8_t)(d->pages - 1);      /* end page */
    c[5] = 0x00;
    c[6] = 0xFF;
    c[7] = 0x2F;                         /* activate */
    send(d, 0, c, sizeof c);
}
