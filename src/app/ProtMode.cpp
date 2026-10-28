#include <Arduino.h>
#include "ProtMode.h"
#include "Theme.h"
#include "Rs232.h"
#include "Frontend.h"

namespace Prot {

static const uint16_t IDLE_SPAN = 768;

void drawChrome(DSO152TFT& tft) {
    tft.fillRect(0, PROT_WAVE_Y - 2, 320, 240 - PROT_WAVE_Y + 2, TH.bg);
    tft.drawFastHLine(0, PROT_WAVE_Y - 2,           320, TH.line);
    tft.drawFastHLine(0, PROT_WAVE_Y + PROT_WAVE_H, 320, TH.line);
    tft.drawFastHLine(0, PROT_LEVEL_Y - 3,          320, TH.line);
}

void drawWave(DSO152TFT& tft, const uint16_t* s, uint16_t n) {
    if (!s || n == 0) return;
    Rs232::Stats st = Rs232::stats();

    // The band is never cleared as a whole: each column is repainted in one
    // pass, black above the trace, the trace, black below. Clearing first
    // left every column black for most of a frame, which showed as flicker.
    const int16_t top = PROT_WAVE_Y;
    const int16_t bot = PROT_WAVE_Y + PROT_WAVE_H - 1;

    uint16_t lo = st.lo, hi = st.hi;
    if (hi <= lo) hi = (uint16_t)(lo + 1);

    // Until a signal has been seen, lo and hi are only the extremes of the
    // last block, which with nothing on the probe is the noise: scaled to
    // fill the band it looked like a signal. So the band then shows a fixed
    // stretch of the scale, about what an RS232 swing takes on range 7, and
    // the noise is the thin line it is.
    if (!st.levels && (uint16_t)(hi - lo) < IDLE_SPAN) {
        uint16_t mid = (uint16_t)((lo + hi) >> 1);
        lo = mid > IDLE_SPAN / 2 ? (uint16_t)(mid - IDLE_SPAN / 2) : 0;
        hi = (uint16_t)(lo + IDLE_SPAN);
    }
    uint32_t span = (uint32_t)(hi - lo);

    // the decision level, so it is plain where the bits are being split
    int32_t tv = (int32_t)st.threshold - (int32_t)lo;
    if (tv < 0) tv = 0;
    if ((uint32_t)tv > span) tv = (int32_t)span;
    int16_t ty = (int16_t)(PROT_WAVE_Y + PROT_WAVE_H - 1
               - (tv * (PROT_WAVE_H - 1) / (int32_t)span));

    // One column covers several samples, so take the span of them rather
    // than one of them. Picking a single sample aliases badly on a line
    // this dense: a square wave comes out as a comb of arbitrary heights.
    int16_t prevY = -1;
    for (int16_t x = 0; x < 320; x++) {
        uint32_t i0 = (uint32_t)x * n / 320u;
        uint32_t i1 = (uint32_t)(x + 1) * n / 320u;
        if (i1 <= i0) i1 = i0 + 1;
        if (i1 > n) i1 = n;

        uint16_t cmin = 0x0FFF, cmax = 0;
        for (uint32_t i = i0; i < i1; i++) {
            uint16_t v = s[i] & 0x0FFF;
            if (v < cmin) cmin = v;
            if (v > cmax) cmax = v;
        }

        int32_t a = (int32_t)cmin - (int32_t)lo;
        int32_t b = (int32_t)cmax - (int32_t)lo;
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if ((uint32_t)a > span) a = (int32_t)span;
        if ((uint32_t)b > span) b = (int32_t)span;

        int16_t yb = (int16_t)(PROT_WAVE_Y + PROT_WAVE_H - 1
                   - (a * (PROT_WAVE_H - 1) / (int32_t)span));
        int16_t yt = (int16_t)(PROT_WAVE_Y + PROT_WAVE_H - 1
                   - (b * (PROT_WAVE_H - 1) / (int32_t)span));

        // carry the trace over from the column before, so edges stay joined
        if (prevY >= 0) {
            if (prevY < yt) yt = prevY;
            if (prevY > yb) yb = prevY;
        }
        if (yt > top) tft.drawFastVLine(x, top, (int16_t)(yt - top), TH.bg);
        tft.drawFastVLine(x, yt, (int16_t)(yb - yt + 1), TH.trace);
        if (yb < bot) tft.drawFastVLine(x, (int16_t)(yb + 1), (int16_t)(bot - yb), TH.bg);
        if (ty < yt || ty > yb) tft.drawPixel(x, ty, TH.bar);
        prevY = (int16_t)(PROT_WAVE_Y + PROT_WAVE_H - 1
              - (((int32_t)(s[i1 - 1] & 0x0FFF) - (int32_t)lo) * (PROT_WAVE_H - 1)
                 / (int32_t)span));
        if (prevY < PROT_WAVE_Y) prevY = PROT_WAVE_Y;
        if (prevY > PROT_WAVE_Y + PROT_WAVE_H - 1) prevY = PROT_WAVE_Y + PROT_WAVE_H - 1;
    }

    // a tick where each bit was actually read
    uint8_t m = Rs232::markCount();
    for (uint8_t k = 0; k < m; k++) {
        int16_t x = (int16_t)((uint32_t)Rs232::markAt(k) * 320u / n);
        if (x > 319) x = 319;
        tft.drawFastVLine(x, PROT_WAVE_Y + PROT_WAVE_H - 4, 4, TH.mark);
    }
}

void drawBus(DSO152TFT& tft, uint16_t n) {
    tft.fillRect(0, PROT_BUS_Y, 320, PROT_BUS_H, TH.bg);
    if (n == 0) return;

    tft.drawFastHLine(0, PROT_BUS_Y + PROT_BUS_H / 2, 320, TH.bar);
    tft.setTextSize(1);

    uint8_t cnt = Rs232::byteCount();
    int16_t lastRight = -8;

    for (uint8_t k = 0; k < cnt; k++) {
        int16_t x = (int16_t)((uint32_t)Rs232::byteAt(k) * 320u / n);
        uint8_t b = Rs232::byteVal(k);

        // A box 20 wide holds one glyph with room either side. Where two
        // characters would land on the same pixels the later one is pushed
        // along rather than drawn over the top of the earlier.
        if (x < lastRight + 2) x = (int16_t)(lastRight + 2);
        if (x > 300) break;

        tft.fillRect(x, PROT_BUS_Y + 2, 20, PROT_BUS_H - 4, TH.bar);
        tft.drawRect(x, PROT_BUS_Y + 2, 20, PROT_BUS_H - 4, TH.ok2);

        tft.setCursor(x + 3, PROT_BUS_Y + 7);
        if (b >= 32 && b < 127) {
            tft.setTextColor(TH.barText, TH.bar);
            tft.print(' ');
            tft.print((char)b);
        } else {
            tft.setTextColor(TH.barHi, TH.bar);
            if (b < 0x10) tft.print('0');
            tft.print(b, HEX);
        }
        lastRight = (int16_t)(x + 20);
    }
}

void drawStream(DSO152TFT& tft, const char* buf, int len) {
    const int perLine = 8, lines = PROT_STREAM_H / 12;
    int shown = perLine * lines;
    int start = len > shown ? len - shown : 0;

    tft.fillRect(0, PROT_STREAM_Y, 320, PROT_STREAM_H, TH.bg);
    tft.setTextSize(1);

    for (int ln = 0; ln < lines; ln++) {
        int base = start + ln * perLine;
        if (base >= len) break;
        int y = PROT_STREAM_Y + ln * 12;

        tft.setCursor(0, y);
        tft.setTextColor(TH.text2, TH.bg);
        for (int i = 0; i < perLine; i++) {
            if (base + i >= len) { tft.print("   "); continue; }
            uint8_t b = (uint8_t)buf[base + i];
            if (b < 0x10) tft.print('0');
            tft.print(b, HEX);
            tft.print(' ');
        }
        tft.setCursor(200, y);
        for (int i = 0; i < perLine; i++) {
            if (base + i >= len) break;
            uint8_t b = (uint8_t)buf[base + i];
            if (b == '\r')      { tft.setTextColor(TH.ok, TH.bg); tft.print('D'); }
            else if (b == '\n') { tft.setTextColor(TH.ok, TH.bg); tft.print('A'); }
            else {
                tft.setTextColor(TH.text, TH.bg);
                tft.print((char)((b >= 32 && b < 127) ? b : '.'));
            }
        }
    }
}

// Blanks from the cursor to x. The level lines are written over themselves
// with the background colour set, and padded out with this, instead of the
// panel being cleared first: clearing made the text blink at every refresh.
static void padTo(DSO152TFT& tft, int16_t x) {
    while (tft.getCursorX() + 6 <= x) tft.print(' ');
}

void drawLevels(DSO152TFT& tft, uint32_t sampleHz, const char* buf, int len) {
    Rs232::Stats st = Rs232::stats();
    tft.setTextSize(1);
    tft.setTextColor(TH.text2, TH.bg);

    tft.setCursor(0, PROT_LEVEL_Y);
    tft.print("lo ");  tft.print(st.lo);
    tft.print(" hi "); tft.print(st.hi);
    tft.print(" th "); tft.print(st.threshold);
    padTo(tft, 170);

    // A swing is a difference, so the front end's unknown zero cancels out of
    // it and the number stands before any calibration. An absolute level
    // would not, which is why none is shown.
    tft.setCursor(170, PROT_LEVEL_Y);
    tft.print("Vpp ");
    Frontend::printVolts(tft, Frontend::countsToMilliVolts((int32_t)st.hi - (int32_t)st.lo), 2, false);
    padTo(tft, 320);

    tft.setCursor(0, PROT_LEVEL_Y + 12);
    tft.print(sampleHz / 1000); tft.print("kS/s ");
    tft.print(st.bitSamples >> 8); tft.print("spb");
    padTo(tft, 110);

    Rs232::Format f = Rs232::format();
    tft.setCursor(110, PROT_LEVEL_Y + 12);
    tft.print(f.dataBits); tft.print(f.parity); tft.print(f.stopBits);
    tft.print(f.decided ? " set" : " ...");
    padTo(tft, 190);

    // The counts in short (F framing, P parity, O overrun, D dropped), and
    // cut at the edge rather than wrapped: written out in full, with counts
    // of two and three digits, they ran past the right edge and the core's
    // text wrap put the rest at the start of the next line, over "Bin"
    // (seen on the DST-210 at 115200, 2026-10-26).
    tft.setCursor(190, PROT_LEVEL_Y + 12);
    tft.setTextWrap(false);
    if (st.framingErrors || st.overruns || st.parityErrors || st.dropped) {
        tft.setTextColor(TH.err, TH.bg);
        if (st.framingErrors) { tft.print('F'); tft.print(st.framingErrors); tft.print(' '); }
        if (st.parityErrors)  { tft.print('P'); tft.print(st.parityErrors); tft.print(' '); }
        if (st.overruns)      { tft.print('O'); tft.print(st.overruns); tft.print(' '); }
        if (st.dropped)       { tft.print('D'); tft.print(st.dropped); }
        tft.setTextColor(TH.text2, TH.bg);
    }
    padTo(tft, 320);
    tft.setTextWrap(true);

    // the byte that arrived last, spelled out
    tft.setCursor(0, PROT_LEVEL_Y + 25);
    if (len > 0) {
        uint8_t b = (uint8_t)buf[len - 1];
        tft.print("Bin ");
        for (int8_t i = 7; i >= 0; i--) {
            tft.print((b >> i) & 1);
            if (i == 4) tft.print(' ');
        }
        padTo(tft, 130);
        tft.setCursor(130, PROT_LEVEL_Y + 25);
        tft.print("Hex ");
        if (b < 0x10) tft.print('0');
        tft.print(b, HEX);
        padTo(tft, 200);
        tft.setCursor(200, PROT_LEVEL_Y + 25);
        tft.print("Chr ");
        tft.print((char)((b >= 32 && b < 127) ? b : '.'));
    }
    padTo(tft, 320);
}

}
