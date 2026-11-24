/* SPDX-License-Identifier: MIT */
/*
 * ssd1306 - driver core for SSD1306 / SH1106 monochrome OLED controllers, C99.
 *
 * The core knows the controller (init sequence, addressing, contrast, rotation) and keeps a
 * framebuffer in RAM, but it has no hardware access: the caller passes a bus with a write
 * callback. I2C (control byte 0x00 / 0x40) and 4-wire SPI (D/C pin) both fit that callback;
 * the Arduino adapter Ssd1306Oled.h ships ready-made ones.
 *
 * Drawing goes into the framebuffer (ssd1306_gfx.h, ssd1306_text.h) and marks the touched
 * columns of each page dirty. ssd1306_flush() then sends only the changed column range of each
 * changed page, so updating a clock digit costs a few dozen bytes instead of the full 1 KiB.
 *
 * No malloc, no float. The framebuffer memory comes from the caller:
 *     static uint8_t fb[SSD1306_FB_BYTES(128, 64)];
 */
#ifndef SSD1306_H
#define SSD1306_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_VERSION "0.1.0"

/* Bytes of framebuffer for a w x h panel (one bit per pixel, 8 rows per byte). */
#define SSD1306_FB_BYTES(w, h) ((size_t)(w) * (((size_t)(h) + 7u) / 8u))

#define SSD1306_MAX_WIDTH  132u /* SH1106 RAM is 132 columns wide */
#define SSD1306_MAX_PAGES  8u   /* 64 rows */

/* 7-bit I2C addresses (the board's "0x78"/"0x7A" are the same addresses shifted left by one). */
#define SSD1306_I2C_ADDR      0x3Cu
#define SSD1306_I2C_ADDR_ALT  0x3Du

typedef enum {
    SSD1306_CTRL_SSD1306 = 0, /* SSD1306, SSD1315, SSD1309 (common 0.96" / 0.91" modules) */
    SSD1306_CTRL_SH1106  = 1  /* SH1106 (common 1.3" modules): page mode only, 132-column RAM */
} ssd1306_controller;

typedef struct {
    /* Send len bytes to the controller. data = 0: command bytes, data = 1: display RAM bytes.
     * I2C: prefix the bytes with control byte 0x00 (commands) or 0x40 (data) in one transaction.
     * SPI: D/C low for commands, high for data. Returns 0 on success, non-zero on a bus error
     * (e.g. no I2C ACK). */
    int (*write)(void *ctx, uint8_t data, const uint8_t *buf, size_t len);
    /* Optional: wait at least ms milliseconds (only used during init). NULL = no waiting. */
    void (*delay_ms)(void *ctx, uint32_t ms);
    void *ctx;
    /* Most bytes the bus takes per write call (e.g. the I2C buffer minus the control byte);
     * longer runs are split. 0 = no limit. */
    uint16_t max_chunk;
} ssd1306_bus;

typedef struct {
    uint8_t width;         /* visible columns: 128, 96, 72, 64 */
    uint8_t height;        /* visible rows: 64, 48, 40, 32, 16 (multiple of 8) */
    ssd1306_controller controller;
    uint8_t col_offset;    /* first RAM column of the visible area (SH1106 128x64: 2, 64x48: 32) */
    uint8_t com_pins;      /* COM pins configuration (command 0xDA); 0 = pick from the height */
    uint8_t external_vcc;  /* 1 = panel has its own VCC supply, 0 = internal charge pump (usual) */
    uint8_t rotate180;     /* 1 = upside down (header pins at the bottom) */
    uint8_t contrast;      /* 0..255 */
} ssd1306_config;

typedef struct {
    ssd1306_bus    bus;
    ssd1306_config cfg;
    uint8_t       *fb;                         /* SSD1306_FB_BYTES(width, height) bytes */
    uint8_t       *shadow;                     /* optional copy of the display RAM, see ssd1306_set_shadow() */
    uint8_t        pages;                      /* height / 8 */
    uint8_t        dirty_lo[SSD1306_MAX_PAGES]; /* changed columns per page: lo..hi, lo > hi = clean */
    uint8_t        dirty_hi[SSD1306_MAX_PAGES];
    int            error;                      /* first bus error since init, 0 = none */
    uint32_t       bytes_sent;                 /* data bytes sent since init (for statistics) */
    /* text state, see ssd1306_text.h */
    int16_t        cur_x, cur_y;
    uint8_t        text_scale, text_fg, text_bg, text_wrap;
    uint8_t        utf8_need;
    uint32_t       utf8_cp;
} ssd1306;

/* Defaults for a panel: SSD1306, column offset/COM pins for the known sizes (64x48: offset 32,
 * 72x40: offset 28, 64x32: offset 32), contrast 0x7F. Pick SH1106 afterwards if needed. */
void ssd1306_default_config(ssd1306_config *cfg, uint8_t width, uint8_t height);
/* Same for the SH1106: column offset 2 on 128-wide panels. */
void ssd1306_default_config_sh1106(ssd1306_config *cfg, uint8_t width, uint8_t height);

/* Initialise the controller: send the init sequence, clear the display RAM and the framebuffer,
 * switch the display on. fb must hold SSD1306_FB_BYTES(width, height) bytes. Returns 0, a bus
 * error, or -1 for an unsupported configuration (width > 132 - col_offset, height not 8..64 in
 * steps of 8). */
int ssd1306_init(ssd1306 *d, const ssd1306_bus *bus, const ssd1306_config *cfg, uint8_t *fb);

/* Optional shadow buffer (another SSD1306_FB_BYTES(width, height) bytes): a copy of what the
 * display shows. With it, flushes compare against the display and send only bytes that really
 * differ - so "clear, draw the whole screen again, flush" sends just the change. Without it,
 * every byte touched since the last flush is sent. Sends the whole frame once to get in sync.
 * NULL switches it off. Worth it wherever RAM allows (ESP8266, ESP32, RP2040). */
int ssd1306_set_shadow(ssd1306 *d, uint8_t *shadow);

/* Send the changed parts of the framebuffer. Returns 0 or the bus error of this call; pages
 * that could not be sent stay dirty, so the next flush retries them. */
int ssd1306_flush(ssd1306 *d);
/* Send the whole framebuffer. */
int ssd1306_flush_all(ssd1306 *d);
/* Number of framebuffer bytes the next ssd1306_flush() would send. */
size_t ssd1306_pending_bytes(const ssd1306 *d);

void ssd1306_display_on(ssd1306 *d, int on);    /* panel on/off (RAM is kept, ~µA when off) */
void ssd1306_set_contrast(ssd1306 *d, uint8_t contrast);
void ssd1306_invert(ssd1306 *d, int on);        /* hardware inversion, framebuffer unchanged */
void ssd1306_rotate180(ssd1306 *d, int on);     /* flip both axes; sends the whole frame again */
/* Hardware scroll of the whole display (SSD1306 only): dir +1 = right, -1 = left, 0 = stop.
 * frames_code: SSD1306 interval code 0..7 (0 = every 5 frames ... 7 = every 2 frames). */
void ssd1306_scroll(ssd1306 *d, int dir, uint8_t frames_code);
/* Raw command bytes (for anything not covered above). */
void ssd1306_command(ssd1306 *d, const uint8_t *cmds, size_t n);

/* Mark columns x0..x1 of page p as changed (used by the drawing functions). */
void ssd1306_mark_dirty(ssd1306 *d, uint8_t page, uint8_t x0, uint8_t x1);
/* Mark the whole framebuffer as changed. */
void ssd1306_mark_all_dirty(ssd1306 *d);

#ifdef __cplusplus
}
#endif
#endif /* SSD1306_H */
