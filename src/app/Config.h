// The settings: the theme, how the sender exports, and when the unit
// switches itself off.
//
// On the DST-210 they are kept in RAM only (Config.cpp): its flash is
// read-protected and this firmware writes nothing to it. On the DSO-152 they
// lived in a flash page over a power cycle.

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

namespace Config {

extern uint8_t format;          // Export::RAW or DUMP
extern uint8_t block;           // which of the block sizes

extern uint8_t autoOff;         // which of the auto-off times

const uint8_t NBLOCK = 4;
const uint8_t NAUTOOFF = 3;
uint32_t autoOffMs();           // 0: never
const char* autoOffName(uint8_t i);
uint16_t blockSize();           // 0: everything in one
uint16_t blockSizeAt(uint8_t i);

void load();                    // at start; defaults if nothing valid is there
bool save();                    // only writes if something differs; false: it did not take

}
#endif
