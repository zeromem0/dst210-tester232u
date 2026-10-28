#include <Arduino.h>
#include "SetApp.h"
#include "Theme.h"
#include "Keys.h"
#include "Config.h"
#include "Export.h"

namespace Settings {

enum { ROW_THEME, ROW_FORMAT, ROW_BLOCK, ROW_AUTOOFF, ROW_BUFFER, NROW };
static const char* const LABEL[NROW] = { "Theme", "Export", "Block", "Auto off", "Buffer" };
static uint8_t g_row = 0;
static int     g_len = 0;           // what the buffer holds
static bool    g_erased = false;

#define ROW_X    8
#define ROW_Y    26
#define ROW_H    26             // five rows above the note
#define ROW_W    304
#define VALUE_X  122          // after "Auto off", 8 characters of 12 from x 14
#define NOTE_Y   162

static void printHex(DSO152TFT& tft, uint32_t v, uint8_t digits) {
    while (digits--) tft.print((char)"0123456789ABCDEF"[(v >> (digits * 4)) & 0xF]);
}

static void drawRow(DSO152TFT& tft, uint8_t r) {
    int16_t  y  = (int16_t)(ROW_Y + r * ROW_H);
    bool     on = (r == g_row);
    uint16_t bg = on ? TH.sel : TH.bg;

    tft.fillRect(ROW_X, y, ROW_W, ROW_H - 2, bg);
    tft.setTextSize(2);
    tft.setTextColor(on ? TH.selText : TH.muted, bg);
    tft.setCursor(ROW_X + 6, y + 3);
    tft.print(LABEL[r]);

    tft.setTextColor(on ? TH.selText : TH.text, bg);
    tft.setCursor(VALUE_X, y + 3);
    switch (r) {
        case ROW_THEME:  tft.print(TH.name); break;
        case ROW_FORMAT: tft.print(Export::name(Config::format)); break;
        case ROW_BLOCK:
            if (Config::blockSize()) { tft.print(Config::blockSize()); tft.print(" bytes"); }
            else                     tft.print("all in one");
            break;
        case ROW_AUTOOFF:
            tft.print(Config::autoOffName(Config::autoOff));
            break;
        case ROW_BUFFER:
            tft.print(g_len); tft.print(" bytes");
            break;
    }
}

// What the setting under the bar means, in a line or two.
static void drawNote(DSO152TFT& tft) {
    tft.fillRect(0, NOTE_Y, 320, 44, TH.bg);
    tft.setTextSize(1);
    tft.setTextColor(TH.text2, TH.bg);
    tft.setCursor(ROW_X + 6, NOTE_Y);
    switch (g_row) {
        case ROW_THEME: {
            // the theme's own colours, since its name says little
            tft.print("screen, text, trace, ok, error:");
            const uint16_t sw[5] = { TH.bg, TH.text, TH.trace, TH.ok, TH.err };
            for (uint8_t k = 0; k < 5; k++) {
                int16_t x = (int16_t)(ROW_X + 6 + k * 26);
                tft.fillRect(x, NOTE_Y + 14, 22, 22, sw[k]);
                tft.drawRect(x, NOTE_Y + 14, 22, 22, TH.line);
            }
            break;
        }
        case ROW_FORMAT:
            if (Config::format == Export::RAW)       tft.print("the bytes as they are, nothing added");
            else                                     tft.print("offset, hex and text, 16 bytes a line, CRC-32");
            tft.setCursor(ROW_X + 6, NOTE_Y + 14);
            tft.print("on the PC: tools/usb_receive.py COMx");
            break;
        case ROW_BLOCK:
            tft.print("the sender can send one block again on its own");
            break;
        case ROW_AUTOOFF:
            tft.print("switched off after so long without a key pressed");
            tft.setCursor(ROW_X + 6, NOTE_Y + 14);
            tft.print("or a byte received");
            break;
        case ROW_BUFFER:
            if (g_erased) {
                tft.setTextColor(TH.err, TH.bg);
                tft.print("ERASED");
            } else {
                tft.print("what the tester received, kept until erased");
                tft.setCursor(ROW_X + 6, NOTE_Y + 14);
                tft.print("AUTO held: erase it");
            }
            break;
    }
}

static void drawSaveFailed(DSO152TFT& tft) {
    // say so, long enough to be read; the settings still hold until power off
    tft.fillRect(0, NOTE_Y, 320, 44, TH.bg);
    tft.setTextSize(2);
    tft.setTextColor(TH.err, TH.bg);
    tft.setCursor(ROW_X + 6, NOTE_Y + 6);
    tft.print("NOT SAVED TO FLASH");
    delay(1500);
}

static void draw(DSO152TFT& tft) {
    tft.fillScreen(TH.bg);
    tft.fillRect(0, 0, 320, 20, TH.bar);
    tft.setTextSize(1);
    tft.setTextColor(TH.barText, TH.bar);
    tft.setCursor(2, 6);
    tft.print("SETTINGS");

    for (uint8_t r = 0; r < NROW; r++) drawRow(tft, r);
    drawNote(tft);

    tft.drawFastHLine(0, 212, 320, TH.line);
    tft.setTextColor(TH.muted, TH.bg);
    tft.setCursor(8, 222);
    tft.print((char)0x1E); tft.print((char)0x1F);       // up and down arrows
    tft.print(" setting   ");
    tft.print((char)0x11); tft.print((char)0x10);       // left and right arrows
    tft.print(" value   OK / MODE menu");
}

void enter(DSO152TFT& tft, int bufLen) {
    Keys::sync();
    g_len = bufLen;
    g_erased = false;
    draw(tft);
}

void erased(DSO152TFT& tft) {
    g_len = 0;
    g_erased = true;
    drawRow(tft, ROW_BUFFER);
    drawNote(tft);
}

// One step of the chosen setting, up or down, going round at the ends.
static void step(int8_t d) {
    uint8_t* v;
    uint8_t  n;
    switch (g_row) {
        case ROW_THEME:  v = &Theme::idx;      n = Theme::count();   break;
        case ROW_FORMAT: v = &Config::format;  n = Export::NFMT;     break;
        case ROW_BLOCK:  v = &Config::block;   n = Config::NBLOCK;   break;
        case ROW_AUTOOFF: v = &Config::autoOff; n = Config::NAUTOOFF; break;
        default:         return;            // the buffer row has no values
    }
    *v = (uint8_t)((*v + n + d) % n);
}

uint8_t poll(DSO152TFT& tft) {
    uint8_t was = g_row;
    if (Keys::hit(Keys::UP)   && g_row > 0)        g_row--;
    if (Keys::hit(Keys::DOWN) && g_row + 1 < NROW) g_row++;
    if (g_row != was) { g_erased = false; drawRow(tft, was); drawRow(tft, g_row); drawNote(tft); }

    int8_t d = 0;
    if (Keys::hit(Keys::RIGHT)) d = +1;
    if (Keys::hit(Keys::LEFT))  d = -1;
    if (d && g_row != ROW_BUFFER) {
        step(d);
        if (g_row == ROW_THEME) draw(tft);  // every colour on the screen has changed
        else { drawRow(tft, g_row); drawNote(tft); }
    }

    if (Keys::press(Keys::AUTO) == Keys::LONG && g_row == ROW_BUFFER) return ERASE;

    if (Keys::hit(Keys::OK) || Keys::hit(Keys::RUN) || Keys::hit(Keys::MODE)) {
        if (!Config::save()) drawSaveFailed(tft);
        return LEAVE;
    }
    return STAY;
}

}
