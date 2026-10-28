// DST-210 as an RS232 protocol inspector on the analogue input: T232USB
// (dso152-tester232-usb) carried over from the DSO-152.
//
// Nothing arrives over a USART here. The BNC goes through the scope's own
// front end into the 8-bit converter, TMR4 paces it, DMA1 fills a circular
// buffer, and the byte stream is recovered from those samples. The front end
// takes RS232 swings directly, so no MAX3232 sits in front of it.
//
// It lives in flash after the FNIRSI application and is started instead of
// it when RUN is held at power-on (src/boot/BootStub.S). Test builds run from
// RAM, loaded over SWD (RAM_RUN); those tools are not published.
//
// The instrument starts on a menu (Menu.cpp): this inspector; the USB Sender
// (TtlApp.cpp), which sends what the inspector received to a PC or a phone
// over the instrument's own USB socket, a CDC serial port (UsbFix.cpp); and
// Settings (SetApp.cpp): the theme, the export format, auto off and erasing
// the buffer, kept in RAM until power off (Config.cpp). OK held brings any of
// them back to the menu. What was received stays in the buffer until it is
// erased.

#include <Arduino.h>
#include "dso152mod.h"
#include "Theme.h"
#include "DisplayHex.h"
#include "DisplayChars.h"
#include "DisplayLarge.h"
#include "Acquire.h"
#include "Frontend.h"
#include "Rs232.h"
#include "ProtMode.h"
#include "Keys.h"
#include "Menu.h"
#include "TtlApp.h"
#include "SetApp.h"
#include "Config.h"
#include "UsbFix.h"
#include "Pins.h"
#include "Teach.h"
#include "Health.h"
#include "Probe.h"
#include "Version.h"

DSO152TFT tft;

#define APP_MENU    0
#define APP_TESTER  1
#define APP_TTL     3
#define APP_SET     4
static uint8_t app = APP_MENU;

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define MODE_PROT   0
#define MODE_HEX    1
#define MODE_CHARS  2
#define MODE_LARGE  3
#define MODE_TEACH  4       // one character, drawn to be learnt from (Teach.cpp)
#define MODE_HEALTH 5       // the electrical side of the line, as the Fluke 125 tests it (Health.cpp)
#define NUM_MODES   6

// The order MODE goes through them in, which is not the order of the numbers:
// the two views drawn from the waveform first, then the text ones.
static const uint8_t MODE_ORDER[NUM_MODES] = { MODE_PROT, MODE_TEACH, MODE_HEALTH, MODE_LARGE, MODE_HEX, MODE_CHARS };
static uint8_t nextMode(uint8_t m) {
    for (uint8_t i = 0; i < NUM_MODES; i++)
        if (MODE_ORDER[i] == m) return MODE_ORDER[(i + 1) % NUM_MODES];
    return MODE_PROT;
}

#define BUFFER_SIZE 2048
static char dataBuffer[BUFFER_SIZE];
static int  bufferIndex = 0;
static int  lastBufferIndex = 0;

static uint8_t  displayMode = MODE_PROT;
static volatile bool running = true;
static int      lastDisplayedLines = 0;
static int      scrollFirstLine = -1;

// Pick the rate for the line speed: about ten samples a bit is enough to
// decode and leaves the most of the traffic on screen. 100 kS/s suits 9600,
// 250 k suits 19200 and 38400, 1.25 M suits 115200. Starting at the slowest
// because 9600 is what turns up most often.
static const uint32_t RATES[] = { 100000, 250000, 500000, 1250000, 2000000 };
static uint8_t rateIdx = 0;

// AUTO steps through these. The two fixed ones are 8N1 and bring their own
// sample rate, a little over ten samples a bit; the third works the speed and
// the shape out from the line, at whatever rate UP and DOWN have set.
#define SPEED_9600   0
#define SPEED_19200  1
#define SPEED_AUTO   2
static uint8_t speedMode = SPEED_9600;



// RUN steps through RUN, HOLD and SNAP. SNAP is a single shot: it waits for
// the first start bit, keeps the samples from just before it for as long as
// SNAP_CHARS characters take, and stops with them on the screen, so that a
// message that would be gone in a blink under RUN can be looked at and
// photographed. RUN pressed on a picture waits for the next one; pressed
// while still waiting it goes back to RUN.
//
// The picture holds SNAP_CHARS characters, but the message is received to
// its end: reading goes on after the picture is full, until the line has
// been at rest for SNAP_REST_MS or SNAP_MAX_CHARS have come. A scanned label
// longer than the picture is then all in the buffer, and its suffix can be
// looked at in the other views without scanning it again. Reading stops at
// the end of a block, so a few more than SNAP_MAX_CHARS may be taken.
#define SNAP_OFF    0
#define SNAP_ARMED  1       // nothing yet
#define SNAP_FILL   2       // the start bit has come, samples are being kept
#define SNAP_DONE   3       // received to the end, not painted yet
#define SNAP_SHOWN  4
#define SNAP_TAIL   5       // the picture is full, the rest of the message is being read
#define SNAP_CHARS      8
#define SNAP_MAX_CHARS  32
#define SNAP_REST_MS    50
static volatile uint8_t snap = SNAP_OFF;
static uint32_t snapBytes0 = 0;     // the count of characters when the message began
static uint32_t snapRest = 0;       // samples gone by with no start bit in them


// The waveform is drawn from a copy, not from the DMA buffer. A half lasts
// 410 us at 1.25 MS/s and the screen is only redrawn eight times a second,
// so by the time anything was painted the live buffer would have been
// written over several times and the trace would tear.
static uint32_t lastWaveMs = 0;
//
// SNAP keeps its samples here too. A message starts anywhere in a half, so
// its picture runs over into the next one, and the length is what eight
// characters need at the widest of the fixed speeds: 19200 at 250 kS/s is 13
// samples a bit, 1081 for eight frames with the lead in.
#define SNAP_LEN 1152
static uint16_t waveSnap[SNAP_LEN];
static uint16_t waveN = 0;
static uint16_t snapFull = 0;       // what the capture holds; review() may show less of it

// Called with each block once it has been fed to the decoder, from wherever
// that happened. A block that went by unread while the picture was being
// filled leaves a gap, and the picture ends at what was kept before it.
T232_RAM static void snapBlock(const uint16_t* s, uint16_t n, bool over) {
    uint16_t from = 0;
    bool started = Rs232::firstStart() >= 0;

    if (snap == SNAP_ARMED) {
        if (!started) {
            snapBytes0 = Rs232::stats().bytes;      // nothing of the message yet
            return;
        }
        uint32_t pre = (Rs232::stats().bitSamples >> 8) * 2u;   // two bits of rest
        if (pre > 64) pre = 64;
        uint32_t at = (uint32_t)Rs232::firstStart();
        from  = at > pre ? (uint16_t)(at - pre) : 0;
        waveN = 0;
        snap  = SNAP_FILL;
    } else if (snap == SNAP_FILL) {
        if (over) snap = SNAP_TAIL;                 // a gap: the picture ends here
    } else if (snap != SNAP_TAIL) {
        return;
    }

    if (snap == SNAP_FILL) {
        uint16_t take = (uint16_t)(n - from);
        if (take > (uint16_t)(SNAP_LEN - waveN)) take = (uint16_t)(SNAP_LEN - waveN);
        memcpy(waveSnap + waveN, s + from, (size_t)take * sizeof(uint16_t));
        waveN = (uint16_t)(waveN + take);
        if (waveN >= SNAP_LEN) snap = SNAP_TAIL;
    }

    // The message is over when the line has rested, or is long enough.
    snapRest = started ? 0 : snapRest + n;
    bool rested = snapRest * 1000u >= Acq::sampleRate() * SNAP_REST_MS;   // both well within 32 bits
    bool enough = (Rs232::stats().bytes - snapBytes0) >= SNAP_MAX_CHARS;
    if (rested || enough) {
        running = false;
        snap    = SNAP_DONE;
    }
}

#ifndef DEMO_SIGNAL
// The decoder runs in the DMA interrupt, so no sample is missed while the
// screen is painted. The screen asks for a picture with wantSnap; the
// interrupt copies the half it has just decoded, together with the marks
// that go with it, and answers with snapReady. Nothing is asked for while a
// picture is being painted, so the copy is never written under the painter.
static volatile bool wantSnap = false, snapReady = false;

T232_RAM static void onBlock(const uint16_t* s, uint16_t n, bool over) {
    if (!running) return;
    if (over) Rs232::noteOverrun();
    Rs232::feed(s, n, Acq::sampleRate(), Acq::lastLo(), Acq::lastHi());
    if (snap != SNAP_OFF) { snapBlock(s, n, over); return; }
    if (wantSnap) {
        waveN = n > (uint16_t)(Acq::BUF_LEN / 2) ? (uint16_t)(Acq::BUF_LEN / 2) : n;
        memcpy(waveSnap, s, (size_t)waveN * sizeof(uint16_t));
        Rs232::latchView();
        wantSnap  = false;
        snapReady = true;
    }
}
#endif

#ifdef DEMO_SIGNAL
// A line that is not there, behaving like a hand scanner on a real adapter:
// a second of rest first, then a short message at 9600 8N1 with the
// characters back to back, then half a second of rest, and so on. The levels
// are those of the real adapter (about -9 V and +10 V, as simulateMessage()
// has them) on the range in force, with twenty counts of noise, and at rest
// a bump of sixty counts a quarter of a millisecond wide every so often,
// which on the DSO-152 was once taken for traffic. For the pictures of the
// screens (local tools), never for a unit in use.
static uint16_t demoBuf[Acq::BUF_LEN / 2];
static const char DEMO_TEXT[] = "Apreciometru RS232\r";
static uint8_t  demoChar = 0, demoBit = 0;
static uint32_t demoAcc = 0, demoIdle = 100000;
static uint16_t demoNoise = 1, demoBump = 0;

static const uint16_t* demoBlock(uint16_t& n, uint32_t sampleHz) {
    const uint32_t baud = 9600;
    n = (uint16_t)(Acq::BUF_LEN / 2);
    const int32_t zero = Frontend::zeroCounts(), cpv = Frontend::countsPerVolt();
    const int16_t lo = (int16_t)constrain(zero - cpv * 9, 16, 4000);
    const int16_t hi = (int16_t)constrain(zero + cpv * 10, 16, 4000);

    for (uint16_t i = 0; i < n; i++) {
        // bit 0 is the start, 1..8 the data, 9 the stop
        bool mark = true;
        if (demoIdle) {
            demoIdle--;
            if ((demoIdle % 1700u) == 0) demoBump = 26;
        } else {
            uint8_t c = (uint8_t)DEMO_TEXT[demoChar];
            if (demoBit == 0)      mark = false;
            else if (demoBit <= 8) mark = ((c >> (demoBit - 1)) & 1) != 0;

            demoAcc += baud;
            if (demoAcc >= sampleHz) {
                demoAcc -= sampleHz;
                if (++demoBit >= 10) {
                    demoBit = 0;
                    if (DEMO_TEXT[++demoChar] == 0) {
                        demoChar = 0;
                        demoIdle = sampleHz / 2;
                    }
                }
            }
        }

        demoNoise = (uint16_t)(demoNoise * 1103u + 12345u);
        int16_t v = (int16_t)((mark ? lo : hi) + (int16_t)((demoNoise >> 9) & 0x1F) - 16);
        if (demoBump) { demoBump--; v += 60; }
        demoBuf[i] = (uint16_t)v;
    }
    return demoBuf;
}
#endif

static void updateStatusBar() {
    Rs232::Stats st = Rs232::stats();
    tft.fillRect(0, 0, SCREEN_WIDTH, 20, TH.bar);
    tft.setTextSize(1);
    tft.setTextColor(TH.barText, TH.bar);

    tft.setCursor(2, 6);
    tft.print(snap == SNAP_OFF ? (running ? "RUN " : "HOLD")
                                    : (running ? "WAIT" : "SNAP"));

    tft.setCursor(44, 6);
    tft.print("R");
    tft.print(Frontend::range());
    tft.print(Frontend::couplingDC() ? " DC" : " AC");

    tft.setCursor(110, 6);
    if (speedMode == SPEED_AUTO) tft.print("A ");
    if (st.locked && st.baud) {
        Rs232::Format f = Rs232::format();
        tft.print(st.baud);
        tft.print(' ');
        tft.print(f.dataBits);
        tft.print(f.parity);
        tft.print(f.stopBits);
        if (!f.decided) tft.print('?');     // still striking candidates out
    } else {
        tft.print("baud ?");
    }

    tft.setCursor(200, 6);
    tft.print(Rs232::inverted() ? "RS232" : "TTL");

    tft.setCursor(240, 6);
    tft.print(st.bytes);
    tft.print(" B");
}

// The byte count moves all the time, the rest of the bar hardly ever, so the
// count is rewritten on its own and the bar only when something in it changed.
static void updateByteCount() {
    tft.setTextSize(1);
    tft.setTextColor(TH.barText, TH.bar);
    tft.setCursor(240, 6);
    tft.print(Rs232::stats().bytes);
    tft.print(" B  ");
}

static void refreshStatusBar() {
    static uint32_t lastMs = 0, lastKey = 0;
    uint32_t now = millis();
    if (now - lastMs < 500) return;
    lastMs = now;

    Rs232::Stats  st = Rs232::stats();
    Rs232::Format f  = Rs232::format();
    uint32_t key = (st.locked ? st.baud : 0)
                 ^ ((uint32_t)f.dataBits << 28) ^ ((uint32_t)f.parity << 20)
                 ^ ((uint32_t)f.stopBits << 26) ^ ((uint32_t)f.decided << 31);
    if (key != lastKey) { lastKey = key; updateStatusBar(); }
    else                updateByteCount();
}

static void paintProt() {
    Prot::drawWave(tft, waveSnap, waveN);
    Prot::drawBus(tft, waveN);
    Prot::drawLevels(tft, Acq::sampleRate(), dataBuffer, bufferIndex);
}

// The levels are held from one message to the next, so anything that moves
// them on the ADC -- another range, the coupling -- has to start them again.
static void paintWaves();

// The decoder starts again. Not while a capture is on the screen: that is
// read again at once, with levels taken from itself and the speed it was
// read at, and the decoder starts again when the next capture is asked for.
// Thrown away there, the speed went with it, and the teaching view found no
// start bit in the capture until RUN.
static bool relearnPending = false;

static void relearn() {
    Acq::pause();
    bool shown = snap == SNAP_SHOWN;
    if (shown) {
        relearnPending = true;
        Rs232::levelsFrom(waveSnap, snapFull);
        waveN = Rs232::review(waveSnap, snapFull, SNAP_CHARS);
    } else {
        Rs232::reset();
    }
    Acq::resume();
    if (shown) paintWaves();
}

static void applySpeedMode() {
    if (speedMode == SPEED_9600)  rateIdx = 0;
    if (speedMode == SPEED_19200) rateIdx = 1;
    Acq::setSampleRate(RATES[rateIdx]);
    Acq::pause();
    Rs232::setFixedBaud(speedMode == SPEED_9600 ? 9600 : speedMode == SPEED_19200 ? 19200 : 0);
    Rs232::reset();
    Acq::resume();
}

static void compactBuffer() {
    const int keep = BUFFER_SIZE / 2;
    memmove(dataBuffer, dataBuffer + bufferIndex - keep, keep);
    bufferIndex = keep;
    dataBuffer[bufferIndex] = 0;
    lastBufferIndex = 0;
    scrollFirstLine = -1;
}

static void drainDecoder() {
    int c;
    while ((c = Rs232::read()) >= 0) {
        if (bufferIndex >= BUFFER_SIZE - 1) compactBuffer();
        dataBuffer[bufferIndex++] = (char)c;
        dataBuffer[bufferIndex] = 0;
    }
}

static void updateTextMode(bool hex) {
    const int perLine  = hex ? HEX_PER_LINE  : CHARS_PER_LINE;
    const int visLines = hex ? HEX_VIS_LINES : CHARS_VIS_LINES;
    const int lineH    = hex ? HEX_LINE_H    : CHARS_LINE_H;

    int totalLines = (bufferIndex + perLine - 1) / perLine;
    int firstLine  = totalLines > visLines ? totalLines - visLines : 0;

    if (lastDisplayedLines == 0 || firstLine != scrollFirstLine) {
        tft.fillRect(0, 20, SCREEN_WIDTH, SCREEN_HEIGHT - 20, TH.bg);
        if (hex) displayHex(tft, dataBuffer, bufferIndex, firstLine);
        else     displayChars(tft, dataBuffer, bufferIndex, firstLine);
        scrollFirstLine = firstLine;
        lastDisplayedLines = 1;
        return;
    }
    int fromLine = lastBufferIndex / perLine;
    if (fromLine < firstLine) fromLine = firstLine;
    for (int ln = fromLine; ln < totalLines; ln++) {
        int y = 20 + (ln - firstLine) * lineH;
        if (hex) displayHexLine(tft, dataBuffer, bufferIndex, ln, y);
        else     displayCharsLine(tft, dataBuffer, bufferIndex, ln, y);
    }
}

static void redrawAll() {
    tft.fillScreen(TH.bg);
    updateStatusBar();
    lastDisplayedLines = 0;
    scrollFirstLine = -1;
    if (displayMode == MODE_PROT) {
        Prot::drawChrome(tft);
        if (snap == SNAP_SHOWN) paintProt();
    }
    if (displayMode == MODE_TEACH) Teach::draw(tft, waveSnap, waveN, !running);
    if (displayMode == MODE_HEALTH) Health::draw(tft);
}

// The two views drawn from a snapshot of the samples. The teaching one is
// redrawn whole, so less often, or it would flicker.
static bool wavesShown() { return displayMode == MODE_PROT || displayMode == MODE_TEACH; }
static uint32_t waveEveryMs() { return displayMode == MODE_TEACH ? 600 : 120; }
static void paintWaves() {
    if (displayMode == MODE_PROT)  paintProt();
    if (displayMode == MODE_TEACH) Teach::draw(tft, waveSnap, waveN, !running);
}

// Asked for from Settings. Whatever was decoded but not yet collected goes
// too, or it would turn up in the empty buffer the next time round.
static void eraseBuffer() {
    while (Rs232::read() >= 0) {}
    Rs232::clearCounts();               // the errors were of what has just gone
    bufferIndex = 0;
    dataBuffer[0] = 0;
    lastBufferIndex = 0;
    lastDisplayedLines = 0;
    scrollFirstLine = -1;
}

static void armSnap() {
    Acq::pause();
    if (relearnPending) { Rs232::reset(); relearnPending = false; }
#ifndef DEMO_SIGNAL
    wantSnap = false; snapReady = false;
#endif
    snap    = SNAP_ARMED;
    snapBytes0 = Rs232::stats().bytes;
    snapRest   = 0;
    running = true;
    Acq::resume();
    displayMode = MODE_PROT;            // the picture is drawn nowhere else
    redrawAll();
}

// The samples are all in. They are read once more from the top, this time
// for the marks and the characters that belong to exactly this stretch.
// A message that never came down the line: "Apreciometru RS232" and CR LF,
// made into samples the way the line would bring them, at the speed and the
// sample rate in force, and put through the decoder and the capture exactly
// as a block from the converter is. The waveform, the characters under it
// and the buffer then show it as if it had been received, which is a way to
// see every view working with nothing on the probe.
//
// The levels are those a real adapter gave on range 7, either side of zero
// volts; in TTL mode the other way up, since there the line rests high.
static void simulateMessage() {
    static const char MSG[] = "Apreciometru RS232\r\n";
    Rs232::Stats st = Rs232::stats();
    uint32_t hz   = Acq::sampleRate();
    uint32_t baud = Rs232::fixedBaud() ? Rs232::fixedBaud()
                  : (st.locked && st.baud) ? st.baud : 9600;
    bool     inv  = Rs232::inverted();
    // RS232: the levels of the real adapter, about -9 V and +10 V, on the
    // range in force. TTL: 0 V and 3.3 V. The most sensitive ranges clip.
    int32_t  zero  = Rs232::zeroCounts();
    int32_t  cpv   = Frontend::countsPerVolt();
    int32_t  up    = zero + cpv * 33 / 10;
    int32_t  m9    = zero - cpv * 9;
    int32_t  p10   = zero + cpv * 10;
    uint16_t mark  = (uint16_t)constrain(inv ? m9 : up, 16, 4000);
    uint16_t space = (uint16_t)constrain(inv ? p10 : zero, 16, 4000);

    Acq::pause();                       // no real block may come in between
    uint16_t blk[256];
    uint16_t ch = 0, bit = 0, noise = 1;
    uint32_t acc = 0, lead = 30;        // some rest first, for the start bit to stand out
    bool     sending = true;

    // Blocks until the capture says the message is over, which it does once
    // the line has rested after it. The guard is far beyond what that takes.
    for (uint16_t guard = 0; snap != SNAP_DONE && guard < 600; guard++) {
        IWDG->KR = 0xAAAA;
        for (uint16_t i = 0; i < 256; i++) {
            bool m = true;
            if (lead) {
                lead--;
            } else if (sending) {
                uint8_t c = (uint8_t)MSG[ch];
                if (bit == 0)      m = false;                   // start
                else if (bit <= 8) m = ((c >> (bit - 1)) & 1) != 0;
                acc += baud;
                if (acc >= hz) {
                    acc -= hz;
                    if (++bit >= 10) { bit = 0; if (MSG[++ch] == 0) sending = false; }
                }
            }
            noise = (uint16_t)(noise * 1103u + 12345u);
            blk[i] = (uint16_t)((m ? mark : space) + ((noise >> 9) & 15) - 8);
        }
        Rs232::feed(blk, 256, hz);
        snapBlock(blk, 256, false);
    }

    Acq::discard();                     // what the converter did meanwhile is not wanted
    Acq::resume();
}

static void showSnap() {
    drainDecoder();
    snapFull = waveN;
    Rs232::levelsFrom(waveSnap, snapFull);
    waveN = Rs232::review(waveSnap, waveN, SNAP_CHARS);
    snap  = SNAP_SHOWN;
    Teach::select(0);
    paintWaves();
    updateStatusBar();
}

// The threshold by hand, from the teaching view. On a capture that is on
// the screen, the capture is read again with it, so that what a threshold
// in the wrong place does to the bits can be seen.
static void setThresholdTo(uint16_t th) {
    Acq::pause();
    if (snap == SNAP_SHOWN && !th) Rs232::levelsFrom(waveSnap, snapFull);
    Rs232::setThreshold(th);
    // read again from the whole capture: a threshold in the wrong place can
    // find an earlier start, and the stretch shown would be cut short with it
    if (snap == SNAP_SHOWN) waveN = Rs232::review(waveSnap, snapFull, SNAP_CHARS);
    Acq::resume();
    Teach::draw(tft, waveSnap, waveN, !running);
}

static void moveThreshold(int8_t d) {
    Rs232::Stats st = Rs232::stats();
    int32_t step = (st.levels && st.hi > st.lo) ? (st.hi - st.lo) / 20 : 16;
    if (step < 4) step = 4;
    int32_t th = (int32_t)st.threshold + d * step;
    if (th < 1)    th = 1;
    if (th > 4094) th = 4094;
    setThresholdTo((uint16_t)th);
}

static void updateDisplay() {
    if (displayMode == MODE_TEACH) return;          // drawn from the samples, in paintWaves()
    if (displayMode == MODE_HEALTH) return;         // drawn by Health::poll()
    if (displayMode == MODE_PROT) {
        if (bufferIndex != lastBufferIndex || lastDisplayedLines == 0) {
            Prot::drawStream(tft, dataBuffer, bufferIndex);
            lastBufferIndex = bufferIndex;
            lastDisplayedLines = 1;
        }
        return;
    }

    if (bufferIndex != lastBufferIndex || lastDisplayedLines == 0) {
        switch (displayMode) {
            case MODE_HEX:   updateTextMode(true);  break;
            case MODE_CHARS: updateTextMode(false); break;
            case MODE_LARGE: {
                int startPos = (bufferIndex >= 32) ? bufferIndex - 32 : 0;
                if (largeBufferChanged(tft, dataBuffer, bufferIndex, startPos)
                    || lastDisplayedLines == 0) {
                    displayLarge(tft, dataBuffer, bufferIndex, startPos);
                    lastDisplayedLines = 1;
                }
                break;
            }
        }
        lastBufferIndex = bufferIndex;
    }
}

// Coming in from the menu. Whatever button is still down from the choosing
// is taken as seen, and the half that settled while nothing was collecting
// is let go rather than counted as lost.
// The bus health view samples at 2 MS/s and has the samples to itself: the
// decoder is not fed while it is up. Its blocks are polled, since at that
// rate an interrupt per half would leave the loop nothing.
//
// A half lasts 0.5 ms here, less than drawing the table takes. After a
// drawing both halves have gone by and the one handed over may be the one
// being written, and its seam made jitter, rise, fall and the bit time
// creep worse every few seconds. So a half that comes after an overrun is
// let go, and a fresh one is copied out and kept only if the DMA has not
// reached it by the end of the copy.
#ifndef DEMO_SIGNAL
static uint16_t healthCopy[Acq::BUF_LEN / 2];
#endif
static const uint16_t* healthSource(uint16_t& n) {
#ifdef DEMO_SIGNAL
    return demoBlock(n, Acq::sampleRate());
#else
    bool over;
    const uint16_t* s = Acq::readyHalf(n, over);
    if (!s || over) return nullptr;
    memcpy(healthCopy, s, n * sizeof(uint16_t));
    return Acq::halfIntact() ? healthCopy : nullptr;
#endif
}

static void healthEnter() {
    Acq::pause();
    // A capture under way would never be filled while this view has the
    // samples; one on the screen, or a held screen, is kept for afterwards.
    if (snap == SNAP_ARMED || snap == SNAP_FILL || snap == SNAP_TAIL) {
        snap    = SNAP_OFF;
        running = true;
    }
#ifndef DEMO_SIGNAL
    wantSnap = false; snapReady = false;
#endif
    Acq::setSampleRate(Health::SAMPLE_HZ);
    Acq::discard();
    Health::enter(tft, healthSource, Acq::sampleRate());
}

static void healthLeave() {
    Acq::setSampleRate(RATES[rateIdx]);
    Acq::discard();
    Acq::resume();
}

static void setMode(uint8_t m) {
    if (displayMode == MODE_HEALTH && m != MODE_HEALTH) healthLeave();
    displayMode = m;
    redrawAll();
    if (m == MODE_HEALTH) healthEnter();
}

static void testerEnter() {
    Keys::sync();
    redrawAll();
    Acq::discard();
    Acq::resume();
    if (displayMode == MODE_HEALTH) healthEnter();
}

// Back to the menu. What has been received and learnt is kept for next time.
static void testerLeave() {
    if (displayMode == MODE_HEALTH) healthLeave();
    Acq::pause();
}

static void showMenu() {
    app = APP_MENU;
    Keys::sync();
    Menu::draw(tft);
}

#ifndef RAM_RUN
// A fault, from flash: what and where, on the screen (Probe.cpp).
static void showFault() {
    tft.backlight(60);
    tft.fillScreen(0xF800);
    tft.setTextColor(0xFFFF, 0xF800);
    tft.setTextSize(2);
    tft.setCursor(8, 8);
    tft.print(FW_NAME " v" FW_VERSION " FAULT");
    const char* const name[] = { "stage", "CFSR", "HFSR", "BFAR", "PC" };
    const uint32_t v[] = { probe.stage, probe.fault[0], probe.fault[1], probe.fault[2], probe.fault[4] };
    for (uint8_t i = 0; i < 5; i++) {
        tft.setCursor(8, 40 + i * 24);
        tft.print(name[i]);
        tft.setCursor(80, 40 + i * 24);
        tft.print(v[i], HEX);
    }
    tft.setCursor(8, 200);
    tft.print("POWER held: off");
}
#endif

void setup() {
    // PA3, which the DSO-152's PC10 power hold stood for, is set high in
    // SystemClock_Config (src/hw/Probe.cpp), before anything else.
    stage(100);
    Pins::begin();
    Keys::begin();

    Config::load();                     // the theme, before anything is drawn
    stage(101);
    tft.begin();
#ifndef RAM_RUN
    fault_show = showFault;
#endif
    stage(102);
    tft.fillScreen(0);
    tft.backlight(60);
    dataBuffer[0] = 0;

#ifdef USBCON
    usbFix();                           // the receive path, made to work on this chip
    SerialUSB.begin();
#endif

    Frontend::begin();
    stage(103);
    Rs232::begin();
#ifndef DEMO_SIGNAL
    Acq::onBlock(onBlock);
#endif
    stage(104);
    Acq::begin(RATES[rateIdx]);
    Acq::pause();                       // nothing is read until the inspector is started
    stage(105);

    showMenu();
    stage(106);
    // started at 72 MHz with the screen set up there; now 240 (Probe.cpp)
    clock240();
    tft.backlight(60);                  // its PWM counts from the clock
    Acq::setSampleRate(RATES[rateIdx]);
    showMenu();
    stage(107);
    probe.magic = kMagicRun;            // for the RAM loader
}

static bool testerLoop();

#ifndef RAM_RUN
static void powerOff() {
    tft.backlight(0);
    tft.fillScreen(0);
    GPIOA->BRR = 1u << 3;
    GPIOB->BRR = 1u << 12;
    // Then a reset, once the power key has been let go for a tenth of a
    // second; the bootloader, finding it up, leaves the unit off. Staying in
    // a loop here, the watchdog fed, as up to v0.23, the core never started
    // again (3V3 stays on): the next press of the key powered the rest of the
    // board, the screen flashed white, and on letting go it was off again,
    // which looked like a unit that would not start (2026-10-27). The FNIRSI
    // firmware stops feeding the watchdog instead.
    uint32_t up = 0;
    while (up < 100) {
        IWDG->KR = 0xAAAA;
        up = Pins::read(PIN_A(6)) ? up + 1 : 0;
        delay(1);
    }
    NVIC_SystemReset();
}
#endif

void loop() {
    IWDG->KR = 0xAAAA;                  // the FNIRSI firmware started it; it cannot be stopped
    probe.loops++;
    probe.ms = millis();
#ifdef USBCON
    usbWatch();                         // a control request left unanswered: off USB and back
#endif

#ifndef RAM_RUN
    // The power key held for two seconds switches the unit off, the way the
    // FNIRSI firmware does it (0x0801DB98): PA3 (the power hold) and PB12 low.
    // The key itself keeps the power on until it is let go, so the watchdog
    // is fed till then; a reset would start FNIRSI.
    static uint32_t powerSince = 0;
    if (!Pins::read(PIN_A(6))) {
        if (!powerSince) powerSince = millis() | 1;
        else if (millis() - powerSince > 2000) powerOff();
    } else {
        powerSince = 0;
    }

    // Auto off (Settings): any key down, or anything new in the buffer, is
    // the unit being used.
    static uint32_t usedMs = 0;
    static int      usedLen = 0;
    bool used = bufferIndex != usedLen;
    for (uint8_t k = 0; k < Keys::COUNT && !used; k++) used = Keys::down(k);
    if (used) { usedMs = millis(); usedLen = bufferIndex; }
    uint32_t offMs = Config::autoOffMs();
    if (offMs && millis() - usedMs > offMs) powerOff();
#endif

    switch (app) {
        case APP_MENU: {
            int8_t pick = Menu::poll(tft);
            if (pick == Menu::ITEM_TESTER)   { app = APP_TESTER; testerEnter(); }
            else if (pick == Menu::ITEM_SET) { app = APP_SET;    Settings::enter(tft, bufferIndex); }
            else if (pick == Menu::ITEM_TTL) { app = APP_TTL;    Ttl::enter(tft, dataBuffer, bufferIndex); }
            break;
        }
        case APP_TTL:
            if (!Ttl::poll(tft, dataBuffer, bufferIndex)) { Ttl::leave(); showMenu(); }
            break;
        case APP_SET: {
            uint8_t r = Settings::poll(tft);
            if (r == Settings::ERASE)      { eraseBuffer(); Settings::erased(tft); }
            else if (r == Settings::LEAVE) showMenu();
            break;
        }
        case APP_TESTER:
            if (!testerLoop()) { testerLeave(); showMenu(); }
            break;
    }
}

// One turn of the inspector. False: OK was held, back to the menu.
static bool testerLoop() {

    if (displayMode == MODE_HEALTH) {
        Health::poll(tft);
    } else {
#ifndef DEMO_SIGNAL
    if (running && Acq::interruptDriven()) {
        drainDecoder();
        if (snapReady) {
            snapReady = false;
            paintWaves();
            lastWaveMs = millis();
        } else if (!wantSnap && snap == SNAP_OFF && wavesShown()
                   && (millis() - lastWaveMs) >= waveEveryMs()) {
            wantSnap = true;
        }
    } else
#endif
    if (running) {
        uint16_t n;
        bool over = false;
#ifdef DEMO_SIGNAL
        const uint16_t* half = demoBlock(n, Acq::sampleRate());
        delay(2);                       // roughly the pace of a real block
#else
        const uint16_t* half = Acq::readyHalf(n, over);
#endif
        if (half) {
            if (over) Rs232::noteOverrun();
            Rs232::feed(half, n, Acq::sampleRate());
            drainDecoder();

            // Painted here rather than later: the bit marks and the byte
            // positions belong to the feed just done, and the next one would
            // replace them.
            uint32_t now = millis();
            if (snap != SNAP_OFF) {
                snapBlock(half, n, over);
            } else if (wavesShown() && (now - lastWaveMs) >= waveEveryMs()) {
                lastWaveMs = now;
                waveN = n > (uint16_t)(Acq::BUF_LEN / 2) ? (uint16_t)(Acq::BUF_LEN / 2) : n;
                memcpy(waveSnap, half, (size_t)waveN * sizeof(uint16_t));
                Rs232::latchView();
                paintWaves();
            }
        }
    }
    }

    if (snap == SNAP_DONE) showSnap();

    // MODE changes the view; held while a capture is awaited, it sends the
    // made-up message below instead of waiting for a real one.
    uint8_t mode = Keys::press(Keys::MODE);
    if (mode == Keys::SHORT) {
        setMode(nextMode(displayMode));
    } else if (mode == Keys::LONG && snap == SNAP_ARMED) {
        simulateMessage();
    }

    if (Keys::hit(Keys::RUN)) {
        if (displayMode == MODE_HEALTH) {
            Health::restart(tft);                   // the figures start again
        } else if (snap == SNAP_OFF && running) {
            running = false;                        // RUN -> HOLD
            updateStatusBar();
        } else if (snap == SNAP_OFF || snap == SNAP_SHOWN) {
            armSnap();                              // HOLD -> SNAP, or once more
        } else {
            Acq::pause();                           // still waiting -> RUN
            snap    = SNAP_OFF;
            running = true;
            Acq::resume();
            updateStatusBar();
        }
    }

    // In the teaching view, with the picture still, left and right go from one
    // character of it to the next instead of changing the range.
    if (displayMode == MODE_TEACH && !running) {
        int8_t d = Keys::hit(Keys::RIGHT) ? 1 : Keys::hit(Keys::LEFT) ? -1 : 0;
        if (d && Teach::step(d)) Teach::draw(tft, waveSnap, waveN, true);
    } else {
    if (Keys::hit(Keys::RIGHT)) {
        if (Frontend::range() < Frontend::RANGE_MAX) {
            Frontend::setRange((uint8_t)(Frontend::range() + 1));
            relearn();
        }
        updateStatusBar();
        if (displayMode == MODE_HEALTH) Health::restart(tft);
    }

    if (Keys::hit(Keys::LEFT)) {
        if (Frontend::range() > 0) {
            Frontend::setRange((uint8_t)(Frontend::range() - 1));
            relearn();
        }
        updateStatusBar();
        if (displayMode == MODE_HEALTH) Health::restart(tft);
    }
    }

    // In the teaching view up and down move the threshold instead of the rate.
    if (displayMode == MODE_TEACH) {
        int8_t d = Keys::hit(Keys::UP) ? 1 : Keys::hit(Keys::DOWN) ? -1 : 0;
        if (d) moveThreshold(d);
    } else if (displayMode != MODE_HEALTH) {        // the health view keeps its own rate
    if (Keys::hit(Keys::UP)) {
        if (rateIdx + 1 < (uint8_t)(sizeof(RATES) / sizeof(RATES[0]))) {
            rateIdx++;
            Acq::setSampleRate(RATES[rateIdx]);
            Acq::pause();
            Rs232::reset();
            Acq::resume();
        }
        updateStatusBar();
    }

    if (Keys::hit(Keys::DOWN)) {
        if (rateIdx > 0) {
            rateIdx--;
            Acq::setSampleRate(RATES[rateIdx]);
            Acq::pause();
            Rs232::reset();
            Acq::resume();
        }
        updateStatusBar();
    }
    }

    // AUTO changes the speed when let go; held in the teaching view, it gives
    // the threshold back to the middle of the levels.
    uint8_t autoKey = Keys::press(Keys::AUTO);
    if (autoKey == Keys::SHORT) {
        speedMode = (uint8_t)((speedMode + 1) % 3);
        applySpeedMode();
        updateStatusBar();
        if (displayMode == MODE_TEACH) Teach::draw(tft, waveSnap, waveN, !running);
        if (displayMode == MODE_HEALTH) {           // the speed mode set its own rate
            Acq::setSampleRate(Health::SAMPLE_HZ);
            Acq::discard();
            Health::restart(tft);
        }
    } else if (autoKey == Keys::LONG && displayMode == MODE_TEACH) {
        setThresholdTo(0);
    }

    // OK held goes back to the menu, so a short press is acted on when the
    // button comes up, not when it goes down.
    uint8_t ok = Keys::press(Keys::OK);
    if (ok == Keys::LONG) return false;
    if (ok == Keys::SHORT) {
        Rs232::setInverted(!Rs232::inverted());   // RS232 swing, or plain TTL
        relearn();
        updateStatusBar();
        if (displayMode == MODE_HEALTH) Health::restart(tft);
    }

    if (Keys::hit(Keys::RELEARN)) {
        Acq::pause();
        Rs232::reset();                     // measure the speed and shape again
        Acq::resume();
        updateStatusBar();
        if (displayMode == MODE_HEALTH) Health::restart(tft);
    }

    // Nothing is painted while a picture is awaited or being filled: above
    // IRQ_MAX_HZ the halves are fetched by this loop, and one that went by
    // during a repaint would cut the picture short.
    if (snap == SNAP_ARMED || snap == SNAP_FILL || snap == SNAP_TAIL) return true;

    updateDisplay();
    refreshStatusBar();
    return true;
}
