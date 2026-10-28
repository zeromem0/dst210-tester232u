#include "DisplayLarge.h"
#include <ctype.h>
#include <string.h>

static char lastLargeBuffer[33] = {0};

// Fundal: imagine 160x44, afisata la scara 2 incepand cu y = 152
#define BG_W      160
#define BG_H      44
#define BG_Y0     152

static inline uint16_t bgPixel(int bx, int by) {
#ifdef USE_FUNDAL
    uint32_t index = (by * BG_W + bx) * 2;
    return (jos88[index] << 8) | jos88[index + 1];
#else
    (void)bx; (void)by;
    return TH.bg;
#endif
}

// Reface fundalul doar in dreptunghiul dat (coordonate ecran)
static void restoreBackground(DSO152TFT& tft, int x, int y, int w, int h) {
    int bx0 = x / 2, bx1 = (x + w - 1) / 2;
    int by0 = (y - BG_Y0) / 2, by1 = (y + h - 1 - BG_Y0) / 2;
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 >= BG_W) bx1 = BG_W - 1;
    if (by1 >= BG_H) by1 = BG_H - 1;
    for (int by = by0; by <= by1; by++)
        for (int bx = bx0; bx <= bx1; bx++)
            tft.fillRect(bx * 2, by * 2 + BG_Y0, 2, 2, bgPixel(bx, by));
}

void displayLargeBackground(DSO152TFT& tft) {
    restoreBackground(tft, 0, BG_Y0, SCREEN_WIDTH, BG_H * 2);
}

// Un rand de 16 caractere marite (marime 3), cu CR/LF evidentiate
static void drawCharRow(DSO152TFT& tft, const char* buf, int bufferIndex, int start, int y) {
    const int charsPerLine = 16, charWidth = 18;
    const int startX = (SCREEN_WIDTH - (charsPerLine * charWidth)) / 2;
    tft.setTextSize(3);
    for (int i = 0; i < charsPerLine; i++) {
        int idx = start + i;
        int x = startX + i * charWidth;
        tft.setCursor(x, y);
        if (idx < bufferIndex) {
            char c = buf[idx];
            if (c == '\r' || c == '\n') {           // CR -> 'D', LF -> 'A' pe fond verde
                tft.fillRect(x - 1, y - 1, charWidth, 25, TH.ok);
                tft.setTextColor(TH.bg, TH.ok);
                tft.print(c == '\r' ? 'D' : 'A');
            } else {
                tft.fillRect(x - 1, y - 1, charWidth, 25, TH.bg);
                tft.setTextColor(TH.ok, TH.bg);
                tft.print(((uint8_t)c >= 32 && (uint8_t)c < 127) ? c : '.');
            }
        } else {
            tft.fillRect(x - 1, y - 1, charWidth, 25, TH.bg);
            tft.setTextColor(TH.ok, TH.bg);
            tft.print(' ');
        }
    }
}

// Randul hex de sub caractere (marime 1, gri)
static void drawHexRow(DSO152TFT& tft, const char* buf, int bufferIndex, int start, int y) {
    const int charsPerLine = 16, charWidth = 18;
    const int startX = (SCREEN_WIDTH - (charsPerLine * charWidth)) / 2;
    tft.setTextSize(1);
    tft.setTextColor(TH.muted, TH.bg);
    for (int i = 0; i < charsPerLine; i++) {
        int idx = start + i;
        tft.setCursor(startX + i * charWidth + (charWidth - 12) / 2, y);
        if (idx < bufferIndex) {
            uint8_t c = (uint8_t)buf[idx];
            if (c < 0x10) tft.print('0');
            tft.print(c, HEX);
        } else {
            tft.print("  ");
        }
    }
}

void displayLarge(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int start) {
    const int yChars1 = 40;
    const int yHex1   = yChars1 + 30;
    const int yChars2 = yHex1 + 12 + 5;
    const int yHex2   = yChars2 + 30;

    drawCharRow(tft, serialBuffer, bufferIndex, start,      yChars1);
    drawHexRow (tft, serialBuffer, bufferIndex, start,      yHex1);
    drawCharRow(tft, serialBuffer, bufferIndex, start + 16, yChars2);
    drawHexRow (tft, serialBuffer, bufferIndex, start + 16, yHex2);

    // Sufixul: ultimele 5 caractere primite, marite (marime 4), peste fundal.
    // Se iau de la capatul bufferului, nu din fereastra de 32, ca sa apara si
    // cand s-au primit mai putin de 32 de caractere; ultimul sta mereu in
    // caseta din dreapta. Cinci casete a cate 60 px, cea mai lata (CR/LF)
    // de 56, incap in cei 320.
    const int suffixLen = 5;
    const int charWidth = 60, startX = 12, y = 180;
    // Litera de marime 4 sta in celula ei de 24 x 32 cu desenul in coltul din
    // stanga sus: 20 de pixeli lati si 28 inalti, apoi o coloana si un rand
    // goale, adica 4 pixeli in dreapta si 4 jos. Ca spatiul pana la chenar sa
    // fie acelasi pe toate laturile, se lasa tot 4 si in stanga si sus, iar
    // chenarul are 2 pixeli peste tot.
    const int GAP = 4, BORDER = 2, OUT = GAP + BORDER;
    tft.setTextSize(4);
    for (int i = 0; i < suffixLen; i++) {
        int idx = bufferIndex - suffixLen + i;
        int x = startX + i * charWidth;
        // se reface fundalul pe toata latimea maxima a casetei (cea de CR/LF)
        restoreBackground(tft, x - OUT, y - OUT, 48 + 2 * BORDER + GAP, 32 + GAP + 2 * BORDER);
        if (idx < 0) continue;                      // fara caracter: ramane fundalul
        char c = serialBuffer[idx];
        tft.setCursor(x, y);
        int cells = (c == '\r' || c == '\n') ? 2 : 1;
        int innerW = 24 * cells + GAP, innerH = 32 + GAP;   // GAP in fata, celula aduce restul
        uint16_t fill = (cells == 2) ? TH.ok : TH.bg;
        tft.fillRect(x - OUT, y - OUT, innerW + 2 * BORDER, innerH + 2 * BORDER, TH.ok2);
        tft.fillRect(x - GAP, y - GAP, innerW, innerH, fill);
        if (cells == 2) {
            tft.setTextColor(TH.bg, TH.ok);
            tft.print(c == '\r' ? "CR" : "LF");
        } else {
            tft.setTextColor(TH.ok, TH.bg);
            tft.print(((uint8_t)c >= 32 && (uint8_t)c < 127) ? c : '.');
        }
    }

    tft.setTextColor(TH.text, TH.bg);
}

bool largeBufferChanged(DSO152TFT& tft, const char* serialBuffer, int bufferIndex, int start) {
    (void)tft;
    char currentBuffer[33] = {0};
    for (int i = start, j = 0; i < bufferIndex && j < 32; i++, j++) {
        currentBuffer[j] = serialBuffer[i];
    }
    bool changed = memcmp(currentBuffer, lastLargeBuffer, 32) != 0;
    if (changed) {
        memcpy(lastLargeBuffer, currentBuffer, 32);
    }
    return changed;
}
