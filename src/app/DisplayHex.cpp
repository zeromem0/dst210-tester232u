#include "DisplayHex.h"
#include <ctype.h>

void displayHexLine(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int line, int y) {
    int start = line * HEX_PER_LINE;
    int count = bufferIndex - start;
    if (count > HEX_PER_LINE) count = HEX_PER_LINE;

    tft.fillRect(0, y, 320, HEX_LINE_H, TH.bg);
    if (count <= 0) return;

    tft.setTextSize(1);
    tft.setTextColor(TH.text2);

    // coloana de caractere
    tft.setCursor(3, y);
    for (int j = 0; j < count; j++) {
        uint8_t c = (uint8_t)serialBuffer[start + j];
        tft.print((c >= 32 && c < 127) ? (char)c : '_');
    }

    // coloana hex
    int xPos = 106;
    for (int j = 0; j < count; j++) {
        uint8_t c = (uint8_t)serialBuffer[start + j];
        tft.setCursor(xPos, y);
        if (c < 0x10) tft.print('0');
        tft.print(c, HEX);
        xPos += 14;
    }
}

void displayHex(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int firstLine) {
    for (int i = 0; i < HEX_VIS_LINES; i++) {
        displayHexLine(tft, serialBuffer, bufferIndex, firstLine + i, 20 + i * HEX_LINE_H);
    }
}
