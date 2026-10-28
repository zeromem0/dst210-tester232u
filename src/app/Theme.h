// The colours of every screen, by what they are for and not by what they
// look like, so that a theme can change them all at once. The arrangement is
// the one of ats-mini's Themes.cpp: a table of themes and TH for the one in
// force. "Default" is what the instrument looked like before there were
// themes.

#ifndef THEME_H
#define THEME_H

#include <stdint.h>

struct ColorTheme {
    const char* name;
    uint16_t bg;            // the screen
    uint16_t text;          // what is read first: characters, big figures
    uint16_t text2;         // the rest of the text
    uint16_t muted;         // hints, what is switched off
    uint16_t line;          // rules, borders, empty steps
    uint16_t bar;           // the bar along the top, and boxes filled like it
    uint16_t barText;       // text on those
    uint16_t barHi;         // text on those that has to stand apart
    uint16_t sel;           // the chosen line of a menu
    uint16_t selText;
    uint16_t trace;         // the waveform; the accent colour elsewhere
    uint16_t mark;          // where the bits were read
    uint16_t ok;
    uint16_t ok2;           // a second green: borders, control characters
    uint16_t err;
    uint16_t menuBg;        // the start screen
};

namespace Theme {

extern const ColorTheme LIST[];
extern uint8_t idx;
uint8_t count();

}

#define TH (Theme::LIST[Theme::idx])

#endif
