// SPDX-License-Identifier: MIT
// Fake SSD1306 / SH1106 controller for host tests and screenshots.
//
// It takes the bytes the driver sends (commands and display RAM data, like on the wire),
// interprets them like the controller does (command arguments, page/horizontal/vertical
// addressing, column pointer wrap, segment remap applied when RAM is written, COM scan
// direction, inversion, display on/off, entire-display-on, charge pump) and shows the result
// as the panel would: panel(x, y) in module orientation (pin header on top).
//
// Wire it to the driver with fake_ssd1306::bus(). It also records statistics (bytes, writes,
// largest write) and can inject bus errors.
#ifndef FAKE_SSD1306_H
#define FAKE_SSD1306_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "ssd1306.h"

struct fake_ssd1306 {
    // --- panel geometry ---
    int ram_w;        // 128 (SSD1306) or 132 (SH1106)
    int w, h;         // visible panel
    int offset;       // first visible segment
    bool sh1106;

    // --- controller state ---
    uint8_t ram[8][132];
    int page = 0, col = 0;
    int mode = 2;                 // 0 horizontal, 1 vertical, 2 page (reset default)
    int col_start = 0, col_end = 127, page_start = 0, page_end = 7;
    bool seg_remap = false, com_remap = false;
    bool on = false, inverted = false, entire_on = false;
    bool charge_pump = false;     // SSD1306 0x8D 0x14 / SH1106 0xAD 0x8B
    bool scrolling = false;
    int contrast = 0x7F, mux = 63;
    uint8_t com_pins = 0x12;

    // --- statistics / error injection ---
    long writes = 0, cmd_bytes = 0, data_bytes = 0;
    size_t largest_write = 0;
    long fail_after = -1;         // fail the write with this index (0-based), -1 = never
    int fail_code = 2;            // what the failing write returns (Wire: 2 = NACK on address)
    std::vector<uint8_t> pending; // command bytes waiting for arguments
    std::string log;              // human-readable command log (for debugging)

    fake_ssd1306(int panel_w = 128, int panel_h = 64, bool is_sh1106 = false)
        : ram_w(is_sh1106 ? 132 : 128), w(panel_w), h(panel_h), sh1106(is_sh1106)
    {
        // column address of the panel's first pixel with segment remap on (0xA1): centred on
        // most modules, but the 96x16 glass sits at one end of the 128 segments
        offset = (w == 96 && h == 16) ? 0 : (ram_w - w) / 2;
        // power-up RAM is random: fill with a pattern so "init clears it" is tested
        for (int p = 0; p < 8; p++)
            for (int c = 0; c < 132; c++)
                ram[p][c] = (uint8_t)(0xA5 ^ (p * 31 + c * 7));
    }

    ssd1306_bus bus(uint16_t max_chunk = 0)
    {
        ssd1306_bus b;
        b.write = &fake_ssd1306::write_cb;
        b.delay_ms = nullptr;
        b.ctx = this;
        b.max_chunk = max_chunk;
        return b;
    }

    static int write_cb(void *ctx, uint8_t data, const uint8_t *buf, size_t len)
    {
        return static_cast<fake_ssd1306 *>(ctx)->write(data, buf, len);
    }

    int write(uint8_t data, const uint8_t *buf, size_t len)
    {
        if (fail_after >= 0 && writes == fail_after) {
            writes++;
            return fail_code;
        }
        writes++;
        if (len > largest_write)
            largest_write = len;
        for (size_t i = 0; i < len; i++) {
            if (data) {
                data_bytes++;
                ram_write(buf[i]);
            } else {
                cmd_bytes++;
                command_byte(buf[i]);
            }
        }
        return 0;
    }

    // --- what the panel shows ---
    // segment that drives panel column x
    int seg_of(int x) const { return ram_w - 1 - offset - x; }
    // RAM row shown in panel row y
    int row_of(int y) const { return com_remap ? y : h - 1 - y; }

    bool ram_bit(int seg, int row) const { return (ram[row / 8][seg] >> (row % 8)) & 1; }

    bool panel(int x, int y) const
    {
        if (!on)
            return false;
        if (entire_on)
            return true;
        bool b = ram_bit(seg_of(x), row_of(y));
        return inverted ? !b : b;
    }

    int lit_pixels() const
    {
        int n = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                n += panel(x, y);
        return n;
    }

    void reset_stats() { writes = cmd_bytes = data_bytes = 0; largest_write = 0; log.clear(); }

  private:
    // number of argument bytes that follow a command
    int args_of(uint8_t c) const
    {
        switch (c) {
        case 0x81: case 0x8D: case 0x20: case 0xA8: case 0xD3: case 0xD5: case 0xD9:
        case 0xDA: case 0xDB: case 0xAD:
            return 1;
        case 0x21: case 0x22: case 0xA3:
            return 2;
        case 0x26: case 0x27:
            return 6;
        case 0x29: case 0x2A:
            return 5;
        default:
            return 0;
        }
    }

    void command_byte(uint8_t b)
    {
        pending.push_back(b);
        if ((int)pending.size() - 1 < args_of(pending[0]))
            return;
        execute(pending);
        pending.clear();
    }

    void execute(const std::vector<uint8_t> &c)
    {
        char tmp[48];
        snprintf(tmp, sizeof tmp, "%02X", c[0]);
        log += tmp;
        for (size_t i = 1; i < c.size(); i++) {
            snprintf(tmp, sizeof tmp, ".%02X", c[i]);
            log += tmp;
        }
        log += ' ';
        uint8_t op = c[0];
        if (op <= 0x0F) { col = (col & 0xF0) | op; return; }
        if (op >= 0x10 && op <= 0x1F) { col = (col & 0x0F) | ((op & 0x0F) << 4); return; }
        if (op >= 0x40 && op <= 0x7F) return; // start line (always 0 here)
        if (op >= 0xB0 && op <= 0xB7) { page = op & 7; return; }
        switch (op) {
        case 0x81: contrast = c[1]; break;
        case 0x8D: if (!sh1106) charge_pump = (c[1] & 0x04) != 0; break;
        case 0xAD: if (sh1106) charge_pump = (c[1] & 0x01) != 0; break;
        case 0x20: if (!sh1106) mode = c[1] & 3; break;
        case 0x21: col_start = c[1] & 127; col_end = c[2] & 127; col = col_start; break;
        case 0x22: page_start = c[1] & 7; page_end = c[2] & 7; page = page_start; break;
        case 0xA0: seg_remap = false; break;
        case 0xA1: seg_remap = true; break;
        case 0xC0: com_remap = false; break;
        case 0xC8: com_remap = true; break;
        case 0xA4: entire_on = false; break;
        case 0xA5: entire_on = true; break;
        case 0xA6: inverted = false; break;
        case 0xA7: inverted = true; break;
        case 0xAE: on = false; break;
        case 0xAF: on = true; break;
        case 0xA8: mux = c[1] & 63; break;
        case 0xDA: com_pins = c[1]; break;
        case 0x2E: scrolling = false; break;
        case 0x2F: scrolling = true; break;
        default: break; // timing / voltage settings: accepted, no visible effect
        }
    }

    void ram_write(uint8_t v)
    {
        int seg = seg_remap ? ram_w - 1 - col : col;
        if (col < ram_w && page < 8)
            ram[page][seg] = v;
        if (sh1106 || mode == 2) {          // page mode: column advances, stays in the page
            if (col < ram_w - 1)
                col++;
            else if (!sh1106)
                col = 0;
            return;
        }
        if (mode == 0) {                    // horizontal
            if (++col > col_end) {
                col = col_start;
                if (++page > page_end)
                    page = page_start;
            }
        } else {                            // vertical
            if (++page > page_end) {
                page = page_start;
                if (++col > col_end)
                    col = col_start;
            }
        }
    }
};

#endif // FAKE_SSD1306_H
