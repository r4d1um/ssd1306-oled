// SPDX-License-Identifier: MIT
// Dashboard: big value, unit, min/max and a bar - the kind of screen a sensor node shows.
// The numbers are simulated; put your sensor reading into value.
#include <Ssd1306Oled.h>

Ssd1306Oled oled;

void setup()
{
    oled.begin();
    oled.setContrast(0x40); // less bright, longer OLED life
}

void loop()
{
    static float t = 0;
    float value = 21.0f + 3.0f * sinf(t);
    t += 0.1f;
    char buf[12];

    oled.clear();
    oled.setTextSize(1);
    oled.printAt(0, 0, "Room");
    oled.printAt(127, 0, "°C", SSD1306_RIGHT);
    oled.drawHLine(0, 10, 128);

    oled.setTextSize(3);
    dtostrf(value, 4, 1, buf);
    oled.printAt(64, 18, buf, SSD1306_CENTER);

    oled.setTextSize(1);
    oled.drawBar(0, 50, 128, 12, (uint32_t)((value - 15.0f) * 10), 150); // 15..30 °C
    oled.display();
    delay(200);
}
