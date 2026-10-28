// Settings: the theme, how the USB Sender exports, auto off, and the buffer.
//
//   up, down            which row
//   left, right         its value, going round at the ends; a theme is taken
//                       at once
//   AUTO held           on the Buffer row: empty what the tester received
//   OK, RUN or MODE     back to the menu, and the settings are kept in flash
//                       if any of them changed (see Config.h)

#ifndef SETAPP_H
#define SETAPP_H

#include "dso152mod.h"
#include <stdint.h>

namespace Settings {

const uint8_t STAY  = 0;
const uint8_t LEAVE = 1;
const uint8_t ERASE = 2;        // the caller empties the buffer, then calls erased()

void    enter(DSO152TFT& tft, int bufLen);
uint8_t poll(DSO152TFT& tft);
void    erased(DSO152TFT& tft);

}
#endif
