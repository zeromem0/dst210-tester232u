// The buttons, named as on the case. Each name was checked against its pin
// by watching the pins over SWD while the buttons were pressed (DST-210,
// 2026-10-26). OK is the DST-210's ENTER. The DSO-152 had a key of its own
// for RELEARN; here it is the power key, short (held, it switches off).

#ifndef KEYS_H
#define KEYS_H

#include <Arduino.h>

#define UP_BUTTON      PC6
#define DOWN_BUTTON    PC9
#define LEFT_BUTTON    PC7
#define RIGHT_BUTTON   PC8
#define OK_BUTTON      PD13
#define MODE_BUTTON    PB14
#define AUTO_BUTTON    PD12
#define RUN_BUTTON     PB15
#define POWER_BUTTON   PA6
#define RELEARN_BUTTON PA6

namespace Keys {

enum { UP, DOWN, LEFT, RIGHT, OK, MODE, AUTO, RUN, RELEARN, COUNT };

const uint8_t SHORT = 1;
const uint8_t LONG  = 2;

void begin();

// Whatever is held at this moment is taken as already dealt with. Called
// when one screen hands over to another, so that the press which brought the
// change about is not acted on a second time by the screen it led to.
void sync();

bool down(uint8_t k);
// Gone down since last asked. The key must have been up for a moment first:
// contacts bounce on the way up as well as on the way down, and the bounce of
// a key being let go would otherwise count as a press of it.
bool hit(uint8_t k);

// For a key that does two things, one on a short press and one when held, as
// OK does on the stock firmware: short belongs to the screen, held goes back
// to the menu. A short press is therefore only known for one when the key
// comes up again. Returns 0, SHORT, or LONG once while the key is still held.
// Use either this or hit() for a given key on a given screen, not both.
uint8_t press(uint8_t k);

}
#endif
