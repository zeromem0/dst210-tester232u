#include <Arduino.h>
#include "Lcd.h"

// A16 selects command (0) or data (1); in 16-bit mode the XMC puts address
// bit 17 on A16.
#define LCD_CMD  (*(volatile uint16_t *)0x6001FFFEu)
#define LCD_DATA (*(volatile uint16_t *)0x60020000u)

// F1-style pin configuration: 0xB alternate push-pull 50 MHz, 0x3 output
// push-pull 50 MHz.
static void pinCfg(GPIO_TypeDef *port, uint16_t pins, uint32_t cfg) {
    for (uint8_t p = 0; p < 16; p++) {
        if (!(pins & (1u << p))) continue;
        volatile uint32_t *r = p < 8 ? &port->CRL : &port->CRH;
        uint8_t s = (p & 7) * 4;
        *r = (*r & ~(0xFu << s)) | (cfg << s);
    }
}

static const uint8_t kInit[] = {
    // command, data count, data...
    0x36, 1, 0x00,
    0x3A, 1, 0x05,
    0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
    0xB7, 1, 0x35,
    0xBB, 1, 0x28,
    0xC0, 1, 0x2C,
    0xC2, 1, 0x01,
    0xC3, 1, 0x0B,
    0xC4, 1, 0x20,
    0xC6, 1, 0x0F,
    0xD0, 2, 0xA4, 0xA1,
    0xE0, 14, 0xD0, 0x01, 0x08, 0x0F, 0x11, 0x2A, 0x36, 0x55, 0x44, 0x3A, 0x0B, 0x06, 0x11, 0x20,
    0xE1, 14, 0xD0, 0x02, 0x07, 0x0A, 0x0B, 0x18, 0x34, 0x43, 0x4A, 0x2B, 0x1B, 0x1C, 0x22, 0x1F,
    0x29, 0,
    0x36, 1, 0xA0,              // landscape, as FNIRSI sets it after the display is on
};

static void noStage(uint32_t) {}

void Lcd::begin(void (*stage)(uint32_t)) {
    if (!stage) stage = noStage;
    stage(2);
    RCC->APB2ENR |= RCC_APB2ENR_IOPDEN | RCC_APB2ENR_IOPEEN | RCC_APB2ENR_AFIOEN;
    RCC->AHBENR |= RCC_AHBENR_FSMCEN;
    stage(3);
    *(volatile uint32_t *)0x40010030u |= 1u << 27;      // IOMUX REMAP7: XMC_NADV_GMUX, as FNIRSI
    stage(4);

    pinCfg(GPIOD, 0xC7B3, 0xB);     // D0-D3, D13-D15, RD (PD4), WR (PD5), CS (PD7)
    pinCfg(GPIOD, 0x0800, 0xB);     // A16 (PD11): command/data
    pinCfg(GPIOE, 0xFF80, 0xB);     // D4-D12
    stage(5);
    pinCfg(GPIOD, 0x0040, 0x3);     // PD6: reset
    stage(6);

    FSMC_Bank1->BTCR[0] = 0x5010;               // 16 bit, write enable, extended timing
    FSMC_Bank1->BTCR[1] = 0x02020424;           // read timing
    // Write timing: address 7 and data 7 cycles. FNIRSI's 2 and 2, at
    // 240 MHz some 20 ns a write against the ST7789's 66, worked from RAM
    // after FNIRSI had set the screen up but not from flash at power-on: the
    // screen stayed white (v0.17..v0.19, 2026-10-26). 7 and 7 is about what
    // 2 and 2 came to at 72 MHz, where it always worked.
    FSMC_Bank1E->BWTR[0] = 0x00000707;
    volatile uint32_t *ext = (volatile uint32_t *)0xA0000220u;  // AT32 only
    *ext = (*ext & 0xFFFF0000u) | 0x08;
    FSMC_Bank1->BTCR[0] |= 1;
    stage(7);

    GPIOD->BSRR = 1u << 6; delay(10);
    GPIOD->BRR  = 1u << 6; delay(20);
    GPIOD->BSRR = 1u << 6; delay(20);
    stage(8);
    LCD_CMD = 0x11;
    stage(9);
    delay(120);
    stage(10);

    for (uint16_t i = 0; i < sizeof(kInit);) {
        LCD_CMD = kInit[i];
        uint8_t n = kInit[i + 1];
        for (uint8_t k = 0; k < n; k++) LCD_DATA = kInit[i + 2 + k];
        i += 2 + n;
    }
}

// TMR9 is not in the STM32F103xE headers; its registers by offset. PWM mode 2
// with the output inverted, as FNIRSI has it: high while the count is below
// the compare value.
#define TMR9_REG(off) (*(volatile uint32_t *)(0x40014C00u + (off)))

void Lcd::backlight(uint8_t percent) {
    RCC->APB2ENR |= (1u << 19) | RCC_APB2ENR_IOPAEN;    // TMR9 clock
    pinCfg(GPIOA, 0x0004, 0xB);                 // PA2: TMR9_CH1
    TMR9_REG(0x00) &= ~1u;                      // CTRL1: stop
    TMR9_REG(0x28) = SystemCoreClock / 100000 - 1;      // PSC: 100 kHz count (APB2 = SYSCLK)
    TMR9_REG(0x2C) = 99;                        // ARR: 1 kHz
    TMR9_REG(0x18) = (TMR9_REG(0x18) & ~0x70u) | 0x70;  // CH1 PWM mode 2
    TMR9_REG(0x20) |= 3;                        // CH1 output on, inverted
    TMR9_REG(0x34) = percent > 100 ? 100 : percent;     // CCR1
    TMR9_REG(0x44) |= 0x8000;                   // output enable, as FNIRSI
    TMR9_REG(0x00) |= 1;                        // run
}

uint32_t Lcd::readId() {
    LCD_CMD = 0x04;
    (void)LCD_DATA;                             // dummy read
    uint32_t id = 0;
    for (uint8_t k = 0; k < 3; k++) id = id << 8 | (LCD_DATA & 0xFF);
    return id;
}

void Lcd::window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    LCD_CMD = 0x2A;
    LCD_DATA = x0 >> 8; LCD_DATA = x0 & 0xFF; LCD_DATA = x1 >> 8; LCD_DATA = x1 & 0xFF;
    LCD_CMD = 0x2B;
    LCD_DATA = y0 >> 8; LCD_DATA = y0 & 0xFF; LCD_DATA = y1 >> 8; LCD_DATA = y1 & 0xFF;
    LCD_CMD = 0x2C;
}

void Lcd::drawPixel(int16_t x, int16_t y, uint16_t c) {
    if (x < 0 || y < 0 || x >= 320 || y >= 240) return;
    window(x, y, x, y);
    LCD_DATA = c;
}

void Lcd::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 320) w = 320 - x;
    if (y + h > 240) h = 240 - y;
    if (w <= 0 || h <= 0) return;
    window(x, y, x + w - 1, y + h - 1);
    for (uint32_t n = (uint32_t)w * h; n; n--) LCD_DATA = c;
}
