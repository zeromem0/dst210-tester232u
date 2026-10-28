#include "DisplayChars.h"
#include <ctype.h>

void displayCharsLine(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int line, int y) {
    int start = line * CHARS_PER_LINE;
    int count = bufferIndex - start;
    if (count > CHARS_PER_LINE) count = CHARS_PER_LINE;

    tft.fillRect(0, y, 320, CHARS_LINE_H, TH.bg);
    if (count <= 0) return;

    tft.setTextSize(1);
    tft.setTextColor(TH.text2);
    tft.setCursor(3, y);
    for (int j = 0; j < count; j++) {
        uint8_t c = (uint8_t)serialBuffer[start + j];
        tft.print((c >= 32 && c < 127) ? (char)c : '_');
    }
}

void displayChars(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int firstLine) {
    for (int i = 0; i < CHARS_VIS_LINES; i++) {
        displayCharsLine(tft, serialBuffer, bufferIndex, firstLine + i, 20 + i * CHARS_LINE_H);
    }
}
