// SPDX-License-Identifier: MIT
// Sample screens drawn through the driver core and shown by the fake controller
// (tests/fake_ssd1306.h), saved as PNG: one file per screen plus overview.png.
// All values are made up. Usage: screens-tool <output dir>   (make screens)
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../../tests/fake_ssd1306.h"
#include "png.h"
#include "ssd1306.h"
#include "ssd1306_font.h"
#include "ssd1306_gfx.h"
#include "ssd1306_text.h"

static const uint32_t LIT = 0xDDEEFF, DARK = 0x0A0D12, GLASS = 0x05070A, BOARD = 0x1B2330;
static const uint32_t BG = 0x2A2F38, CAPTION = 0xC8CED8;
static const int SCALE = 3;

struct screen {
    std::string file, caption;
    int w, h;
    bool sh1106;
    std::function<void(ssd1306 *)> draw;
};

// draw a panel (scaled pixels with a small gap, glass and board around it) into img at (ox, oy)
static void put_panel(rgb_image &img, int ox, int oy, const fake_ssd1306 &f)
{
    int pw = f.w * SCALE, ph = f.h * SCALE;
    img.rect(ox, oy, pw + 24, ph + 24, BOARD);
    img.rect(ox + 8, oy + 8, pw + 8, ph + 8, GLASS);
    for (int y = 0; y < f.h; y++)
        for (int x = 0; x < f.w; x++)
            img.rect(ox + 12 + x * SCALE, oy + 12 + y * SCALE, SCALE - 1, SCALE - 1, f.panel(x, y) ? LIT : DARK);
}

// caption text drawn with the driver's own font glyphs (2x), any length
static void put_text(rgb_image &img, int ox, int oy, const std::string &text)
{
    size_t i = 0;
    while (i < text.size()) {
        uint32_t cp = (uint8_t)text[i++];
        int more = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
        if (more)
            cp &= 0x3Fu >> more;
        while (more-- && i < text.size())
            cp = (cp << 6) | ((uint8_t)text[i++] & 0x3F);
        uint8_t cols[5];
        if (!ssd1306_font5x7_glyph(cp, cols))
            ssd1306_font5x7_glyph('?', cols);
        for (int c = 0; c < 5; c++)
            for (int r = 0; r < 8; r++)
                if ((cols[c] >> r) & 1)
                    img.rect(ox + c * 2, oy + r * 2, 2, 2, CAPTION);
        ox += 12;
    }
}

static fake_ssd1306 render(const screen &s)
{
    fake_ssd1306 f(s.w, s.h, s.sh1106);
    std::vector<uint8_t> fb(SSD1306_FB_BYTES(s.w, s.h));
    ssd1306 d;
    ssd1306_config cfg;
    if (s.sh1106)
        ssd1306_default_config_sh1106(&cfg, (uint8_t)s.w, (uint8_t)s.h);
    else
        ssd1306_default_config(&cfg, (uint8_t)s.w, (uint8_t)s.h);
    ssd1306_bus b = f.bus(31); // like an AVR's 32-byte Wire buffer
    ssd1306_init(&d, &b, &cfg, fb.data());
    s.draw(&d);
    ssd1306_flush(&d);
    return f;
}

int main(int argc, char **argv)
{
    std::string out = argc > 1 ? argv[1] : ".";
    std::vector<screen> screens = {
        {"air", "0.96\" 128x64: sensor node", 128, 64, false, [](ssd1306 *d) {
             ssd1306_print_at(d, 0, 0, SSD1306_LEFT, "Air monitor");
             ssd1306_fill_circle(d, 123, 3, 3, SSD1306_WHITE);
             ssd1306_hline(d, 0, 10, 128, SSD1306_WHITE);
             ssd1306_set_text_scale(d, 3);
             ssd1306_print_at(d, 0, 15, SSD1306_LEFT, "23.5");
             ssd1306_set_text_scale(d, 2);
             ssd1306_print_at(d, 127, 15, SSD1306_RIGHT, "°C");
             ssd1306_set_text_scale(d, 1);
             ssd1306_print_at(d, 76, 33, SSD1306_LEFT, "41 %rH");
             ssd1306_print_at(d, 0, 44, SSD1306_LEFT, "1013 hPa");
             ssd1306_print_at(d, 127, 44, SSD1306_RIGHT, "↑ 0.4");
             ssd1306_print_at(d, 0, 56, SSD1306_LEFT, "VOC");
             ssd1306_bar(d, 22, 55, 106, 9, 37, 100);
         }},
        {"timer", "0.96\" 128x64: countdown", 128, 64, false, [](ssd1306 *d) {
             ssd1306_fill_rect(d, 0, 0, 128, 11, SSD1306_WHITE);
             ssd1306_set_text_color(d, SSD1306_BLACK, SSD1306_TRANSPARENT);
             ssd1306_print_at(d, 64, 2, SSD1306_CENTER, "STEP 2 / 4");
             ssd1306_set_text_color(d, SSD1306_WHITE, SSD1306_TRANSPARENT);
             ssd1306_set_text_scale(d, 4);
             ssd1306_print_at(d, 64, 15, SSD1306_CENTER, "07:30");
             ssd1306_set_text_scale(d, 1);
             ssd1306_bar(d, 0, 49, 128, 7, 270, 600);
             ssd1306_print_at(d, 0, 57, SSD1306_LEFT, "next ↓ 0:30");
             ssd1306_print_at(d, 127, 57, SSD1306_RIGHT, "20.0°C");
         }},
        {"shapes", "0.96\" 128x64: drawing", 128, 64, false, [](ssd1306 *d) {
             ssd1306_round_rect(d, 0, 0, 128, 64, 8, SSD1306_WHITE);
             ssd1306_circle(d, 22, 22, 14, SSD1306_WHITE);
             ssd1306_fill_circle(d, 22, 22, 7, SSD1306_WHITE);
             ssd1306_fill_triangle(d, 44, 36, 60, 8, 76, 36, SSD1306_WHITE);
             ssd1306_fill_round_rect(d, 84, 8, 36, 28, 6, SSD1306_WHITE);
             ssd1306_set_text_color(d, SSD1306_INVERSE, SSD1306_TRANSPARENT);
             ssd1306_set_text_scale(d, 2);
             ssd1306_print_at(d, 102, 15, SSD1306_CENTER, "OK");
             ssd1306_set_text_scale(d, 1);
             ssd1306_set_text_color(d, SSD1306_WHITE, SSD1306_TRANSPARENT);
             for (int i = 0; i < 12; i++) {
                 double a = i * 3.14159265 / 6;
                 ssd1306_line(d, 64, 52, (int16_t)(64 + 10 * cos(a)), (int16_t)(52 + 9 * sin(a)), SSD1306_WHITE);
             }
             ssd1306_print_at(d, 8, 48, SSD1306_LEFT, "→ 9.99");
             ssd1306_print_at(d, 120, 48, SSD1306_RIGHT, "x²±y");
         }},
        {"font", "built-in font, UTF-8", 128, 64, false, [](ssd1306 *d) {
             ssd1306_set_cursor(d, 0, 0);
             for (int c = 0x20; c < 0x7F; c++) {
                 char s[2] = {(char)c, 0};
                 ssd1306_print(d, s);
             }
             ssd1306_print(d, "°±²³µ·ÄÖÜßäöü€←↑→↓█");
         }},
        {"wide", "0.91\" 128x32", 128, 32, false, [](ssd1306 *d) {
             ssd1306_print_at(d, 0, 0, SSD1306_LEFT, "Garage");
             ssd1306_print_at(d, 127, 0, SSD1306_RIGHT, "12:04");
             ssd1306_set_text_scale(d, 2);
             ssd1306_print_at(d, 0, 12, SSD1306_LEFT, "→ 12 free");
             ssd1306_set_text_scale(d, 1);
             ssd1306_hline(d, 0, 30, 96, SSD1306_WHITE);
         }},
        {"sh1106", "1.3\" SH1106 128x64", 128, 64, true, [](ssd1306 *d) {
             ssd1306_set_text_scale(d, 3);
             ssd1306_print_at(d, 64, 12, SSD1306_CENTER, "12:34");
             ssd1306_set_text_scale(d, 1);
             ssd1306_print_at(d, 64, 40, SSD1306_CENTER, "Tue 24 Nov 2026");
             ssd1306_print_at(d, 64, 54, SSD1306_CENTER, "↑ alarm 06:45");
         }},
    };

    // one PNG per screen
    for (auto &s : screens) {
        fake_ssd1306 f = render(s);
        rgb_image img(s.w * SCALE + 24, s.h * SCALE + 24, BG);
        put_panel(img, 0, 0, f);
        std::string path = out + "/" + s.file + ".png";
        if (!write_png(path.c_str(), img)) {
            fprintf(stderr, "cannot write %s\n", path.c_str());
            return 1;
        }
        printf("  ▣ %-28s %s\n", s.caption.c_str(), path.c_str());
    }

    // overview: 3 columns x 2 rows with captions
    const int cell_w = 128 * SCALE + 24 + 24, cell_h = 64 * SCALE + 24 + 40;
    rgb_image all(3 * cell_w + 24, 2 * cell_h + 24, BG);
    for (size_t i = 0; i < screens.size(); i++) {
        fake_ssd1306 f = render(screens[i]);
        int ox = 24 + (int)(i % 3) * cell_w, oy = 24 + (int)(i / 3) * cell_h;
        put_panel(all, ox, oy + (64 - screens[i].h) * SCALE / 2, f);
        put_text(all, ox, oy + 64 * SCALE + 30, screens[i].caption);
    }
    std::string path = out + "/overview.png";
    if (!write_png(path.c_str(), all))
        return 1;
    printf("  ▣ %-28s %s\n", "overview", path.c_str());
    return 0;
}
