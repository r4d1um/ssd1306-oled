# Changelog

## 0.1.0

- First release: C99 core for SSD1306 and SH1106 (128x64, 128x32, 96x16, 64x48, 72x40, 64x32),
  page-wise dirty tracking (sends only changed columns), rotation, contrast, inversion,
  hardware scroll.
- Drawing: pixels, lines, rectangles, rounded rectangles, circles, triangles (outline and
  filled, each pixel drawn once so INVERSE works), bitmaps, bars.
- Text: own 5x7 font, UTF-8 (° ± ² ³ µ · Ä Ö Ü ß ä ö ü € arrows █), scale 1..8, alignment,
  wrap, background colour.
- Arduino adapter `Ssd1306Oled` (I2C via Wire, 4-wire SPI, Print), precompiled core for
  ESP8266, ESP32, RP2040 (cortex-m0plus), ATmega328P and ATmega2560.
