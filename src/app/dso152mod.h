#pragma once

// The screen, under the name the tester's views know it by. On the DST-210 it
// is an ST7789 on the XMC (src/hw/Lcd.h, set up as the FNIRSI firmware does),
// with the backlight on PA2; on the DSO-152 this header held a driver for the
// same controller on a bit-banged bus, colours inverted.

#include "Pins.h"
#include "Lcd.h"

typedef Lcd DSO152TFT;
