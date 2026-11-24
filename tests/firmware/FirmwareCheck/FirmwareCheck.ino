// SPDX-License-Identifier: MIT
// CI only: a real firmware against the packaged Arduino library, built for every supported
// board (make firmware-check). Uses every part of the API so the linker pulls in all objects.
#include <Ssd1306Oled.h>

Ssd1306Oled i2c;
Ssd1306Oled spi(128, 32);
Ssd1306Oled big(128, 64, SSD1306_CTRL_SH1106);

static const uint8_t icon[] = {0x18, 0x3C, 0x7E, 0xFF, 0x18, 0x18, 0x18, 0x18};

void setup()
{
    Serial.begin(115200);
    bool ok = i2c.begin();
    ok &= big.begin(SSD1306_I2C_ADDR_ALT, Wire, false);
    ok &= spi.beginSPI(5, 4, 2);
    i2c.setTextSize(2);
    i2c.println("23.5 °C");
    i2c.printAt(127, 40, "Grüße →", SSD1306_RIGHT);
    i2c.drawLine(0, 0, 127, 63);
    i2c.drawRoundRect(1, 1, 60, 30, 5);
    i2c.fillRoundRect(64, 1, 60, 30, 5, SSD1306_INVERSE);
    i2c.drawCircle(30, 40, 10);
    i2c.fillCircle(90, 40, 10);
    i2c.drawTriangle(0, 63, 10, 50, 20, 63);
    i2c.fillTriangle(30, 63, 40, 50, 50, 63);
    i2c.drawBitmap(100, 50, icon, 8, 8);
    i2c.drawBar(0, 56, 64, 8, 42, 100);
    i2c.display();
    i2c.scroll(1);
    i2c.rotate180(true);
    i2c.invert(false);
    i2c.setContrast(10);
    ssd1306_print(i2c.core(), "C API");
    Serial.println(ok ? "ok" : "no display");
    Serial.println(i2c.error());
}

void loop() {}
