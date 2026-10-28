// What the start screen says this firmware is, so that one build is not
// taken for another once several are about. Raise FW_VERSION with each build
// that goes to an instrument; the date is the compiler's.

#ifndef VERSION_H
#define VERSION_H

#define FW_NAME     "T232-DSTu"
#define FW_VERSION "0.33"
#define FW_DATE     __DATE__

#endif
