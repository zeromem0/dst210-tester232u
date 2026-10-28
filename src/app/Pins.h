// The few pins that are set or read one at a time (keys, power, the
// front-end switches, the screen's control lines), by their registers. The
// core's pinMode() and digitalWrite() did the same through its pin tables,
// which cost some 1.5 kB of flash that the bus health view needed.

#ifndef PINS_H
#define PINS_H

#include <Arduino.h>

namespace Pins {

struct Pin { uint32_t base; uint8_t bit; };

#define PIN_A(n) (Pins::Pin{ GPIOA_BASE, (n) })
#define PIN_B(n) (Pins::Pin{ GPIOB_BASE, (n) })
#define PIN_C(n) (Pins::Pin{ GPIOC_BASE, (n) })
#define PIN_D(n) (Pins::Pin{ GPIOD_BASE, (n) })
#define PIN_E(n) (Pins::Pin{ GPIOE_BASE, (n) })

inline GPIO_TypeDef* port(Pin p) { return (GPIO_TypeDef*)p.base; }

// MODE and CNF of the pin, four bits in CRL or CRH
inline void mode(Pin p, uint32_t cfg) {
    volatile uint32_t* cr = p.bit < 8 ? &port(p)->CRL : &port(p)->CRH;
    uint32_t sh = (uint32_t)(p.bit & 7) * 4u;
    *cr = (*cr & ~(0xFu << sh)) | (cfg << sh);
}

inline void write(Pin p, bool high) { port(p)->BSRR = high ? (1u << p.bit) : (1u << (p.bit + 16)); }
inline bool read(Pin p)             { return (port(p)->IDR >> p.bit) & 1u; }

inline void output(Pin p, bool high) { write(p, high); mode(p, 0x3); }   // push-pull, 50 MHz
inline void inputPullUp(Pin p)       { port(p)->BSRR = 1u << p.bit; mode(p, 0x8); }

// The ports' clocks. The SWJ setting is left as it is: on the DST-210 no
// JTAG pin is needed as a plain pin, and SWD is how images get in.
inline void begin() {
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN
                  | RCC_APB2ENR_IOPDEN | RCC_APB2ENR_IOPEEN | RCC_APB2ENR_AFIOEN;
}

}
#endif
