#include <Arduino.h>
#include "Health.h"
#include "Theme.h"
#include "Rs232.h"
#include "Frontend.h"

namespace Health {

static Source   g_src = nullptr;
static uint32_t g_hz  = SAMPLE_HZ;

// --- what has been measured -------------------------------------------------
// Times are in sixteenths of a sample, so that edges placed between two
// samples keep their place.
static uint32_t g_blocks, g_edges, g_lastEdgeMs;
static int32_t  g_hiAvg, g_loAvg;           // the two levels, in counts
static bool     g_haveLevels;
static uint32_t g_riseMax, g_fallMax;       // worst 10-90 % times, 1/16 sample
static uint32_t g_dSum, g_kSum;             // gaps between edges, and how many bits they span
static uint16_t g_jitMax;                   // worst edge off its bit, in 1/1000 of a bit
static uint32_t g_lastDrawMs;
static bool     g_dirty;

static const uint32_t STD_BAUD[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400 };

void restart(DSO152TFT& tft) {
    g_blocks = g_edges = 0;
    g_lastEdgeMs = 0;
    g_haveLevels = false;
    g_riseMax = g_fallMax = 0;
    g_dSum = g_kSum = 0;
    g_jitMax = 0;
    g_dirty = true;
    draw(tft);
}

void enter(DSO152TFT& tft, Source src, uint32_t sampleHz) {
    g_src = src;
    g_hz  = sampleHz ? sampleHz : SAMPLE_HZ;
    restart(tft);
}

// The bit time the line is meant to have, in sixteenths of a sample.
static uint32_t nominalBitQ4() {
    uint32_t baud = Rs232::fixedBaud();
    if (!baud) baud = Rs232::stats().baud;
    if (!baud) baud = 9600;
    return (g_hz * 16u) / baud;
}

// Where between samples i-1 and i the level `l` was crossed, in 1/16 sample.
static int32_t cross(const uint16_t* s, uint16_t i, int32_t l) {
    int32_t a = s[i - 1] & 0x0FFF, b = s[i] & 0x0FFF;
    int32_t f = (b != a) ? (l - a) * 16 / (b - a) : 8;
    if (f < 0) f = 0;
    if (f > 16) f = 16;
    return (int32_t)(i - 1) * 16 + f;
}

static void take(const uint16_t* s, uint16_t n) {
    if (!s || n < 16) return;
    g_blocks++;

    uint16_t mn = 0x0FFF, mx = 0;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t v = s[i] & 0x0FFF;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    // Only a block that holds the line's real swing counts. A line at rest
    // carries bumps of a volt or two (a real adapter put 60 counts on range
    // 7), and taken for edges they made levels, times and jitter all wrong.
    // So an RS232 line must go through zero volts, and a TTL one swing at
    // least 2 V.
    int32_t span = mx - mn;
    const int32_t cpv  = (int32_t)Frontend::countsPerVolt();
    const int32_t zero = Rs232::zeroCounts();
    if (Rs232::inverted()) {
        if (!(mn < zero - cpv && mx > zero + cpv)) return;
    } else if (span < 2 * cpv) {
        return;
    }
    if (span < 48) return;

    // the two levels, as the averages of what lies in the outer quarters
    int32_t qHi = mx - span / 4, qLo = mn + span / 4;
    int32_t hiSum = 0, hiN = 0, loSum = 0, loN = 0;
    for (uint16_t i = 0; i < n; i++) {
        int32_t v = s[i] & 0x0FFF;
        if (v >= qHi) { hiSum += v; hiN++; }
        else if (v <= qLo) { loSum += v; loN++; }
    }
    if (!hiN || !loN) return;
    int32_t hi = hiSum / hiN, lo = loSum / loN;
    if (!g_haveLevels) { g_hiAvg = hi; g_loAvg = lo; g_haveLevels = true; }
    else { g_hiAvg = (7 * g_hiAvg + hi) / 8; g_loAvg = (7 * g_loAvg + lo) / 8; }

    // edges at half way, with some hysteresis; and for each its 10-90 % time
    const int32_t sw  = hi - lo;
    const int32_t l50 = lo + sw / 2, l10 = lo + sw / 10, l90 = hi - sw / 10;
    const int32_t hyst = sw / 8;
    const uint16_t reach = 400;             // how far to look for 10 and 90 %, 200 us
    bool high = (s[0] & 0x0FFF) > l50;
    int32_t edge[48];
    uint8_t nEdge = 0;
    for (uint16_t i = 1; i < n; i++) {
        int32_t v = s[i] & 0x0FFF;
        bool flip = high ? (v < l50 - hyst) : (v > l50 + hyst);
        if (!flip) continue;
        high = !high;

        // back to where it went through half way
        uint16_t k = i;
        while (k > 1 && (high ? ((s[k - 1] & 0x0FFF) > l50) : ((s[k - 1] & 0x0FFF) < l50))) k--;
        if (nEdge < 48) edge[nEdge++] = cross(s, k, l50);
        g_edges++;
        g_lastEdgeMs = millis();

        // rise or fall: the last sample on the far side of 10 % (or 90 %)
        // before it, the first past 90 % (or 10 %) after it
        int32_t from = high ? l10 : l90, to = high ? l90 : l10;
        uint16_t a = k, b = i;
        while (a > 1 && (k - a) < reach && (high ? ((s[a - 1] & 0x0FFF) > from) : ((s[a - 1] & 0x0FFF) < from))) a--;
        while (b < n - 1 && (b - i) < reach && (high ? ((s[b] & 0x0FFF) < to) : ((s[b] & 0x0FFF) > to))) b++;
        if (a <= 1 || b >= n - 1) continue;         // runs off the block: not measured
        int32_t t = cross(s, b, to) - cross(s, a, from);
        if (t < 0) t = 0;
        if (high) { if ((uint32_t)t > g_riseMax) g_riseMax = (uint32_t)t; }
        else      { if ((uint32_t)t > g_fallMax) g_fallMax = (uint32_t)t; }
    }

    // the bit time from the gaps between edges, each a whole number of bits
    uint32_t tNom = nominalBitQ4();
    if (!tNom) return;
    for (uint8_t j = 1; j < nEdge; j++) {
        uint32_t d = (uint32_t)(edge[j] - edge[j - 1]);
        uint32_t kb = (d + tNom / 2) / tNom;
        if (kb < 1 || kb > 10) continue;
        g_dSum += d; g_kSum += kb;
    }
    if (g_dSum > 0x40000000u) { g_dSum >>= 1; g_kSum >>= 1; }

    // jitter: how far each edge is from a whole number of bits after the first
    uint32_t tBit = g_kSum ? g_dSum / g_kSum : tNom;
    for (uint8_t j = 1; j < nEdge; j++) {
        int32_t d   = edge[j] - edge[0];
        int32_t nb  = (d + (int32_t)tBit / 2) / (int32_t)tBit;
        int32_t off = d - nb * (int32_t)tBit;
        if (off < 0) off = -off;
        uint16_t pm = (uint16_t)((uint32_t)off * 1000 / tBit);
        if (pm > g_jitMax) g_jitMax = pm;
    }
    g_dirty = true;
}

void poll(DSO152TFT& tft) {
    if (!g_src) return;
    uint32_t t0 = millis();
    while ((millis() - t0) < 40) {
        uint16_t n;
        const uint16_t* s = g_src(n);
        if (!s) break;
        take(s, n);
    }
    if (millis() - g_lastDrawMs >= 500) {
        bool active = g_edges && (millis() - g_lastEdgeMs) < 1000;
        static bool wasActive = false;
        if (g_dirty || active != wasActive) draw(tft);
        wasActive = active;
    }
}

// --- the table --------------------------------------------------------------
#define ROW_Y    46
#define ROW_H    22
#define VAL_X    92
#define LIM_X    192
#define STAT_X   276

// WEAK is taken: the core defines it as the weak attribute
enum { ST_NONE, ST_GOOD, ST_WEAK, ST_BAD };

// A value against a range: good inside it with a tenth of its width to spare
// at both ends, weak in that last tenth, bad outside.
static uint8_t inRange(int32_t v, int32_t a, int32_t b) {
    int32_t m = (b - a) / 10;
    if (v >= a + m && v <= b - m) return ST_GOOD;
    if (v >= a && v <= b) return ST_WEAK;
    return ST_BAD;
}
// ... and against a ceiling: weak in the last tenth below it.
static uint8_t below(int32_t v, int32_t lim) {
    if (v * 10 <= lim * 9) return ST_GOOD;
    return v <= lim ? ST_WEAK : ST_BAD;
}

static void rowStart(DSO152TFT& tft, uint8_t r, const char* name) {
    int16_t y = (int16_t)(ROW_Y + r * ROW_H);
    tft.fillRect(0, y - 2, 320, ROW_H, (r & 1) ? TH.bg : TH.line);
    tft.setTextSize(1);
    tft.setTextColor(TH.text2, (r & 1) ? TH.bg : TH.line);
    tft.setCursor(4, y + 4);
    tft.print(name);
    tft.setTextSize(2);
    tft.setTextColor(TH.text, (r & 1) ? TH.bg : TH.line);
    tft.setCursor(VAL_X, y);
}
static void rowLimit(DSO152TFT& tft, uint8_t r) {
    int16_t y = (int16_t)(ROW_Y + r * ROW_H);
    tft.setTextSize(1);
    tft.setTextColor(TH.muted, (r & 1) ? TH.bg : TH.line);
    tft.setCursor(LIM_X, y + 4);
}
static void rowStatus(DSO152TFT& tft, uint8_t r, uint8_t st) {
    int16_t y = (int16_t)(ROW_Y + r * ROW_H);
    uint16_t bg = (r & 1) ? TH.bg : TH.line;
    tft.setTextSize(2);
    tft.setCursor(STAT_X, y);
    if (st == ST_GOOD)      { tft.setTextColor(TH.ok, bg);    tft.print("OK"); }
    else if (st == ST_WEAK) { tft.setTextColor(TH.trace, bg); tft.print("WEAK"); }
    else if (st == ST_BAD)  { tft.setTextColor(TH.err, bg);   tft.print("BAD"); }
    else                 { tft.setTextColor(TH.muted, bg); tft.print("--"); }
}

// tenths of a percent, as "12.3%"
static void printPermille(DSO152TFT& tft, uint32_t pm) {
    tft.print(pm / 10); tft.print('.'); tft.print(pm % 10); tft.print('%');
}

void draw(DSO152TFT& tft) {
    g_lastDrawMs = millis();
    g_dirty = false;
    const bool rs232 = Rs232::inverted();
    const int32_t zero = Rs232::zeroCounts();

    tft.fillRect(0, 20, 320, 220, TH.bg);
    tft.setTextSize(1);
    tft.setTextColor(TH.text, TH.bg);
    tft.setCursor(4, 26);
    tft.print("BUS HEALTH  ");
    tft.print(rs232 ? "EIA-232" : "TTL levels");
    tft.setTextColor(TH.muted, TH.bg);
    tft.setCursor(200, 26);
    tft.print(g_hz / 1000000); tft.print('.'); tft.print((g_hz / 10000) % 100 / 10);
    tft.print((g_hz / 10000) % 10); tft.print(" MS/s");

    // activity
    bool active = g_edges && (millis() - g_lastEdgeMs) < 1000;
    rowStart(tft, 0, "Activity");
    tft.print(active ? "yes" : (g_edges ? "was" : "none"));
    rowLimit(tft, 0); tft.print(g_edges); tft.print(" edges");
    rowStatus(tft, 0, active ? ST_GOOD : (g_edges ? ST_NONE : ST_BAD));

    // levels
    int32_t hiMv = g_haveLevels ? Frontend::countsToMilliVolts(g_hiAvg - zero) : 0;
    int32_t loMv = g_haveLevels ? Frontend::countsToMilliVolts(g_loAvg - zero) : 0;
    int32_t hA = rs232 ? 3000 : 2000,   hB = rs232 ? 15000 : 5500;
    int32_t lA = rs232 ? -15000 : -500, lB = rs232 ? -3000 : 800;

    rowStart(tft, 1, "V high");
    if (g_haveLevels) Frontend::printVolts(tft, hiMv, 2, true); else tft.print("--");
    rowLimit(tft, 1);
    Frontend::printVolts(tft, hA, rs232 ? 0 : 1, false); tft.print(".."); Frontend::printVolts(tft, hB, rs232 ? 0 : 1, false);
    rowStatus(tft, 1, g_haveLevels ? inRange(hiMv, hA, hB) : ST_NONE);

    rowStart(tft, 2, "V low");
    if (g_haveLevels) Frontend::printVolts(tft, loMv, 2, true); else tft.print("--");
    rowLimit(tft, 2);
    Frontend::printVolts(tft, lA, rs232 ? 0 : 1, false); tft.print(".."); Frontend::printVolts(tft, lB, rs232 ? 0 : 1, false);
    rowStatus(tft, 2, g_haveLevels ? inRange(loMv, lA, lB) : ST_NONE);

    // the bit rate, against the standard one nearest it
    rowStart(tft, 3, "Baud");
    uint32_t tBit = g_kSum ? g_dSum / g_kSum : 0;
    uint8_t baudSt = ST_NONE;
    if (tBit) {
        uint32_t baud = (g_hz * 16u) / tBit;
        uint32_t std = STD_BAUD[0];
        for (uint8_t i = 0; i < sizeof(STD_BAUD) / sizeof(STD_BAUD[0]); i++) {
            uint32_t a = baud > STD_BAUD[i] ? baud - STD_BAUD[i] : STD_BAUD[i] - baud;
            uint32_t b = baud > std ? baud - std : std - baud;
            if (a < b) std = STD_BAUD[i];
        }
        int32_t devPm = (int32_t)(((int64_t)baud - std) * 1000 / (int64_t)std);
        tft.print(baud);
        rowLimit(tft, 3);
        if (devPm >= 0) tft.print('+'); else tft.print('-');
        printPermille(tft, (uint32_t)(devPm < 0 ? -devPm : devPm));
        tft.print(' '); tft.print(std);
        baudSt = below(devPm < 0 ? -devPm : devPm, 30);         // +-3 %
    } else {
        tft.print("--");
        rowLimit(tft, 3); tft.print("max 3%");
    }
    rowStatus(tft, 3, baudSt);

    // rise and fall, in microseconds and as a share of a bit
    uint32_t bitQ4 = tBit ? tBit : nominalBitQ4();
    const char* names[2] = { "Rise", "Fall" };
    uint32_t times[2] = { g_riseMax, g_fallMax };
    for (uint8_t k = 0; k < 2; k++) {
        rowStart(tft, (uint8_t)(4 + k), names[k]);
        uint8_t st = ST_NONE;
        if (times[k]) {
            uint32_t tenthsUs = (uint32_t)times[k] * 625u / (g_hz / 1000u);
            tft.print(tenthsUs / 10); tft.print('.'); tft.print(tenthsUs % 10); tft.print("us");
            uint32_t pm = bitQ4 ? times[k] * 1000 / bitQ4 : 0;
            rowLimit(tft, (uint8_t)(4 + k)); printPermille(tft, pm); tft.print(" max 40%");
            st = below((int32_t)pm, 400);
        } else {
            tft.print("--");
            rowLimit(tft, (uint8_t)(4 + k)); tft.print("max 40%");
        }
        rowStatus(tft, (uint8_t)(4 + k), st);
    }

    // jitter
    rowStart(tft, 6, "Jitter");
    if (g_kSum) { printPermille(tft, g_jitMax); } else tft.print("--");
    rowLimit(tft, 6); tft.print("max 10%");
    rowStatus(tft, 6, g_kSum ? below(g_jitMax, 100) : ST_NONE);

    tft.setTextSize(1);
    tft.setTextColor(TH.muted, TH.bg);
    tft.setCursor(4, 230);
    tft.print("RUN restart   MODE view   probe x1");
}

}
