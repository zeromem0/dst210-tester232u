// The teaching view: one character, the first of the stretch on screen, drawn
// large enough to read bit by bit, with what the line is doing named on it.
//
//   the two levels of the line, mark (a 1, the line at rest) and space (a 0),
//   and in RS232 mode zero volts between them;
//   the threshold the line is split at, which up and down move by hand here;
//   a cell for each bit of the frame: the start bit, the data bits from the
//   lowest, parity if there is one, the stop bit; the point where each was
//   read, and what it was read as;
//   and the byte that comes of it, the lowest bit having come first.

#ifndef TEACH_H
#define TEACH_H

#include "dso152mod.h"
#include <stdint.h>

namespace Teach {

// `frozen`: the samples are a capture or a held screen and will not change.
// Then a character found once stays where it was found even when the
// threshold is moved so far that the decoder no longer sees it, so that what
// a bad threshold does can be watched on the same bits.
void draw(DSO152TFT& tft, const uint16_t* s, uint16_t n, bool frozen);

// Which of the characters under the picture is drawn, on a still one: the
// first after a new capture; left and right step through them (true if it
// moved).
void select(uint8_t i);
bool step(int8_t d);

}
#endif
