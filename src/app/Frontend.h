// The analogue front end ahead of the ADC: the attenuator relay (PC4), the
// coupling switch (PC5), the three lines that pick a tap on the 74HC4051
// (PE4..PE6), and the offset that places zero volts on the converter (a PWM
// on PA7, TMR14).
//
// Ten sensitivity ranges, 0 to 9, as the FNIRSI firmware steps through them
// (0x08008830): the relay on for 0..4, the mux on taps 0,1,3,4,5 and then
// 0,1,2,4,5. With a x1 probe, 10 mV a division on range 0 up to 10 V on 9.

#ifndef FRONTEND_H
#define FRONTEND_H

#include <Arduino.h>

namespace Frontend {

const uint8_t RANGE_MAX = 9;

void    begin();
void    setRange(uint8_t idx);
uint8_t range();
void    setCouplingDC(bool dc);
bool    couplingDC();

// Volts at the probe tip, with the probe at x1 (at x10, ten times as many),
// in the converter's counts as Acquire hands them over: the 8 bits the
// DST-210 reads, times 16, so 0..4080 as on the DSO-152's 12-bit converter.
//
// Measured on range 7 (2026-10-26): the generator's 0..3 V square wave came
// to about 40 of the 8-bit counts, 13 a volt, 205 here: 2 V a division over
// the converter's ten. The other ranges follow from the 1-2-5 steps; they
// have not been measured. Zero volts sits at 2048 by way of the offset: on
// each range from the unit's own FNIRSI calibration when there is one
// (calibrated()), else an estimate from two readings, the same on all.
uint16_t countsPerVolt();                   // for the range in force
int32_t  countsToMilliVolts(int32_t counts); // a difference of counts, in mV
uint16_t zeroCounts();                      // where zero volts sits
bool     calibrated();

// Prints millivolts as volts with `decimals` places (0..3), rounded, with a
// '+' in front of positive values if `plus`.
void     printVolts(Print& p, int32_t mV, uint8_t decimals, bool plus);

}
#endif
