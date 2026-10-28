#include <Arduino.h>
#include "Config.h"
#include "Theme.h"
#include "Export.h"

// On the DST-210 the flash is read-protected and nothing is written to it:
// the settings live in RAM for as long as the image runs, starting from the
// defaults each time. (On the DSO-152 they were kept in a flash page.)

namespace Config {

static const uint16_t BLOCK[NBLOCK] = { 0, 64, 256, 1024 };

// Off, 10 and 30 minutes without a key or a byte received. 10 by default: a
// unit switched on by accident in a bag should not run for hours.
static const uint8_t     AUTOOFF_MIN[NAUTOOFF]  = { 0, 10, 30 };
static const char* const AUTOOFF_NAME[NAUTOOFF] = { "off", "10 min", "30 min" };

uint8_t format  = Export::DUMP;
uint8_t block   = 0;
uint8_t autoOff = 1;

uint16_t blockSizeAt(uint8_t i) { return i < NBLOCK ? BLOCK[i] : 0; }
uint16_t blockSize() { return blockSizeAt(block); }

uint32_t autoOffMs() { return autoOff < NAUTOOFF ? AUTOOFF_MIN[autoOff] * 60000ul : 0; }
const char* autoOffName(uint8_t i) { return i < NAUTOOFF ? AUTOOFF_NAME[i] : "?"; }

void load() {}

bool save() { return true; }

}
