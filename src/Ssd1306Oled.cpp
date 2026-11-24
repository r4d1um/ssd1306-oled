// SPDX-License-Identifier: MIT
#include "Ssd1306Oled.h"

#include <stdlib.h>
#include <string.h>

// Bytes one Wire transmission can carry, minus the control byte.
#if defined(I2C_BUFFER_LENGTH)        // ESP32
#define SSD1306_WIRE_CHUNK (I2C_BUFFER_LENGTH - 1)
#elif defined(BUFFER_LENGTH)          // AVR (32), ESP8266 (128)
#define SSD1306_WIRE_CHUNK (BUFFER_LENGTH - 1)
#elif defined(WIRE_BUFFER_SIZE)       // RP2040 (arduino-pico), others
#define SSD1306_WIRE_CHUNK (WIRE_BUFFER_SIZE - 1)
#else
#define SSD1306_WIRE_CHUNK 31
#endif

Ssd1306Oled::Ssd1306Oled(uint8_t width, uint8_t height, ssd1306_controller controller)
{
    memset(&d_, 0, sizeof d_);
    if (controller == SSD1306_CTRL_SH1106)
        ssd1306_default_config_sh1106(&cfg_, width, height);
    else
        ssd1306_default_config(&cfg_, width, height);
    d_.cfg = cfg_;
}

Ssd1306Oled::~Ssd1306Oled()
{
    if (ownFb_)
        free(fb_);
    free(shadow_);
}

int Ssd1306Oled::wireWrite(void *ctx, uint8_t data, const uint8_t *buf, size_t len)
{
    Ssd1306Oled *self = static_cast<Ssd1306Oled *>(ctx);
    self->wire_->beginTransmission(self->addr_);
    self->wire_->write((uint8_t)(data ? 0x40 : 0x00));
    self->wire_->write(buf, len);
    return self->wire_->endTransmission();
}

bool Ssd1306Oled::begin(uint8_t address, TwoWire &wire, bool startWire, uint32_t clockHz)
{
    wire_ = &wire;
    addr_ = address;
    if (startWire)
        wire.begin();
    wire.setClock(clockHz);
    delay(100); // power-up time before the first transfer
    wire.beginTransmission(address);
    if (wire.endTransmission() != 0)
        return false; // nobody at that address
    uint16_t chunk = SSD1306_WIRE_CHUNK;
    ssd1306_bus bus = {&Ssd1306Oled::wireWrite, nullptr, this, chunk};
    return start(bus);
}

void Ssd1306Oled::hardReset(int8_t rst)
{
    if (rst < 0)
        return;
    pinMode(rst, OUTPUT);
    digitalWrite(rst, HIGH);
    delay(1);
    digitalWrite(rst, LOW);
    delay(10);
    digitalWrite(rst, HIGH);
}

bool Ssd1306Oled::start(const ssd1306_bus &bus)
{
    if (!fb_) {
        fb_ = static_cast<uint8_t *>(malloc(SSD1306_FB_BYTES(cfg_.width, cfg_.height)));
        ownFb_ = true;
        if (!fb_)
            return false;
    }
    if (ssd1306_init(&d_, &bus, &cfg_, fb_) != 0)
        return false;
    if (wantShadow_ && !shadow_) // optional: without it display() just sends a bit more
        shadow_ = static_cast<uint8_t *>(malloc(SSD1306_FB_BYTES(cfg_.width, cfg_.height)));
    if (wantShadow_ && shadow_)
        return ssd1306_set_shadow(&d_, shadow_) == 0;
    return true;
}
