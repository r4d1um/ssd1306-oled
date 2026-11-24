// SPDX-License-Identifier: MIT
// Host tests: the C99 core against the fake controller (tests/fake_ssd1306.h).
// Built with ASan/UBSan by `make test`. Output is meant to be read by humans: one line per
// check group, a coloured summary at the end (NO_COLOR=1 turns colours off).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "fake_ssd1306.h"
#include "ssd1306.h"
#include "ssd1306_font.h"
#include "ssd1306_gfx.h"
#include "ssd1306_text.h"

// ---------------------------------------------------------------- tiny, fancy test runner
static bool color_on()
{
    static int c = -1;
    if (c < 0)
        c = getenv("NO_COLOR") == nullptr;
    return c;
}
static const char *C(const char *code) { return color_on() ? code : ""; }
#define GREEN C("\033[32m")
#define RED C("\033[31m")
#define DIM C("\033[2m")
#define BOLD C("\033[1m")
#define CYAN C("\033[36m")
#define RESET C("\033[0m")

static int g_checks, g_failed_checks, g_tests, g_failed_tests;
static bool g_cur_failed;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        g_checks++;                                                                        \
        if (!(cond)) {                                                                     \
            g_failed_checks++;                                                             \
            g_cur_failed = true;                                                           \
            printf("      %s✗ %s:%d  %s%s\n", RED, __FILE__, __LINE__, #cond, RESET);       \
        }                                                                                  \
    } while (0)
#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        long long va_ = (long long)(a), vb_ = (long long)(b);                              \
        g_checks++;                                                                        \
        if (va_ != vb_) {                                                                  \
            g_failed_checks++;                                                             \
            g_cur_failed = true;                                                           \
            printf("      %s✗ %s:%d  %s == %s  (%lld != %lld)%s\n", RED, __FILE__, __LINE__, \
                   #a, #b, va_, vb_, RESET);                                               \
        }                                                                                  \
    } while (0)

struct test_case {
    const char *group, *name;
    std::function<void()> fn;
};
static std::vector<test_case> &registry()
{
    static std::vector<test_case> r;
    return r;
}
struct reg {
    reg(const char *g, const char *n, std::function<void()> f) { registry().push_back({g, n, f}); }
};
#define TEST(group, name)                                                                  \
    static void test_##group##_##name();                                                   \
    static reg reg_##group##_##name(#group, #name, test_##group##_##name);                 \
    static void test_##group##_##name()

// ---------------------------------------------------------------- helpers
struct rig {
    fake_ssd1306 fake;
    std::vector<uint8_t> fb, shadow;
    ssd1306 d;
    int init_result;
    rig(int w = 128, int h = 64, bool sh1106 = false, uint16_t chunk = 0, int rotate = 0)
        : fake(w, h, sh1106), fb(SSD1306_FB_BYTES(w, h))
    {
        ssd1306_config cfg;
        if (sh1106)
            ssd1306_default_config_sh1106(&cfg, (uint8_t)w, (uint8_t)h);
        else
            ssd1306_default_config(&cfg, (uint8_t)w, (uint8_t)h);
        cfg.rotate180 = (uint8_t)rotate;
        ssd1306_bus b = fake.bus(chunk);
        init_result = ssd1306_init(&d, &b, &cfg, fb.data());
    }
    void use_shadow()
    {
        shadow.resize(fb.size());
        ssd1306_set_shadow(&d, shadow.data());
    }
    // panel shows exactly the framebuffer?
    int mismatches() const
    {
        int n = 0;
        for (int y = 0; y < fake.h; y++)
            for (int x = 0; x < fake.w; x++) {
                int want = ssd1306_get_pixel(&d, (int16_t)x, (int16_t)y);
                int px = d.cfg.rotate180 ? fake.w - 1 - x : x;
                int py = d.cfg.rotate180 ? fake.h - 1 - y : y;
                n += want != (int)fake.panel(px, py);
            }
        return n;
    }
    int fb_pixels() const
    {
        int n = 0;
        for (int y = 0; y < fake.h; y++)
            for (int x = 0; x < fake.w; x++)
                n += ssd1306_get_pixel(&d, (int16_t)x, (int16_t)y);
        return n;
    }
};

static void draw_sample(ssd1306 *d)
{
    ssd1306_rect(d, 0, 0, (int16_t)d->cfg.width, (int16_t)d->cfg.height, SSD1306_WHITE);
    ssd1306_line(d, 0, 0, (int16_t)(d->cfg.width - 1), (int16_t)(d->cfg.height - 1), SSD1306_WHITE);
    ssd1306_fill_circle(d, 20, 12, 7, SSD1306_WHITE);
    ssd1306_set_cursor(d, 3, 3);
    ssd1306_print(d, "Hi °C");
    ssd1306_fill_rect(d, (int16_t)(d->cfg.width - 10), 2, 6, (int16_t)(d->cfg.height - 4), SSD1306_INVERSE);
}

// ---------------------------------------------------------------- controller / init
TEST(init, sequence_and_cleared_ram)
{
    rig r;
    CHECK_EQ(r.init_result, 0);
    CHECK(r.fake.on);
    CHECK(r.fake.charge_pump);
    CHECK_EQ(r.fake.mode, 2);
    CHECK_EQ(r.fake.mux, 63);
    CHECK_EQ(r.fake.com_pins, 0x12);
    CHECK(r.fake.seg_remap && r.fake.com_remap);
    CHECK(!r.fake.inverted);
    CHECK_EQ(r.fake.lit_pixels(), 0); // random power-up RAM was cleared
    CHECK(r.fake.pending.empty());    // no half command left
    CHECK_EQ(ssd1306_pending_bytes(&r.d), 0);
    // the display must be switched on only after the RAM was cleared
    CHECK(r.fake.log.rfind("AF") > r.fake.log.rfind("B7"));
}

TEST(init, rejects_bad_config)
{
    fake_ssd1306 f;
    uint8_t fb[SSD1306_FB_BYTES(128, 64)];
    ssd1306 d;
    ssd1306_config cfg;
    ssd1306_bus b = f.bus();
    ssd1306_default_config(&cfg, 128, 60);
    CHECK_EQ(ssd1306_init(&d, &b, &cfg, fb), -1); // height not a multiple of 8
    ssd1306_default_config(&cfg, 128, 64);
    cfg.col_offset = 4;
    CHECK_EQ(ssd1306_init(&d, &b, &cfg, fb), -1); // 132 columns > SSD1306 RAM
    CHECK_EQ(ssd1306_init(&d, &b, &cfg, nullptr), -1);
    CHECK_EQ(f.writes, 0);
}

TEST(init, sizes_and_controllers)
{
    struct { int w, h; bool sh; int pins; } sizes[] = {
        {128, 64, false, 0x12}, {128, 32, false, 0x02}, {96, 16, false, 0x02},
        {64, 48, false, 0x12}, {72, 40, false, 0x12}, {64, 32, false, 0x12}, {128, 64, true, 0x12},
    };
    for (int rot = 0; rot < 2; rot++)
    for (auto &s : sizes) {
        rig r(s.w, s.h, s.sh, 0, rot);
        CHECK_EQ(r.init_result, 0);
        if (rot)
            r.use_shadow();
        CHECK_EQ(r.fake.mux, s.h - 1);
        CHECK_EQ(r.fake.com_pins, s.pins);
        CHECK(r.fake.charge_pump);
        draw_sample(&r.d);
        CHECK_EQ(ssd1306_flush(&r.d), 0);
        CHECK_EQ(r.mismatches(), 0);
        CHECK(r.fb_pixels() > 50);
    }
}

TEST(init, sh1106_hidden_columns_cleared)
{
    rig r(128, 64, true);
    int dirty = 0;
    for (int p = 0; p < 8; p++)
        for (int c = 0; c < 132; c++)
            dirty += r.fake.ram[p][c] != 0;
    CHECK_EQ(dirty, 0);
    CHECK_EQ(r.d.cfg.col_offset, 2);
}

// ---------------------------------------------------------------- flushing
TEST(flush, only_changed_bytes)
{
    rig r;
    ssd1306_flush(&r.d);
    r.fake.reset_stats();
    ssd1306_pixel(&r.d, 100, 50, SSD1306_WHITE);
    CHECK_EQ(ssd1306_pending_bytes(&r.d), 1);
    CHECK_EQ(ssd1306_flush(&r.d), 0);
    CHECK_EQ(r.fake.data_bytes, 1);
    CHECK_EQ(r.fake.cmd_bytes, 3); // page + 2 column nibbles
    CHECK_EQ(r.mismatches(), 0);

    r.fake.reset_stats();
    CHECK_EQ(ssd1306_flush(&r.d), 0); // nothing changed
    CHECK_EQ(r.fake.writes, 0);

    // a clock: "12:34" at scale 2, then only the last digit changes
    ssd1306_set_text_scale(&r.d, 2);
    ssd1306_set_text_color(&r.d, SSD1306_WHITE, SSD1306_BLACK);
    ssd1306_print_at(&r.d, 10, 16, SSD1306_LEFT, "12:34");
    ssd1306_flush(&r.d);
    r.fake.reset_stats();
    ssd1306_print_at(&r.d, 10, 16, SSD1306_LEFT, "12:35");
    ssd1306_flush(&r.d);
    CHECK(r.fake.data_bytes <= 2 * 12 * 2); // two pages of the 4th... 5th cell only
    CHECK_EQ(r.mismatches(), 0);
}

TEST(flush, redraw_same_frame_sends_nothing)
{
    rig r;
    auto frame = [&](const char *v) {
        ssd1306_clear(&r.d);
        draw_sample(&r.d);
        ssd1306_set_text_scale(&r.d, 3);
        ssd1306_print_at(&r.d, 64, 30, SSD1306_CENTER, v);
        ssd1306_set_text_scale(&r.d, 1);
    };
    frame("21.4");
    ssd1306_flush(&r.d);
    frame("21.4");
    CHECK_EQ(ssd1306_pending_bytes(&r.d), 1024); // without shadow: everything clear() touched
    ssd1306_flush(&r.d);

    r.use_shadow();
    frame("21.4");
    ssd1306_flush(&r.d);
    r.fake.reset_stats();
    frame("21.4"); // the usual loop: clear, draw everything, display
    CHECK_EQ(ssd1306_pending_bytes(&r.d), 0);
    ssd1306_flush(&r.d);
    CHECK_EQ(r.fake.writes, 0);
    frame("21.5");
    ssd1306_flush(&r.d);
    CHECK(r.fake.data_bytes > 0 && r.fake.data_bytes <= 3 * 18); // one digit, 3 pages
    CHECK_EQ(r.mismatches(), 0);
}

TEST(flush, full_frame_size)
{
    rig r;
    r.fake.reset_stats();
    CHECK_EQ(ssd1306_flush_all(&r.d), 0);
    CHECK_EQ(r.fake.data_bytes, 1024);
    CHECK_EQ(r.d.bytes_sent, 1024 + 8 * 128); // init cleared 8 pages too
}

TEST(flush, chunking)
{
    for (uint16_t chunk : {1, 16, 31, 127}) {
        rig r(128, 64, false, chunk);
        CHECK_EQ(r.init_result, 0);
        draw_sample(&r.d);
        ssd1306_flush(&r.d);
        CHECK(r.fake.largest_write <= chunk);
        CHECK_EQ(r.mismatches(), 0);
    }
}

TEST(flush, bus_errors)
{
    for (long at : {0L, 3L, 20L}) {
        fake_ssd1306 f;
        f.fail_after = at;
        uint8_t fb[SSD1306_FB_BYTES(128, 64)];
        ssd1306 d;
        ssd1306_config cfg;
        ssd1306_default_config(&cfg, 128, 64);
        ssd1306_bus b = f.bus();
        CHECK_EQ(ssd1306_init(&d, &b, &cfg, fb), 2);
        CHECK_EQ(d.error, 2);
    }
    rig r;
    r.fake.fail_after = r.fake.writes; // next write fails
    ssd1306_pixel(&r.d, 1, 1, SSD1306_WHITE);
    CHECK_EQ(ssd1306_flush(&r.d), 2);
    CHECK_EQ(ssd1306_pending_bytes(&r.d), 1); // stays dirty, can be retried
}

// ---------------------------------------------------------------- display settings
TEST(settings, contrast_invert_power_rotate_scroll)
{
    rig r;
    ssd1306_set_contrast(&r.d, 0x10);
    CHECK_EQ(r.fake.contrast, 0x10);
    ssd1306_invert(&r.d, 1);
    CHECK(r.fake.inverted);
    CHECK_EQ(r.fake.lit_pixels(), 128 * 64);
    ssd1306_invert(&r.d, 0);
    ssd1306_display_on(&r.d, 0);
    CHECK(!r.fake.on);
    ssd1306_display_on(&r.d, 1);
    CHECK(r.fake.on);

    draw_sample(&r.d);
    ssd1306_flush(&r.d);
    ssd1306_rotate180(&r.d, 1);
    CHECK(!r.fake.seg_remap && !r.fake.com_remap);
    CHECK_EQ(r.mismatches(), 0); // mismatches() compares rotated
    CHECK_EQ(r.fake.panel(127 - 20, 63 - 12), 1); // circle centre is now bottom right
    ssd1306_rotate180(&r.d, 0);
    CHECK_EQ(r.mismatches(), 0);

    ssd1306_scroll(&r.d, 1, 7);
    CHECK(r.fake.scrolling);
    ssd1306_scroll(&r.d, 0, 0);
    CHECK(!r.fake.scrolling);

    rig s(128, 64, true);
    s.fake.reset_stats();
    ssd1306_scroll(&s.d, 1, 0); // SH1106 has no scrolling: nothing is sent
    CHECK_EQ(s.fake.writes, 0);
}

TEST(settings, rotated_init)
{
    rig r(128, 32, false, 0, 1);
    draw_sample(&r.d);
    ssd1306_flush(&r.d);
    CHECK_EQ(r.mismatches(), 0);
}

// ---------------------------------------------------------------- drawing
TEST(gfx, clipping_never_writes_outside)
{
    rig r(64, 48);
    // everything partly or fully off-screen; ASan watches the framebuffer bounds
    ssd1306_line(&r.d, -100, -50, 300, 200, SSD1306_WHITE);
    ssd1306_fill_rect(&r.d, -10, -10, 30, 30, SSD1306_WHITE);
    ssd1306_fill_rect(&r.d, 60, 40, 100, 100, SSD1306_WHITE);
    ssd1306_circle(&r.d, -5, 24, 20, SSD1306_WHITE);
    ssd1306_fill_circle(&r.d, 70, 50, 15, SSD1306_WHITE);
    ssd1306_fill_triangle(&r.d, -40, -40, 100, 10, 20, 90, SSD1306_INVERSE);
    ssd1306_hline(&r.d, -1000, 5, 3000, SSD1306_WHITE);
    ssd1306_vline(&r.d, 5, 47, -100, SSD1306_WHITE);
    ssd1306_set_text_scale(&r.d, 3);
    ssd1306_print_at(&r.d, 40, 30, SSD1306_LEFT, "Overflow!");
    ssd1306_flush(&r.d);
    CHECK_EQ(r.mismatches(), 0);
    CHECK_EQ(ssd1306_get_pixel(&r.d, -1, 0), 0);
    CHECK_EQ(ssd1306_get_pixel(&r.d, 64, 0), 0);
}

TEST(gfx, lines_and_rects)
{
    rig r;
    ssd1306_hline(&r.d, 10, 5, 20, SSD1306_WHITE);
    CHECK_EQ(r.fb_pixels(), 20);
    ssd1306_hline(&r.d, 29, 6, -20, SSD1306_WHITE); // negative width goes left
    CHECK_EQ(r.fb_pixels(), 40);
    ssd1306_clear(&r.d);
    ssd1306_rect(&r.d, 0, 0, 10, 5, SSD1306_WHITE);
    CHECK_EQ(r.fb_pixels(), 2 * 10 + 2 * 3);
    ssd1306_clear(&r.d);
    ssd1306_fill_rect(&r.d, 3, 5, 7, 13, SSD1306_WHITE); // spans 3 pages
    CHECK_EQ(r.fb_pixels(), 7 * 13);
    CHECK_EQ(ssd1306_get_pixel(&r.d, 3, 5), 1);
    CHECK_EQ(ssd1306_get_pixel(&r.d, 9, 17), 1);
    CHECK_EQ(ssd1306_get_pixel(&r.d, 10, 17), 0);
    CHECK_EQ(ssd1306_get_pixel(&r.d, 9, 18), 0);
    ssd1306_clear(&r.d);
    ssd1306_line(&r.d, 0, 0, 7, 3, SSD1306_WHITE);
    CHECK_EQ(r.fb_pixels(), 8); // one pixel per column on a flat line
    CHECK_EQ(ssd1306_get_pixel(&r.d, 7, 3), 1);
}

// INVERSE twice must give back the empty screen: every shape draws each pixel exactly once
TEST(gfx, inverse_draws_each_pixel_once)
{
    rig r;
    struct shape { const char *name; std::function<void(ssd1306 *)> draw; };
    std::vector<shape> shapes = {
        {"circle", [](ssd1306 *d) { ssd1306_circle(d, 64, 32, 20, SSD1306_INVERSE); }},
        {"fill_circle", [](ssd1306 *d) { ssd1306_fill_circle(d, 64, 32, 17, SSD1306_INVERSE); }},
        {"round_rect", [](ssd1306 *d) { ssd1306_round_rect(d, 10, 10, 60, 30, 8, SSD1306_INVERSE); }},
        {"fill_round_rect", [](ssd1306 *d) { ssd1306_fill_round_rect(d, 10, 10, 61, 31, 9, SSD1306_INVERSE); }},
        {"fill_triangle", [](ssd1306 *d) { ssd1306_fill_triangle(d, 5, 60, 64, 2, 120, 45, SSD1306_INVERSE); }},
        {"rect", [](ssd1306 *d) { ssd1306_rect(d, 1, 1, 2, 2, SSD1306_INVERSE); }},
        {"text", [](ssd1306 *d) { ssd1306_set_text_color(d, SSD1306_INVERSE, SSD1306_TRANSPARENT);
                                  ssd1306_set_text_scale(d, 2);
                                  ssd1306_print_at(d, 3, 3, SSD1306_LEFT, "Wäg 8€"); }},
    };
    for (auto &s : shapes) {
        ssd1306_clear(&r.d);
        s.draw(&r.d);
        int once = r.fb_pixels();
        s.draw(&r.d);
        if (r.fb_pixels() != 0)
            printf("      %s%s left pixels%s\n", RED, s.name, RESET);
        CHECK(once > 0);
        CHECK_EQ(r.fb_pixels(), 0);
    }
}

TEST(gfx, circle_symmetry)
{
    rig r;
    ssd1306_circle(&r.d, 40, 30, 13, SSD1306_WHITE);
    ssd1306_fill_circle(&r.d, 100, 30, 9, SSD1306_WHITE);
    int asym = 0;
    for (int dy = -14; dy <= 14; dy++)
        for (int dx = -14; dx <= 14; dx++) {
            int a = ssd1306_get_pixel(&r.d, (int16_t)(40 + dx), (int16_t)(30 + dy));
            asym += a != ssd1306_get_pixel(&r.d, (int16_t)(40 - dx), (int16_t)(30 + dy));
            asym += a != ssd1306_get_pixel(&r.d, (int16_t)(40 + dy), (int16_t)(30 + dx));
            int b = ssd1306_get_pixel(&r.d, (int16_t)(100 + dx), (int16_t)(30 + dy));
            asym += b != ssd1306_get_pixel(&r.d, (int16_t)(100 - dx), (int16_t)(30 - dy));
        }
    CHECK_EQ(asym, 0);
    CHECK_EQ(ssd1306_get_pixel(&r.d, 40, 17), 1);  // top
    CHECK_EQ(ssd1306_get_pixel(&r.d, 40, 30), 0);  // hollow
    CHECK_EQ(ssd1306_get_pixel(&r.d, 100, 30), 1); // filled
}

TEST(gfx, bitmap_and_bar)
{
    rig r;
    static const uint8_t arrow[] = {0x18, 0x3C, 0x7E, 0xFF, 0x18, 0x18, 0x18, 0x18}; // 8x8
    ssd1306_bitmap(&r.d, 0, 0, arrow, 8, 8, SSD1306_WHITE, SSD1306_TRANSPARENT);
    CHECK_EQ(ssd1306_get_pixel(&r.d, 3, 0), 1);
    CHECK_EQ(ssd1306_get_pixel(&r.d, 2, 0), 0);
    CHECK_EQ(ssd1306_get_pixel(&r.d, 0, 3), 1);
    CHECK_EQ(r.fb_pixels(), 2 + 4 + 6 + 8 + 2 * 4);
    ssd1306_clear(&r.d);
    ssd1306_bar(&r.d, 0, 0, 104, 10, 50, 100); // inner width 100 -> 50 columns
    int filled = 0;
    for (int x = 2; x < 102; x++)
        filled += ssd1306_get_pixel(&r.d, (int16_t)x, 5);
    CHECK_EQ(filled, 50);
    ssd1306_bar(&r.d, 0, 0, 104, 10, 500, 100); // clamped
    filled = 0;
    for (int x = 2; x < 102; x++)
        filled += ssd1306_get_pixel(&r.d, (int16_t)x, 5);
    CHECK_EQ(filled, 100);
}

// ---------------------------------------------------------------- text
static int glyph_pixels(uint32_t cp)
{
    uint8_t g[5];
    if (!ssd1306_font5x7_glyph(cp, g))
        return -1;
    int n = 0;
    for (int i = 0; i < 5; i++)
        n += __builtin_popcount(g[i]);
    return n;
}

TEST(text, font_complete)
{
    CHECK_EQ(glyph_pixels(' '), 0);
    for (uint32_t c = 0x21; c <= 0x7E; c++)
        CHECK(glyph_pixels(c) > 0);
    const uint32_t extra[] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB5, 0xB7, 0xC4, 0xD6, 0xDC, 0xDF,
                              0xE4, 0xF6, 0xFC, 0x20AC, 0x2190, 0x2191, 0x2192, 0x2193, 0x2588};
    for (uint32_t c : extra)
        CHECK(glyph_pixels(c) > 0);
    CHECK_EQ(glyph_pixels(0x263A), -1);
    CHECK_EQ(glyph_pixels(0x1F600), -1);
    // no two printable ASCII glyphs look the same
    int dup = 0;
    for (uint32_t a = 0x21; a <= 0x7E; a++)
        for (uint32_t b = a + 1; b <= 0x7E; b++) {
            uint8_t ga[5], gb[5];
            ssd1306_font5x7_glyph(a, ga);
            ssd1306_font5x7_glyph(b, gb);
            dup += memcmp(ga, gb, 5) == 0;
        }
    CHECK_EQ(dup, 0);
}

// panel cells as a string of glyph fingerprints, to compare text output
static std::string cells(const ssd1306 *d, int y, int n)
{
    std::string s;
    for (int i = 0; i < n; i++)
        for (int c = 0; c < 6; c++) {
            int v = 0;
            for (int r = 0; r < 8; r++)
                v |= ssd1306_get_pixel(d, (int16_t)(i * 6 + c), (int16_t)(y + r)) << r;
            s += (char)('A' + (v & 15));
            s += (char)('A' + (v >> 4));
        }
    return s;
}

TEST(text, utf8_and_fallback)
{
    rig a, b;
    ssd1306_print(&a.d, "23.5\xC2\xB0" "C \xE2\x82\xAC \xF0\x9F\x98\x80"); // "23.5°C € 😀"
    // byte by byte, like Arduino's Print does
    const char *s = "23.5°C € 😀";
    for (const char *p = s; *p; p++)
        ssd1306_put_byte(&b.d, (uint8_t)*p);
    CHECK(cells(&a.d, 0, 10) == cells(&b.d, 0, 10));
    CHECK_EQ(a.d.cur_x, 10 * 6);

    rig q, ref;
    ssd1306_print(&q.d, "\xC3(x\xE2\x82"); // broken sequence, then a cut-off one
    ssd1306_print(&ref.d, "?(x");
    CHECK(cells(&q.d, 0, 3) == cells(&ref.d, 0, 3));

    rig u, uref;
    ssd1306_print(&u.d, "\xE2\x98\xBA"); // ☺ is not in the font
    ssd1306_print(&uref.d, "?");
    CHECK(cells(&u.d, 0, 1) == cells(&uref.d, 0, 1));

    CHECK_EQ(ssd1306_text_length("Grüße"), 5);
    CHECK_EQ(ssd1306_text_width("Grüße", 1), 29);
    CHECK_EQ(ssd1306_text_width("Grüße", 2), 58);
    CHECK_EQ(ssd1306_text_width("ab\ncdef", 1), 11);
    CHECK_EQ(ssd1306_text_width("", 3), 0);
}

TEST(text, wrap_newline_align_background)
{
    rig r;
    ssd1306_print(&r.d, "0123456789ABCDEFGHIJKL"); // 22 chars, 21 fit
    CHECK_EQ(r.d.cur_y, 8);
    CHECK_EQ(r.d.cur_x, 6);
    ssd1306_print(&r.d, "\nX");
    CHECK_EQ(r.d.cur_y, 16);
    CHECK_EQ(r.d.cur_x, 6);

    ssd1306_set_text_wrap(&r.d, 0);
    ssd1306_set_cursor(&r.d, 120, 24);
    ssd1306_print(&r.d, "no wrap");
    CHECK_EQ(r.d.cur_y, 24);

    rig c;
    ssd1306_print_at(&c.d, 64, 0, SSD1306_CENTER, "MID"); // 17 px wide -> starts at 56
    int left = 128;
    for (int x = 0; x < 128; x++)
        for (int y = 0; y < 8; y++)
            if (ssd1306_get_pixel(&c.d, (int16_t)x, (int16_t)y) && x < left)
                left = x;
    CHECK_EQ(left, 56);
    rig rr;
    ssd1306_print_at(&rr.d, 127, 0, SSD1306_RIGHT, "I"); // 'I' columns 1..3 of its cell
    CHECK_EQ(ssd1306_get_pixel(&rr.d, 125, 0), 1);
    CHECK_EQ(rr.d.cur_x, 128);

    rig bg;
    ssd1306_fill(&bg.d, SSD1306_WHITE);
    ssd1306_set_text_color(&bg.d, SSD1306_WHITE, SSD1306_BLACK);
    ssd1306_set_text_scale(&bg.d, 2);
    ssd1306_print_at(&bg.d, 0, 0, SSD1306_LEFT, " ");
    int black = 0;
    for (int x = 0; x < 12; x++)
        for (int y = 0; y < 16; y++)
            black += !ssd1306_get_pixel(&bg.d, (int16_t)x, (int16_t)y);
    CHECK_EQ(black, 12 * 16);
    CHECK_EQ(ssd1306_get_pixel(&bg.d, 12, 0), 1);
}

// ---------------------------------------------------------------- main
int main()
{
    auto t0 = std::chrono::steady_clock::now();
    printf("\n%s%s  ssd1306-oled %s - host tests against the fake controller%s\n\n", BOLD, CYAN,
           SSD1306_VERSION, RESET);
    const char *group = "";
    for (auto &t : registry()) {
        if (strcmp(group, t.group) != 0) {
            group = t.group;
            printf("  %s%s%s\n", BOLD, group, RESET);
        }
        g_cur_failed = false;
        int before = g_checks;
        auto s = std::chrono::steady_clock::now();
        t.fn();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count();
        g_tests++;
        if (g_cur_failed)
            g_failed_tests++;
        printf("    %s%s%s %-40s %s%3d checks %6.1f ms%s\n", g_cur_failed ? RED : GREEN,
               g_cur_failed ? "✗" : "✓", RESET, t.name, DIM, g_checks - before, ms, RESET);
    }
    double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const char *col = g_failed_tests ? RED : GREEN;
    printf("\n  %s%s┌──────────────────────────────────────────────┐%s\n", BOLD, col, RESET);
    printf("  %s%s│ %-3s %3d/%-3d tests  %5d checks  %7.1f ms │%s\n", BOLD, col,
           g_failed_tests ? "✗" : "✓", g_tests - g_failed_tests, g_tests, g_checks, total, RESET);
    printf("  %s%s└──────────────────────────────────────────────┘%s\n\n", BOLD, col, RESET);
    return g_failed_tests ? 1 : 0;
}
