#include <Arduino.h>
#include "Menu.h"
#include "Theme.h"
#include "Keys.h"
#include "Version.h"
#include "Probe.h"

namespace Menu {

// There is room for four of these; three are used.
static const char* const ITEM[] = { "RS232 Tester", "USB Sender", "Settings" };
static const int8_t NITEM = 3;
static int8_t g_sel = 0;

#define ITEM_X     148
#define ITEM_Y     34
#define ITEM_W     160
#define ITEM_H     30
#define ITEM_STEP  38
#define FOOT_Y     196

// On the left, top to bottom, a DE-9 plug seen from the front, after the
// drawing of commons.wikimedia.org/wiki/File:DE9_Diagram.svg, turned a quarter
// right: pin 1 at the top of the wide side. Drawn from a few shapes rather than
// kept as a picture, which saves the kilobyte the mascot took.
#define PLUG_X     70           // the middle of it
#define PLUG_Y     120

static const uint16_t METAL  = 0xAD55;
static const uint16_t SHELL  = 0x7BCF;
static const uint16_t GOLD   = 0xFE60;
static const uint16_t GOLD_D = 0x9300;

// Every shape here is one of these: columns centred on y = cy, from x0
// rightwards, wL tall at the left to wR at the right, w columns in all, with
// the corners taken off as by a circle of radius r. The D of the plug is
// taller at the right; with equal heights it is a rounded rectangle, and 2r+1
// wide and tall it is a circle. Cheaper than the library's circles, which
// would cost most of what leaving out the mascot saved.
static void shape(DSO152TFT& tft, int16_t cy, int16_t x0, int16_t wL, int16_t wR, int16_t w, int16_t r, uint16_t c) {
    for (int16_t x = 0; x < w; x++) {
        int16_t half = (int16_t)((wL * (w - 1 - x) + wR * x) / (w - 1) / 2);
        int16_t d = x < r ? r - x : (x >= w - r ? x - (w - 1 - r) : 0);
        int16_t in = 0;
        if (d > 0) {
            int16_t k = 0;
            while ((k + 1) * (k + 1) <= r * r + r - d * d) k++;
            in = r - k;
        }
        tft.fillRect(x0 + x, cy - half + in, 1, 2 * (half - in) + 1, c);
    }
}

static void circle(DSO152TFT& tft, int16_t x, int16_t y, int16_t r, uint16_t c) {
    shape(tft, y, x - r, 2 * r + 1, 2 * r + 1, 2 * r + 1, r, c);
}

static void pin(DSO152TFT& tft, int16_t x, int16_t y) {
    circle(tft, x, y, 5, GOLD_D);
    circle(tft, x, y, 4, GOLD);
    tft.fillRect(x - 2, y - 2, 2, 2, 0xFFF0);
}

static void drawPlug(DSO152TFT& tft) {
    // the flange, with its two screw holes
    shape(tft, PLUG_Y, PLUG_X - 40, 174, 174, 81, 8, METAL);
    for (int8_t s = -1; s <= 1; s += 2) {
        circle(tft, PLUG_X, PLUG_Y + s * 74, 8, SHELL);
        circle(tft, PLUG_X, PLUG_Y + s * 74, 5, TH.menuBg);
    }
    shape(tft, PLUG_Y, PLUG_X - 30, 100, 123, 61, 9, SHELL);
    shape(tft, PLUG_Y, PLUG_X - 25, 91, 112, 51, 7, TH.menuBg);     // the inside, in the theme's colour, as the holes

    // pins 1 to 5 down the wide side, 6 to 9 down the narrow one
    for (int8_t i = 0; i < 5; i++) pin(tft, PLUG_X + 9, PLUG_Y - 36 + i * 18);
    for (int8_t i = 0; i < 4; i++) pin(tft, PLUG_X - 11, PLUG_Y - 27 + i * 18);
}

static void drawItem(DSO152TFT& tft, int8_t i) {
    int16_t  y   = ITEM_Y + i * ITEM_STEP;
    bool     on  = (i == g_sel);
    uint16_t bg  = on ? TH.sel : TH.menuBg;
    tft.fillRect(ITEM_X, y, ITEM_W, ITEM_H, bg);
    tft.drawRect(ITEM_X, y, ITEM_W, ITEM_H, on ? TH.selText : TH.line);
    tft.setTextSize(2);
    tft.setTextColor(on ? TH.selText : TH.muted, bg);
    tft.setCursor(ITEM_X + 6, y + 8);
    tft.print(ITEM[i]);
}

void draw(DSO152TFT& tft) {
    tft.fillScreen(TH.menuBg);

    drawPlug(tft);

    for (int8_t i = 0; i < NITEM; i++) drawItem(tft, i);

    tft.setTextSize(1);
    tft.setTextColor(TH.muted, TH.menuBg);
    tft.setCursor(ITEM_X + 2, FOOT_Y);
    tft.print((char)0x1E); tft.print((char)0x1F);       // up and down arrows
    tft.print(" select   OK/RUN start");

    // which build this is, so that two of them are not mistaken for each other
    tft.setTextColor(TH.trace, TH.menuBg);
    tft.setCursor(ITEM_X + 2, FOOT_Y + 16);
    // one line: "T232-DSTu v0.32 240MHz", from x 150 (the screen holds 28)
    tft.setTextWrap(false);
    tft.print(FW_NAME " v" FW_VERSION);
    tft.setTextColor(TH.muted, TH.menuBg);
    tft.print(' ');
    tft.print(SystemCoreClock / 1000000);       // 240 (72 while the clock is still coming up)
    tft.print("MHz");
    tft.setTextWrap(true);
}

int8_t poll(DSO152TFT& tft) {
    int8_t was = g_sel;
    // round at the ends: down from the last goes to the first
    if (Keys::hit(Keys::UP))   g_sel = (int8_t)((g_sel + NITEM - 1) % NITEM);
    if (Keys::hit(Keys::DOWN)) g_sel = (int8_t)((g_sel + 1) % NITEM);
    if (g_sel != was) { drawItem(tft, was); drawItem(tft, g_sel); }

    if (Keys::hit(Keys::OK) || Keys::hit(Keys::RUN)) return g_sel;
    return -1;
}

}
