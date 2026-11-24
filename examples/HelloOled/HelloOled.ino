// SPDX-License-Identifier: MIT
// HelloOled: 0.96" 128x64 SSD1306 on I2C (GND, VCC 3.3-5 V, SCL, SDA), address 0x3C.
// Default I2C pins: D1 mini SCL=D1 SDA=D2, ESP32 SCL=22 SDA=21, Pico SCL=GP5 SDA=GP4, Uno SCL=A5 SDA=A4.
#include <Ssd1306Oled.h>

Ssd1306Oled oled; // 128x64 SSD1306; for a 1.3" SH1106: Ssd1306Oled oled(128, 64, SSD1306_CTRL_SH1106);

void setup()
{
    Serial.begin(115200);
    if (!oled.begin()) { // try oled.begin(SSD1306_I2C_ADDR_ALT) for 0x3D
        Serial.println("no OLED at 0x3C");
        return;
    }
    oled.setTextSize(2);
    oled.println("Hello!");
    oled.setTextSize(1);
    oled.println("23.5 °C  Grüße  → €");
    oled.drawRoundRect(0, 40, 128, 24, 6);
    oled.display();
}

void loop()
{
    static uint32_t n;
    char buf[16];
    snprintf(buf, sizeof buf, "%lu s", (unsigned long)n++);
    oled.fillRect(4, 44, 120, 16, SSD1306_BLACK);
    oled.setTextSize(2);
    oled.printAt(64, 45, buf, SSD1306_CENTER);
    oled.display(); // sends only the changed columns
    delay(1000);
}
