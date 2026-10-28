#ifndef DISPLAY_CHARS_H
#define DISPLAY_CHARS_H

#include "dso152mod.h"
#include "Theme.h"

// Geometria modului CHARS: 52 caractere pe linie, 19 linii vizibile, 11 px pe linie
#define CHARS_PER_LINE   52
#define CHARS_VIS_LINES  19
#define CHARS_LINE_H     11

void displayCharsLine(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int line, int y);
void displayChars(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int firstLine);

#endif
