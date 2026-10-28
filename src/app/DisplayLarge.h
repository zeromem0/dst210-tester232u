#ifndef DISPLAY_LARGE_H
#define DISPLAY_LARGE_H

// #define USE_FUNDAL   // jos88.h left behind: 14080 bytes of flash
#ifdef USE_FUNDAL
#include "jos88.h"
#endif

#include "dso152mod.h"
#include "Theme.h"

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// Fundalul de jos (jos88): se deseneaza o singura data, la intrarea in mod
void displayLargeBackground(DSO152TFT& tft);
// Textul (ultimele 32 de caractere + ultimele 3 marite); nu redeseneaza fundalul complet
void displayLarge(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int start);
bool largeBufferChanged(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int start);

#endif
