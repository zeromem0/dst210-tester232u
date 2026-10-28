#include <Arduino.h>
#include "Frontend.h"
#include "Pins.h"

#define PIN_RELAY    PIN_C(4)
#define PIN_COUPLING PIN_C(5)
#define PIN_SENSEL0  PIN_E(4)
#define PIN_SENSEL1  PIN_E(5)
#define PIN_SENSEL2  PIN_E(6)

// TMR14 is not in the STM32F103xE headers; its registers by offset.
#define TMR14_REG(off) (*(volatile uint32_t *)(0x40002000u + (off)))

namespace Frontend {

// Range 8, 5 V a division: an RS232 line of +-12 V comes to about +-980
// counts either side of zero and still fits; range 7 would clip it.
static uint8_t g_range = 8;
static bool    g_dc    = true;

// PE6..PE4 for each range, the relay apart, as the FNIRSI firmware sets them
// (its 0x08008830).
static const uint8_t TAP[RANGE_MAX + 1] = { 0, 1, 3, 4, 5, 0, 1, 2, 4, 5 };

// Counts a volt: 409.6 a division (4096 over ten) at 10 mV .. 10 V a division.
static const uint16_t CPV[RANGE_MAX + 1] = { 40960, 20480, 8192, 4096, 2048, 819, 410, 205, 82, 41 };

// The offset PWM's compare value that puts zero volts at 2048 (128 of the 8
// bits). From two readings on ranges 6 and 7, about 0.2 counts of the 8 bits
// per step of it and the same on both: what is used when the unit's own
// calibration cannot be had.
static const uint16_t ZERO_CCR = 0x7EC;
static const uint16_t ZERO     = 2048;

// The unit's own offset calibration, in the FNIRSI settings page (0x08006000,
// written by the FNIRSI firmware only, at 0x0801DB98): for each range the compare value at the trace
// position -100 (+0x20) and at +100 (+0x5C); zero is the position 0, half
// way. That is set A, the one FNIRSI takes at 2.4 MS/s and below (timebase
// 10 on), where all the tester's rates are. Only read, and only from flash:
// loaded over SWD, with the debugger there, a read of flash faults.
//
// FNIRSI's position 0 is not the converter's middle: with the halfway values
// a probe on ground read 2104, 2088, 2104 on ranges 6, 7, 8 (2026-10-26),
// 2.5 to 3.5 steps of the 8 bits high. A step of the 8 bits is about 5 of the
// compare value (0.2 a step, measured), so CAL_TRIM takes 16 off every range,
// keeping the differences between ranges from the calibration.
#define CAL_PAGE  0x08006000u
#define CAL_TRIM  16
static uint16_t g_zeroCcr[RANGE_MAX + 1];
static bool     g_cal = false;

static void calLoad() {
    for (uint8_t i = 0; i <= RANGE_MAX; i++) g_zeroCcr[i] = ZERO_CCR;
#ifndef RAM_RUN
    const volatile uint8_t*  page = (const volatile uint8_t*)CAL_PAGE;
    const volatile uint16_t* lo   = (const volatile uint16_t*)(CAL_PAGE + 0x20);
    const volatile uint16_t* hi   = (const volatile uint16_t*)(CAL_PAGE + 0x5C);
    if (page[0] != 0x55 && page[0] != 0xAA) return;
    uint16_t z[RANGE_MAX + 1];
    for (uint8_t i = 0; i <= RANGE_MAX; i++) {
        // FNIRSI's own values sit about 0x600 and 0x9E0; anything far off is
        // not a calibration, and then none of it is taken
        if (lo[i] < 0x300 || hi[i] > 0xD00 || lo[i] + 0x100 > hi[i]) return;
        z[i] = (uint16_t)((lo[i] + hi[i] + 1) / 2 - CAL_TRIM);
    }
    for (uint8_t i = 0; i <= RANGE_MAX; i++) g_zeroCcr[i] = z[i];
    g_cal = true;
#endif
}

static void offsetBegin() {
    RCC->APB1ENR |= 1u << 8;                    // TMR14
    Pins::mode(PIN_A(7), 0xB);                  // TMR14_CH1, alternate push-pull
    TMR14_REG(0x00) &= ~1u;
    TMR14_REG(0x28) = 0;                        // PSC
    TMR14_REG(0x2C) = 0xFFE;                    // ARR, as FNIRSI
    TMR14_REG(0x18) = (TMR14_REG(0x18) & ~0x70u) | 0x70;   // CH1 PWM mode 2
    TMR14_REG(0x20) |= 3;                       // on, inverted
    TMR14_REG(0x34) = g_zeroCcr[g_range];
    TMR14_REG(0x44) |= 0x8000;
    TMR14_REG(0x00) |= 1;
}

void begin() {
    Pins::output(PIN_RELAY,    false);
    Pins::output(PIN_COUPLING, false);
    Pins::output(PIN_SENSEL0,  false);
    Pins::output(PIN_SENSEL1,  false);
    Pins::output(PIN_SENSEL2,  false);
    calLoad();
    offsetBegin();
    setCouplingDC(true);
    setRange(g_range);
}

void setRange(uint8_t idx) {
    if (idx > RANGE_MAX) idx = RANGE_MAX;
    g_range = idx;
    uint8_t tap = TAP[idx];
    Pins::write(PIN_RELAY,   idx < 5);
    Pins::write(PIN_SENSEL0, (tap     ) & 1);
    Pins::write(PIN_SENSEL1, (tap >> 1) & 1);
    Pins::write(PIN_SENSEL2, (tap >> 2) & 1);
    TMR14_REG(0x34) = g_zeroCcr[idx];
}

uint8_t range() { return g_range; }

// PC5 high is taken for DC: with it low the generator's 0..3 V square wave
// sat centred on the trace's rest level, as AC coupling puts it (2026-10-26).
void setCouplingDC(bool dc) { g_dc = dc; Pins::write(PIN_COUPLING, dc); }
bool couplingDC() { return g_dc; }

uint16_t countsPerVolt() { return CPV[g_range]; }
int32_t  countsToMilliVolts(int32_t counts) { return counts * 1000 / (int32_t)countsPerVolt(); }
uint16_t zeroCounts() { return ZERO; }
bool     calibrated() { return g_cal; }

void printVolts(Print& p, int32_t mV, uint8_t decimals, bool plus) {
    static const int32_t UNIT[4] = { 1000, 100, 10, 1 };
    if (decimals > 3) decimals = 3;
    int32_t step = UNIT[decimals];                      // mV per last digit
    int32_t a = mV < 0 ? -mV : mV;
    a = (a + step / 2) / step;                          // in last digits, rounded
    if (mV < 0 && a) p.print('-');
    else if (plus && mV > 0 && a) p.print('+');
    int32_t scale = 1000 / step;
    p.print(a / scale);
    if (decimals) {
        p.print('.');
        int32_t f = a % scale;
        for (int32_t d = scale / 10; d > 0; d /= 10) { p.print((char)('0' + f / d)); f %= d; }
    }
    p.print('V');
}

}
