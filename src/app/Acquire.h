// The external converter + TMR4 + DMA1, sampling the BNC input continuously
// into a circular buffer, the way the FNIRSI firmware does it.
//
//   3PA1030 on PB0..PB7 (its top 8 bits), clocked by TMR4 CH3 on PB8.
//   TMR4's update asks DMA1 channel 7 for a copy of GPIOB's input register,
//   8 bit, circular, very high priority. Each half is widened to 16 bits,
//   times 16, as it is handed over: 0..4080, as on the DSO-152.
//
// Two ways of getting the halves out. Up to IRQ_MAX_HZ, and if a block
// function has been given, DMA1's half and full transfer interrupts call it
// with each half as it settles, so the decoder sees every sample whatever the
// screen is doing. Above that a half lasts less time than it takes to decode,
// an interrupt per half would leave the main loop nothing, and the flags are
// polled instead with readyHalf(): halves then go by unread while the screen
// is painted, and that is counted as overrun rather than hidden.
//
// The core leaves DMA1_Channel7_IRQHandler as a weak alias of its default
// handler, so defining it here clashes with nothing.

#ifndef ACQUIRE_H
#define ACQUIRE_H

#include <stdint.h>

// Code on the way from the DMA to the decoder: kept in RAM, copied there
// with .data at start (.RamFunc in the linker scripts). From flash the tester
// would run at 0x0806B000, far past the part of the AT32's flash that is read
// without waiting, and at 1.25 MS/s a half has to be decoded in well under a
// millisecond.
#define T232_RAM __attribute__((section(".RamFunc")))

namespace Acq {

// A half of this is what one screen shows, so the length sets how much of
// the line is visible at once. 1024 samples a half at 100 kS/s is about 98
// bit times, eight characters of 8N1 traffic, which is the sort of span a
// bench scope puts on screen. Going much longer costs RAM the decoder and
// the snapshot also need.
const uint16_t BUF_LEN = 2048;

// At 240 MHz a half of 1024 samples at 1.25 MS/s lasts 197 000 core cycles.
const uint32_t IRQ_MAX_HZ = 1250000;

// Called from the DMA interrupt. `overrun` says the half before went unread.
typedef void (*BlockFn)(const uint16_t* s, uint16_t n, bool overrun);

void     onBlock(BlockFn fn);           // before begin()
bool     interruptDriven();             // true: onBlock is live, do not poll

// Brackets for the main loop to change state the block function also uses.
void     pause();
void     resume();

// Forgets a half that settled while nobody was collecting, after a long
// pause, so that it is not handed over late and counted as an overrun.
void     discard();

void     begin(uint32_t sampleHz);
void     setSampleRate(uint32_t hz);
uint32_t sampleRate();                  // what was actually achieved

// Hands back a half of the buffer once it is no longer being written, or
// nullptr if neither half is ready. `overrun` says a half went by unread.
const uint16_t* readyHalf(uint16_t& n, bool& overrun);

// The lowest and highest sample of the half readyHalf() handed over last,
// found while widening it, so that the decoder need not go over it again.
uint16_t lastLo();
uint16_t lastHi();

// After readyHalf: false once the DMA has gone on into the half it handed
// over, so that what was read of it since may be part new samples.
bool     halfIntact();

}
#endif
