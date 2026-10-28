// What the tester reports in RAM (`probe`, read over SWD by the test tools,
// which are not published), and the support it needs on the DST-210
// (src/hw/Probe.cpp): the clock from the crystal, the FNIRSI watchdog kept fed
// (it is running and cannot be stopped), a HardFault handler that shows the
// fault, and stages a test run can be stopped at.
#pragma once
#include <Arduino.h>

struct Probe {
    uint32_t magic;     // 'D210' once setup() has run; 'STOP', 'FULT'
    uint32_t loops;
    uint32_t ms;
    uint32_t hse;       // 1: crystal started, PLL to 240 MHz in use; 0: still on HSI
    uint32_t cfgr;      // RCC->CFGR as left by SystemClock_Config
    uint32_t sysclk;    // SystemCoreClock
    uint32_t keys;      // bit i: key i of kKeys held
    uint32_t idr[5];    // GPIOA..GPIOE IDR
    uint32_t lcd_id;    // RDDID
    uint32_t stage;     // last stage() reached
    uint32_t fault[5];  // HardFault: CFSR, HFSR, BFAR, MMFAR, stacked PC
    uint32_t adc[4];    // scope images: min, max, DMA position, trigger index
    uint8_t samples[16];
    uint32_t blkCyc;    // core cycles the last DMA half took to decode (DWT)
    uint32_t blkMax;    // the most any took
    uint32_t blkCount;  // halves decoded from the interrupt
};

// From the crystal to 240 MHz, SysTick set again (Probe.cpp). The start is
// at 72 MHz; main.cpp calls this once the menu is up.
void clock240();

constexpr uint32_t kMagicRun = 0x30313244;     // "D210"

extern "C" {
extern volatile Probe probe;
extern volatile uint32_t stop_at;
extern void (*volatile fault_show)(void);
}

// Records how far setup() got; stops there for good (watchdog fed) when the
// RAM loader asked for that stage.
void stage(uint32_t n);

// The nine keys, active low: MODE, RUN, AUTO, ENTER, UP, LEFT,
// RIGHT, DOWN, POWER. Bit i of the result: key i held.
extern const char *const kKeyNames[9];
void keysBegin();
uint32_t keysRead();
