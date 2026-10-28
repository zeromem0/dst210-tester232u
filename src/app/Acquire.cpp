#include <Arduino.h>
#include "Acquire.h"

// The decoder runs on every sample, up to 1.25 MS/s from the DMA interrupt:
// worth the speed over the size the rest of the build is optimised for.
#pragma GCC optimize("O2")
#include "Probe.h"

// The DST-210's converter is a 3PEAK 3PA1030 on PB0..PB7 (the top 8 of its
// 10 bits), clocked by TMR4 channel 3 on PB8. DMA1 channel 7 copies GPIOB's
// input register into a circular buffer on every TMR4 update, one byte a
// sample, as the FNIRSI firmware does it (its 0x0801D2DC). Each half is
// widened to 16 bits, times 16, as it is handed over, so that the rest of
// the tester sees the 0..4080 it saw from the DSO-152's 12-bit converter.

namespace Acq {

static volatile uint8_t g_raw[BUF_LEN] __attribute__((aligned(4)));
static uint16_t g_buf[BUF_LEN] __attribute__((aligned(4)));
static uint16_t g_lo = 0, g_hi = 0;     // of the half last widened
static uint32_t g_sampleHz = 0;
static BlockFn  g_fn  = 0;
static bool     g_irq = false;
static bool     g_up  = false;

// Below the system tick, so millis() keeps counting while a half is decoded.
static const uint32_t IRQ_PRIO = 5;

static void applyMode() {
    if (!g_up) return;
    // at 72 MHz (UP held at start) the interrupt keeps to the old 250 k
    uint32_t irqMax = SystemCoreClock >= 200000000u ? IRQ_MAX_HZ : 250000u;
    bool want = g_fn && g_sampleHz <= irqMax;
    NVIC_DisableIRQ(DMA1_Channel7_IRQn);
    if (want) DMA1_Channel7->CCR |=  (DMA_CCR_HTIE | DMA_CCR_TCIE);
    else      DMA1_Channel7->CCR &= ~(DMA_CCR_HTIE | DMA_CCR_TCIE);
    DMA1->IFCR = DMA_IFCR_CGIF7;
    g_irq = want;
    if (want) {
        NVIC_SetPriority(DMA1_Channel7_IRQn, IRQ_PRIO);
        NVIC_EnableIRQ(DMA1_Channel7_IRQn);
    }
}

void onBlock(BlockFn fn) { g_fn = fn; applyMode(); }
bool interruptDriven() { return g_irq; }
void pause()  { if (g_irq) NVIC_DisableIRQ(DMA1_Channel7_IRQn); }
void resume() { if (g_irq) NVIC_EnableIRQ(DMA1_Channel7_IRQn); }

void discard() {
    DMA1->IFCR = DMA_IFCR_CGIF7;
    NVIC_ClearPendingIRQ(DMA1_Channel7_IRQn);
}

// TMR4 hangs off APB1. Its clock is PCLK1 when APB1 is undivided, twice it
// otherwise, which is the usual STM32 timer rule.
static uint32_t timerClock() {
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t ppre1 = (RCC->CFGR >> 8) & 0x7u;
    return (ppre1 & 0x4u) ? pclk1 * 2u : pclk1;
}

uint32_t sampleRate() { return g_sampleHz; }

void setSampleRate(uint32_t hz) {
    if (hz == 0) return;
    uint32_t tclk = timerClock();
    uint32_t psc  = 0;
    uint32_t div  = (tclk + hz / 2) / hz;
    while (div > 65536u) { psc++; div = (tclk / (psc + 1u) + hz / 2) / hz; }
    if (div < 2u) div = 2u;

    TIM4->PSC  = psc;
    TIM4->ARR  = div - 1u;
    TIM4->CCR3 = div / 2u;              // the converter's clock, half high
    TIM4->EGR  = TIM_EGR_UG;            // load the shadow registers now
    g_sampleHz = tclk / (psc + 1u) / div;
    applyMode();
}

void begin(uint32_t sampleHz) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    RCC->AHBENR  |= RCC_AHBENR_DMA1EN;

    GPIOB->CRL = 0x44444444;            // PB0..PB7: the converter's data, floating in
    GPIOB->CRH = (GPIOB->CRH & ~0xFu) | 0xBu;   // PB8: TMR4_CH3, alternate push-pull

    DMA1_Channel7->CCR   = 0;
    DMA1_Channel7->CPAR  = (uint32_t)&GPIOB->IDR;
    DMA1_Channel7->CMAR  = (uint32_t)g_raw;
    DMA1_Channel7->CNDTR = BUF_LEN;
    // The AT32's flexible request mapping, as FNIRSI sets it: channel 7 <-
    // request 79, TMR4's update. The fixed STM32 mapping gives channel 7
    // TIM4_UP anyway.
    volatile uint32_t* flex = (volatile uint32_t*)0x400200A4u;
    *flex = (*flex & 0xFF00FFFFu) | 0x01000000u | (0x4Fu << 16);
    DMA1_Channel7->CCR   = DMA_CCR_MINC                 // 8 bit either side
                         | DMA_CCR_CIRC
                         | DMA_CCR_PL_0 | DMA_CCR_PL_1;
    DMA1->IFCR = DMA_IFCR_CGIF7;
    DMA1_Channel7->CCR  |= DMA_CCR_EN;

    TIM4->CR1   = 0;
    TIM4->CR2   = TIM_CR2_MMS_1;                        // TRGO on update, as FNIRSI
    TIM4->CCMR2 = TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3PE;  // PWM 1
    TIM4->CCER  = TIM_CCER_CC3E | TIM_CCER_CC3P;
    TIM4->DIER  = TIM_DIER_UDE;                         // a DMA request on each update
    setSampleRate(sampleHz);
    TIM4->CR1  |= TIM_CR1_ARPE | TIM_CR1_CEN;

    g_up = true;
    applyMode();
}

// The half that has settled, widened into g_buf, and its lowest and highest
// sample on the way: four samples to a word read and two words written. One
// sample at a time this took 16 000 cycles a half, and the minimum and
// maximum, in the decoder, another 24 000 (2026-10-26). (The core is built
// for the Cortex-M3, so the M4's byte-wise SIMD is not to be had.)
T232_RAM static const uint16_t* widen(uint16_t from) {
    const uint32_t* src = (const uint32_t*)(const void*)(g_raw + from);
    uint32_t*       dst = (uint32_t*)(void*)(g_buf + from);
    uint32_t lo = 0xFF, hi = 0;
    for (uint16_t k = 0; k < BUF_LEN / 8; k++) {
        uint32_t w  = src[k];
        uint32_t b0 = w & 0xFF, b1 = (w >> 8) & 0xFF, b2 = (w >> 16) & 0xFF, b3 = w >> 24;
        dst[2 * k]     = (b0 | b1 << 16) << 4;
        dst[2 * k + 1] = (b2 | b3 << 16) << 4;
        uint32_t m01 = b0 < b1 ? b0 : b1, m23 = b2 < b3 ? b2 : b3;
        uint32_t x01 = b0 < b1 ? b1 : b0, x23 = b2 < b3 ? b3 : b2;
        uint32_t m = m01 < m23 ? m01 : m23, x = x01 < x23 ? x23 : x01;
        if (m < lo) lo = m;
        if (x > hi) hi = x;
    }
    g_lo = (uint16_t)(lo << 4);
    g_hi = (uint16_t)(hi << 4);
    return g_buf + from;
}

uint16_t lastLo() { return g_lo; }
uint16_t lastHi() { return g_hi; }

T232_RAM const uint16_t* readyHalf(uint16_t& n, bool& overrun) {
    uint32_t isr = DMA1->ISR;
    bool ht = (isr & DMA_ISR_HTIF7) != 0;
    bool tc = (isr & DMA_ISR_TCIF7) != 0;

    n = BUF_LEN / 2;
    if (tc) {
        // The pointer has wrapped, so the upper half is settled. Both flags
        // up means the lower half came and went without being read.
        overrun = ht;
        DMA1->IFCR = DMA_IFCR_CTCIF7 | DMA_IFCR_CHTIF7;
        return widen(BUF_LEN / 2);
    }
    if (ht) {
        overrun = false;
        DMA1->IFCR = DMA_IFCR_CHTIF7;
        return widen(0);
    }
    overrun = false;
    n = 0;
    return 0;
}

// readyHalf cleared both flags. The next one up means the DMA finished the
// other half and has started on the one handed over.
bool halfIntact() {
    return !(DMA1->ISR & (DMA_ISR_HTIF7 | DMA_ISR_TCIF7));
}

T232_RAM void dispatchFromIrq() {
    uint16_t n;
    bool over;
    uint32_t t0 = DWT->CYCCNT;
    const uint16_t* half = readyHalf(n, over);
    if (half && g_fn) g_fn(half, n, over);
    uint32_t dt = DWT->CYCCNT - t0;
    probe.blkCyc = dt;
    if (dt > probe.blkMax) probe.blkMax = dt;
    probe.blkCount++;
}

}

extern "C" T232_RAM void DMA1_Channel7_IRQHandler(void) { Acq::dispatchFromIrq(); }
