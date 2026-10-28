// The DST-210 screen: an ST7789 on the XMC (the AT32's FSMC), 16-bit bus,
// 320x240 landscape, RGB565, one bus write per pixel. Everything here is taken
// from the FNIRSI start-up (its 0x0801F0B0): pins, bus timing, reset
// pulse on PD6 and the controller's register sequence.
#pragma once
#include <Adafruit_GFX.h>

class Lcd : public Adafruit_GFX {
public:
    Lcd() : Adafruit_GFX(320, 240) {}
    // `stage`, when given, is called with 2..10 as begin() goes, for a probe
    // to report how far it got (or to stop there).
    void begin(void (*stage)(uint32_t) = nullptr);
    // RDDID (0x04): 0x858552 on an ST7789V.
    uint32_t readId();
    // Backlight: 1 kHz PWM on PA2 (TMR9 channel 1), as FNIRSI sets it up;
    // `percent` 0..100 is the compare value against a period of 100.
    void backlight(uint8_t percent);
    void drawPixel(int16_t x, int16_t y, uint16_t c) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { fillRect(x, y, w, 1, c); }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { fillRect(x, y, 1, h, c); }
    void fillScreen(uint16_t c) override { fillRect(0, 0, 320, 240, c); }

private:
    void window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
};
