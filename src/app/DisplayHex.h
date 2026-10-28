#ifndef DISPLAY_HEX_H
#define DISPLAY_HEX_H

#include "dso152mod.h"
#include "Theme.h"

// Geometria modului HEX: 15 octeti pe linie, 18 linii vizibile, 12 px pe linie
#define HEX_PER_LINE   15
#define HEX_VIS_LINES  18
#define HEX_LINE_H     12

// Deseneaza o singura linie (indice absolut 'line') la pozitia y
void displayHexLine(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int line, int y);
// Deseneaza fereastra completa incepand cu linia 'firstLine'
void displayHex(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int firstLine);

#endif
