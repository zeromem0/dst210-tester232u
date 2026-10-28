// Sends what the tester received to the PC over the USB serial port, in the format set in Settings (see Export.h): raw, a
// hex dump with a CRC-32 a block. (No Intel HEX in this build.) The screen shows the CRC-32
// of what RUN will send, to compare with that of what arrived;
// tools/usb_receive.ps1 -Port COMx takes it on the PC.
//
// How it is sent, and in blocks of what size, is set in Settings.
//
//   up, down           everything, or one block of it
//   RUN                send; RUN again stops it, as does closing the port
//   MODE, or OK held   back to the menu

#ifndef TTLAPP_H
#define TTLAPP_H

#include "dso152mod.h"
#include <stdint.h>

namespace Ttl {

void enter(DSO152TFT& tft, const char* buf, int len);
bool poll(DSO152TFT& tft, const char* buf, int len);     // false: leave
void leave();

}
#endif
