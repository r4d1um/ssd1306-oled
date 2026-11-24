// SPDX-License-Identifier: MIT
// Ssd1306Oled - Arduino adapter for the ssd1306 core: SSD1306 / SH1106 OLEDs over I2C (Wire)
// or 4-wire SPI, with a Print interface (UTF-8: "23.5 °C" works).
//
//   #include <Ssd1306Oled.h>
//   Ssd1306Oled oled;                     // 128x64 SSD1306
//   void setup() {
//     oled.begin();                       // Wire, address 0x3C, 400 kHz
//     oled.println("Hello");
//     oled.display();                     // send what changed
//   }
//
// The driver core (ssd1306*.c) is precompiled per microcontroller; this adapter is the only
// part compiled with the Arduino core you use.
#ifndef SSD1306_OLED_H
#define SSD1306_OLED_H

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "ssd1306.h"
#include "ssd1306_gfx.h"
#include "ssd1306_text.h"

class Ssd1306Oled : public Print {
  public:
    // width x height of the panel; SSD1306_CTRL_SH1106 for most 1.3" modules
    explicit Ssd1306Oled(uint8_t width = 128, uint8_t height = 64,
                         ssd1306_controller controller = SSD1306_CTRL_SSD1306);
    ~Ssd1306Oled();

    // Change panel details before begin() (e.g. external VCC, a different column offset).
    ssd1306_config &config() { return cfg_; }
    // Use your own framebuffer (SSD1306_FB_BYTES(w, h) bytes) instead of a malloc'd one.
    void setBuffer(uint8_t *fb) { fb_ = fb; ownFb_ = false; }
    // Keep a copy of the display RAM (another width*height/8 bytes) so display() sends only
    // bytes that really changed, even after clear() + drawing everything again. On by default,
    // except on AVR (2 KiB RAM). Call before begin().
    void useShadow(bool on) { wantShadow_ = on; }

    // I2C. Calls wire.begin() unless startWire is false (call Wire.begin(sda, scl) yourself
    // for other pins, then pass false). Returns false if nothing answers at the address or the
    // framebuffer can't be allocated.
    bool begin(uint8_t address = SSD1306_I2C_ADDR, TwoWire &wire = Wire, bool startWire = true,
               uint32_t clockHz = 400000);

    // 4-wire SPI: chip select, data/command and reset pin (-1 = not connected).
    bool beginSPI(int8_t cs, int8_t dc, int8_t rst = -1, uint32_t clockHz = 8000000)
    {
        return beginSPI(cs, dc, rst, SPI, clockHz);
    }
    template <class SpiT>
    bool beginSPI(int8_t cs, int8_t dc, int8_t rst, SpiT &spi, uint32_t clockHz = 8000000)
    {
        spiCs_ = cs;
        spiDc_ = dc;
        spiHz_ = clockHz;
        spi_ = &spi;
        spi.begin();
        if (cs >= 0) {
            pinMode(cs, OUTPUT);
            digitalWrite(cs, HIGH);
        }
        pinMode(dc, OUTPUT);
        hardReset(rst);
        ssd1306_bus bus = {&Ssd1306Oled::spiWrite<SpiT>, &Ssd1306Oled::delayCb, this, 0};
        return start(bus);
    }

    // --- display ---
    bool display() { return ssd1306_flush(&d_) == 0; }        // send what changed
    bool displayAll() { return ssd1306_flush_all(&d_) == 0; } // send everything
    size_t pendingBytes() const { return ssd1306_pending_bytes(&d_); }
    void clear() { ssd1306_clear(&d_); }                       // framebuffer black, cursor 0,0
    void fill(uint8_t color) { ssd1306_fill(&d_, color); }
    void on() { ssd1306_display_on(&d_, 1); }
    void off() { ssd1306_display_on(&d_, 0); }
    void setContrast(uint8_t c) { ssd1306_set_contrast(&d_, c); }
    void invert(bool on) { ssd1306_invert(&d_, on); }
    void rotate180(bool on) { ssd1306_rotate180(&d_, on); }
    void scroll(int dir, uint8_t speed = 7) { ssd1306_scroll(&d_, dir, speed); }

    // --- drawing (colors: SSD1306_WHITE, SSD1306_BLACK, SSD1306_INVERSE) ---
    void drawPixel(int16_t x, int16_t y, uint8_t c = SSD1306_WHITE) { ssd1306_pixel(&d_, x, y, c); }
    bool getPixel(int16_t x, int16_t y) const { return ssd1306_get_pixel(&d_, x, y); }
    void drawHLine(int16_t x, int16_t y, int16_t w, uint8_t c = SSD1306_WHITE) { ssd1306_hline(&d_, x, y, w, c); }
    void drawVLine(int16_t x, int16_t y, int16_t h, uint8_t c = SSD1306_WHITE) { ssd1306_vline(&d_, x, y, h, c); }
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c = SSD1306_WHITE) { ssd1306_line(&d_, x0, y0, x1, y1, c); }
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c = SSD1306_WHITE) { ssd1306_rect(&d_, x, y, w, h, c); }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c = SSD1306_WHITE) { ssd1306_fill_rect(&d_, x, y, w, h, c); }
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t c = SSD1306_WHITE) { ssd1306_round_rect(&d_, x, y, w, h, r, c); }
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t c = SSD1306_WHITE) { ssd1306_fill_round_rect(&d_, x, y, w, h, r, c); }
    void drawCircle(int16_t x, int16_t y, int16_t r, uint8_t c = SSD1306_WHITE) { ssd1306_circle(&d_, x, y, r, c); }
    void fillCircle(int16_t x, int16_t y, int16_t r, uint8_t c = SSD1306_WHITE) { ssd1306_fill_circle(&d_, x, y, r, c); }
    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t c = SSD1306_WHITE) { ssd1306_triangle(&d_, x0, y0, x1, y1, x2, y2, c); }
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t c = SSD1306_WHITE) { ssd1306_fill_triangle(&d_, x0, y0, x1, y1, x2, y2, c); }
    // rows top to bottom, MSB = left pixel; bg = SSD1306_TRANSPARENT keeps clear bits untouched
    void drawBitmap(int16_t x, int16_t y, const uint8_t *bits, int16_t w, int16_t h,
                    uint8_t c = SSD1306_WHITE, uint8_t bg = SSD1306_TRANSPARENT) { ssd1306_bitmap(&d_, x, y, bits, w, h, c, bg); }
    void drawBar(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t value, uint32_t max) { ssd1306_bar(&d_, x, y, w, h, value, max); }

    // --- text (built-in 5x7 font, 6x8 cells, UTF-8) ---
    void setCursor(int16_t x, int16_t y) { ssd1306_set_cursor(&d_, x, y); }
    int16_t cursorX() const { return d_.cur_x; }
    int16_t cursorY() const { return d_.cur_y; }
    void setTextSize(uint8_t scale) { ssd1306_set_text_scale(&d_, scale); }
    void setTextColor(uint8_t fg, uint8_t bg = SSD1306_TRANSPARENT) { ssd1306_set_text_color(&d_, fg, bg); }
    void setTextWrap(bool on) { ssd1306_set_text_wrap(&d_, on); }
    size_t printAt(int16_t x, int16_t y, const char *utf8, ssd1306_align align = SSD1306_LEFT) { return ssd1306_print_at(&d_, x, y, align, utf8); }
    uint16_t textWidth(const char *utf8) const { return ssd1306_text_width(utf8, d_.text_scale); }
    using Print::write;
    size_t write(uint8_t b) override
    {
        ssd1306_put_byte(&d_, b);
        return 1;
    }
    size_t write(const uint8_t *buf, size_t n) override
    {
        ssd1306_print_n(&d_, reinterpret_cast<const char *>(buf), n);
        return n;
    }

    // --- info ---
    uint8_t width() const { return d_.cfg.width; }
    uint8_t height() const { return d_.cfg.height; }
    int error() const { return d_.error; }   // first bus error since begin(), 0 = none
    uint8_t *buffer() { return d_.fb; }
    ssd1306 *core() { return &d_; }           // for the C API (ssd1306_*.h)

  private:
    bool start(const ssd1306_bus &bus);
    void hardReset(int8_t rst);
    static int wireWrite(void *ctx, uint8_t data, const uint8_t *buf, size_t len);
    static void delayCb(void *, uint32_t ms) { delay(ms); }
    template <class SpiT>
    static int spiWrite(void *ctx, uint8_t data, const uint8_t *buf, size_t len)
    {
        Ssd1306Oled *self = static_cast<Ssd1306Oled *>(ctx);
        SpiT *spi = static_cast<SpiT *>(self->spi_);
        digitalWrite(self->spiDc_, data ? HIGH : LOW);
        spi->beginTransaction(SPISettings(self->spiHz_, MSBFIRST, SPI_MODE0));
        if (self->spiCs_ >= 0)
            digitalWrite(self->spiCs_, LOW);
        for (size_t i = 0; i < len; i++)
            spi->transfer(buf[i]);
        if (self->spiCs_ >= 0)
            digitalWrite(self->spiCs_, HIGH);
        spi->endTransaction();
        return 0;
    }

    ssd1306 d_;
    ssd1306_config cfg_;
    uint8_t *fb_ = nullptr;
    uint8_t *shadow_ = nullptr;
    bool ownFb_ = true;
#ifdef __AVR__
    bool wantShadow_ = false;
#else
    bool wantShadow_ = true;
#endif
    TwoWire *wire_ = nullptr;
    uint8_t addr_ = SSD1306_I2C_ADDR;
    void *spi_ = nullptr;
    int8_t spiCs_ = -1, spiDc_ = -1;
    uint32_t spiHz_ = 8000000;
};

#endif // SSD1306_OLED_H
