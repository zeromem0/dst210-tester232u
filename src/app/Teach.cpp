#include <Arduino.h>
#include "Teach.h"
#include "Theme.h"
#include "Rs232.h"
#include "Frontend.h"

namespace Teach {

// the plot
#define PX0   50
#define PX1   290             // the volts of the band are named at the right
#define PY0   34
#define PY1   136
#define NAME_Y   142
#define VALUE_Y  154

static int16_t g_vmin, g_vmax, g_w0, g_w1;

// where the character was last found, for a frozen screen (see Teach.h)
static int32_t  g_lastStart = -1;
static uint32_t g_lastBitQ8 = 0;
static uint8_t  g_sel = 0;      // which character, on a still picture

void select(uint8_t i) { g_sel = i; }
bool step(int8_t d) {
    uint8_t n = Rs232::byteCount();
    int16_t i = (int16_t)(g_sel + d);
    if (i < 0 || i >= n) return false;
    g_sel = (uint8_t)i;
    return true;
}

// The labels of the level lines go at the left of the plot, and two levels
// close together would print one name over the other; each label is moved
// down out of the way of those already placed.
static int16_t g_labelY[2][4];  // left and right
static uint8_t g_labels[2];

static int16_t yOf(int32_t v) {
    if (v < g_vmin) v = g_vmin;
    if (v > g_vmax) v = g_vmax;
    return (int16_t)(PY1 - (v - g_vmin) * (PY1 - PY0) / (g_vmax - g_vmin));
}
static int16_t xOf(int32_t i) {
    return (int16_t)(PX0 + (i - g_w0) * (PX1 - PX0) / (g_w1 - g_w0));
}

// A level drawn across the plot as a dotted line, named at the left.
static void level(DSO152TFT& tft, int32_t v, uint16_t col, const char* name, uint8_t side = 0) {
    int16_t y = yOf(v);
    for (int16_t x = PX0; x < PX1; x += 4) tft.drawFastHLine(x, y, 2, col);

    int16_t ly = (int16_t)(y - 3);
    for (bool moved = true; moved; ) {
        moved = false;
        for (uint8_t i = 0; i < g_labels[side]; i++) {
            int16_t o = g_labelY[side][i];
            if (ly > o - 9 && ly < o + 9) { ly = (int16_t)(o + 9); moved = true; }
        }
    }
    if (g_labels[side] < 4) g_labelY[side][g_labels[side]++] = ly;
    tft.setTextSize(1);
    tft.setTextColor(col, TH.bg);
    tft.setCursor(side ? PX1 + 4 : 2, ly);
    tft.print(name);
}

// A level as volts, with its sign, from its distance to zero volts.
static void printVolts(DSO152TFT& tft, int32_t counts) {
    Frontend::printVolts(tft, Frontend::countsToMilliVolts(counts - (int32_t)Rs232::zeroCounts()), 1, true);
}

static void hint(DSO152TFT& tft) {
    tft.setTextSize(1);
    tft.setTextColor(TH.muted, TH.bg);
    tft.setCursor(4, 230);
    tft.print((char)0x1E); tft.print((char)0x1F);       // up and down arrows
    tft.print(" threshold  "); tft.print((char)0x11); tft.print((char)0x10);
    tft.print(" character  AUTO held: auto");
}

static void printByte(DSO152TFT& tft, uint8_t b) {
    if (b == '\r')      tft.print("CR");
    else if (b == '\n') tft.print("LF");
    else if (b >= 32 && b < 127) { tft.print('\''); tft.print((char)b); tft.print('\''); }
    else                tft.print("--");
}

void draw(DSO152TFT& tft, const uint16_t* s, uint16_t n, bool frozen) {
    tft.fillRect(0, 20, 320, 220, TH.bg);
    g_labels[0] = g_labels[1] = 0;
    if (frozen && Rs232::byteCount() > 1) {     // which of the characters this is
        tft.setTextSize(1);
        tft.setTextColor(TH.muted, TH.bg);
        tft.setCursor(PX1 - 30, 23);
        tft.print(g_sel + 1); tft.print('/'); tft.print(Rs232::byteCount());
    }

    Rs232::Stats  st = Rs232::stats();
    Rs232::Format f  = Rs232::format();

    // the character: the one the decoder found, or on a frozen screen the
    // one it found before the threshold was moved
    bool    found = Rs232::byteCount() > 0 && st.bitSamples >= 256;
    int32_t start;
    uint32_t bitQ8;
    if (!frozen) g_sel = 0;
    if (found && g_sel >= Rs232::byteCount()) g_sel = (uint8_t)(Rs232::byteCount() - 1);
    if (found) {
        start = Rs232::byteAt(g_sel); bitQ8 = st.bitSamples;
        g_lastStart = start; g_lastBitQ8 = bitQ8;
    } else if (frozen && g_lastStart >= 0 && g_lastStart < n) {
        start = g_lastStart; bitQ8 = g_lastBitQ8;
    } else {
        g_lastStart = -1;
        start = -1; bitQ8 = 0;
    }

    if (!s || n == 0 || start < 0) {
        tft.setTextSize(2);
        tft.setTextColor(TH.text2, TH.bg);
        tft.setCursor(16, 80);
        tft.print("no character yet");
        tft.setTextSize(1);
        tft.setCursor(16, 108);
        tft.print("RUN twice for a capture (SNAP), and in WAIT");
        tft.setCursor(16, 120);
        tft.print("MODE held sends a made-up message.");
        hint(tft);
        return;
    }

    // the frame of the first character, from one bit of rest before its
    // start bit to one bit of rest after its stop bits
    const uint8_t  nPar  = (f.parity == 'N') ? 0 : 1;
    const uint8_t  nBits = (uint8_t)(1 + f.dataBits + nPar + f.stopBits);
    int32_t w0 = start - (int32_t)(bitQ8 >> 8);
    int32_t w1 = start + (int32_t)(((uint32_t)(nBits + 1) * bitQ8) >> 8);
    if (w0 < 0) w0 = 0;
    if (w1 > n) w1 = n;
    if (w1 - w0 < 4) return;
    g_w0 = (int16_t)w0; g_w1 = (int16_t)w1;

    // Mark is the line at rest and a 1. In RS232 mode it is the negative
    // level, which comes out as the lower count; in TTL mode the higher.
    const bool     inv   = Rs232::inverted();
    const uint16_t mark  = inv ? st.lo : st.hi;
    const uint16_t space = inv ? st.hi : st.lo;
    const uint16_t th    = st.threshold;
    // In RS232 mode the plot always takes in the band of +-3 V, which the
    // standard leaves undecided: a receiver need only tell apart what is
    // beyond it on either side.
    const int32_t zero = Rs232::zeroCounts();
    const int32_t v3   = 3 * (int32_t)Frontend::countsPerVolt();
    int32_t lo = st.lo < th ? st.lo : th, hi = st.hi > th ? st.hi : th;
    if (inv) {
        if (lo > zero - v3) lo = zero - v3;
        if (hi < zero + v3) hi = zero + v3;
    }
    int32_t pad = (hi - lo) / 6 + 8;
    g_vmin = (int16_t)(lo - pad); g_vmax = (int16_t)(hi + pad);

    // a light band for every other bit, so the cells can be told apart
    for (uint8_t k = 0; k < nBits; k++) {
        int16_t xa = xOf(start + (int32_t)(((uint32_t)k * bitQ8) >> 8));
        int16_t xb = xOf(start + (int32_t)(((uint32_t)(k + 1) * bitQ8) >> 8));
        if (k & 1) tft.fillRect(xa, PY0, (int16_t)(xb - xa), PY1 - PY0 + 1, TH.line);
        tft.drawFastVLine(xa, PY0, PY1 - PY0 + 1, TH.muted);
        if (k == nBits - 1) tft.drawFastVLine(xb, PY0, PY1 - PY0 + 1, TH.muted);
    }

    // the undecided band across the cells, in a colour of its own
    if (inv) {
        int16_t yTop = yOf(zero + v3), yBot = yOf(zero - v3);
        tft.fillRect(PX0, yTop, PX1 - PX0, (int16_t)(yBot - yTop + 1), TH.bar);
    }

    level(tft, mark,  TH.ok,  inv ? "1 mark-" : "1 mark");
    level(tft, space, TH.ok2, inv ? "0 space+" : "0 space");
    level(tft, th, TH.err, Rs232::manualThreshold() ? "th hand" : "th auto");
    if (inv) {
        level(tft, zero + v3, TH.muted, "+3V", 1);
        level(tft, zero,      TH.muted, "0V",  1);
        level(tft, zero - v3, TH.muted, "-3V", 1);
    }

    // the trace, a column at a time, its lowest to its highest sample
    int16_t prev = -1;
    for (int16_t x = PX0; x < PX1; x++) {
        int32_t i0 = w0 + (int32_t)(x - PX0) * (w1 - w0) / (PX1 - PX0);
        int32_t i1 = w0 + (int32_t)(x + 1 - PX0) * (w1 - w0) / (PX1 - PX0);
        if (i1 <= i0) i1 = i0 + 1;
        uint16_t cmin = 0x0FFF, cmax = 0;
        for (int32_t i = i0; i < i1 && i < n; i++) {
            uint16_t v = s[i] & 0x0FFF;
            if (v < cmin) cmin = v;
            if (v > cmax) cmax = v;
        }
        int16_t yt = yOf(cmax), yb = yOf(cmin);
        if (prev >= 0) { if (prev < yt) yt = prev; if (prev > yb) yb = prev; }
        tft.drawFastVLine(x, yt, (int16_t)(yb - yt + 1), TH.trace);
        prev = yOf(s[i1 - 1 < n ? i1 - 1 : n - 1] & 0x0FFF);
    }

    // each bit: its name, the point where it was read, and what it was read as
    uint8_t value = 0;
    char order[16];
    uint8_t nOrder = 0;
    tft.setTextSize(1);
    for (uint8_t k = 0; k < nBits; k++) {
        int32_t at = start + (int32_t)((((uint32_t)k * 2 + 1) * bitQ8) >> 9);   // the middle of the bit
        uint16_t v = (at < n) ? (s[at] & 0x0FFF) : mark;
        bool one = inv ? (v < th) : (v > th);

        int16_t xa = xOf(start + (int32_t)(((uint32_t)k * bitQ8) >> 8));
        int16_t xb = xOf(start + (int32_t)(((uint32_t)(k + 1) * bitQ8) >> 8));
        int16_t xm = (int16_t)((xa + xb) / 2);
        tft.fillRect((int16_t)(xm - 2), (int16_t)(yOf(v) - 2), 5, 5, TH.mark);

        // Start and Stop are wider than a cell: Start leans left, into the
        // bit of rest before it, Stop is centred, clear of bit 7's name.
        char d[2] = { (char)('0' + k - 1), 0 };
        const char* name; int16_t nx;
        if (k == 0)                           { name = "Start"; nx = (int16_t)(xm - 24); }
        else if (k <= f.dataBits)             { name = d;       nx = (int16_t)(xm - 3); }
        else if (nPar && k == f.dataBits + 1) { name = "Pa";    nx = (int16_t)(xm - 6); }
        else                                  { name = "Stop";  nx = (int16_t)(xm - 12); }
        tft.setTextColor(TH.text2, TH.bg);
        tft.setCursor(nx, NAME_Y);
        tft.print(name);

        tft.setTextSize(2);
        tft.setTextColor(one ? TH.ok : TH.text, TH.bg);
        tft.setCursor((int16_t)(xm - 6), VALUE_Y);
        tft.print(one ? '1' : '0');
        tft.setTextSize(1);

        if (k >= 1 && k <= f.dataBits) {
            if (one) value |= (uint8_t)(1u << (k - 1));
            order[nOrder++] = one ? '1' : '0';
        }
    }
    order[nOrder] = 0;

    // what the levels are, and what the bits come to
    tft.setTextSize(1);
    tft.setTextColor(TH.text2, TH.bg);
    tft.setCursor(4, 176);
    tft.print("mark "); printVolts(tft, mark);
    tft.print("  space "); printVolts(tft, space);
    tft.print("  th "); printVolts(tft, th);
    tft.print(Rs232::manualThreshold() ? " by hand" : " middle");

    tft.setCursor(4, 190);
    tft.print("bits as sent, lowest first: ");
    tft.setTextColor(TH.text, TH.bg);
    tft.print(order);

    tft.setTextSize(2);
    tft.setTextColor(TH.text, TH.bg);
    tft.setCursor(4, 204);
    if (!found) {
        // the bits can still be read off the picture, but the decoder no
        // longer finds a character in them
        tft.setTextColor(TH.err, TH.bg);
        tft.print("no start bit seen");
        hint(tft);
        return;
    }
    tft.print("0x");
    if (value < 0x10) tft.print('0');
    tft.print(value, HEX);
    tft.print(' ');
    for (int8_t b = 7; b >= 0; b--) tft.print((value >> b) & 1);
    tft.print(' ');
    printByte(tft, value);

    hint(tft);
}

}
