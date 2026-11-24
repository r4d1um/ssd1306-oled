// SPDX-License-Identifier: MIT
// SpiOled: 7-pin SPI module (GND VCC D0=SCK D1=MOSI RES DC CS) on the hardware SPI pins.
#include <Ssd1306Oled.h>

const int8_t PIN_CS = 5, PIN_DC = 4, PIN_RST = 2; // change to your wiring

Ssd1306Oled oled;

void setup()
{
    oled.beginSPI(PIN_CS, PIN_DC, PIN_RST);
    oled.fillCircle(64, 32, 20);
    oled.setTextColor(SSD1306_INVERSE);
    oled.setTextSize(2);
    oled.printAt(64, 25, "SPI", SSD1306_CENTER);
    oled.display();
}

void loop()
{
    oled.invert(true);
    delay(500);
    oled.invert(false);
    delay(500);
}
