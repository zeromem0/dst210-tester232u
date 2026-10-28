// Bus health, after the Bus Health Test of the Fluke 125: the electrical
// side of the line only, not what it carries. Levels high and low, the real
// bit rate, rise and fall times as a share of a bit and timing jitter, each
// against the EIA-232 limits (or logic levels in TTL mode) and marked OK,
// WEAK (within the last tenth of a limit) or BAD. Overshoot, which the
// Fluke leaves out for EIA-232 too, is not measured.
//
// It needs edges finer than the inspector samples at, so while this view is
// up the converter runs at 2 MS/s (main.cpp sets it) and the decoder is not
// fed. Every half buffer with edges in it adds to the figures, which build
// up from when the view was entered or RUN was pressed.

#ifndef HEALTH_H
#define HEALTH_H

#include "dso152mod.h"
#include <stdint.h>

namespace Health {

const uint32_t SAMPLE_HZ = 2000000;

// Hands over the next block of samples, or nullptr if there is none yet.
typedef const uint16_t* (*Source)(uint16_t& n);

void enter(DSO152TFT& tft, Source src, uint32_t sampleHz);
void restart(DSO152TFT& tft);           // forget the figures, start again
void poll(DSO152TFT& tft);              // take what has come in; redraw now and then
void draw(DSO152TFT& tft);

}
#endif
