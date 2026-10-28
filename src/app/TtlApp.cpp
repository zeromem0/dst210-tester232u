#include <Arduino.h>
#include "TtlApp.h"
#include "Theme.h"
#include "Keys.h"
#include "Export.h"
#include "Config.h"
#include "UsbFix.h"
#ifdef USBCON
extern "C" {
#include "usbd_cdc_if.h"
extern USBD_HandleTypeDef hUSBD_Device_CDC;
}
#endif

namespace Ttl {

static uint16_t g_sel = 0;      // 0: everything; otherwise that block, from 1

#define SIZE_Y     28
#define LINK_Y     52
#define BLOCK_Y    76
#define STATE_Y    102
#define TAIL_Y     132
#define TAIL_COLS  52
#define TAIL_ROWS  3

// --- the buffer in blocks ---------------------------------------------------
static uint16_t blocks(int len) {
    uint16_t bs = Config::blockSize();
    if (bs == 0 || len <= 0) return 1;
    return (uint16_t)((len + bs - 1) / bs);
}

static void blockAt(uint16_t blk, int len, uint16_t& off, uint16_t& n) {
    uint16_t bs = Config::blockSize();
    if (bs == 0) { off = 0; n = (uint16_t)len; return; }
    off = (uint16_t)((blk - 1) * bs);
    n   = (uint16_t)(len - off < bs ? len - off : bs);
}

// what RUN would send now
static void chosen(int len, uint16_t& off, uint16_t& n) {
    if (g_sel == 0) { off = 0; n = (uint16_t)len; }
    else            blockAt(g_sel, len, off, n);
}

// --- the screen ---------------------------------------------------------------
static void drawSize(DSO152TFT& tft, const char* buf, int len) {
    uint16_t off, n;
    chosen(len, off, n);
    tft.fillRect(0, SIZE_Y, 320, 16, TH.bg);
    tft.setTextSize(2);
    tft.setTextColor(n ? TH.text : TH.muted, TH.bg);
    tft.setCursor(4, SIZE_Y);
    tft.print(n);
    tft.print(" bytes");
    if (n) {
        uint32_t crc = Export::crc32((const uint8_t*)buf + off, n);
        tft.setTextColor(TH.trace, TH.bg);
        tft.setCursor(148, SIZE_Y);
        tft.print("CRC ");
        for (int8_t s = 28; s >= 0; s -= 4) tft.print((char)"0123456789ABCDEF"[(crc >> s) & 0xF]);
    }
}

static bool g_portOpen;

static bool portOpen() {
#ifdef USBCON
    return usbOpen();
#else
    return false;
#endif
}

static void drawLink(DSO152TFT& tft) {
    g_portOpen = portOpen();
    tft.fillRect(0, LINK_Y, 320, 16, TH.bg);
    tft.setTextSize(2);
    tft.setTextColor(g_portOpen ? TH.text2 : TH.err, TH.bg);
    tft.setCursor(4, LINK_Y);
    tft.print(g_portOpen ? "USB open" : "USB closed");
    tft.setTextColor(TH.text2, TH.bg);
    tft.setCursor(148, LINK_Y);
    tft.print(Export::name(Config::format));
}

static void drawBlock(DSO152TFT& tft, int len) {
    uint16_t nb = blocks(len);
    tft.fillRect(0, BLOCK_Y, 320, 16, TH.bg);
    tft.setTextSize(2);
    tft.setTextColor(TH.text2, TH.bg);
    tft.setCursor(4, BLOCK_Y);
    if (g_sel == 0) {
        tft.print("ALL");
        if (nb > 1) {
            tft.setCursor(148, BLOCK_Y);
            tft.print(nb); tft.print(" x "); tft.print(Config::blockSize());
        }
    } else {
        uint16_t off, n;
        blockAt(g_sel, len, off, n);
        tft.print("BLOCK "); tft.print(g_sel); tft.print('/'); tft.print(nb);
        tft.setCursor(148, BLOCK_Y);
        tft.print("at "); tft.print(off);
    }
}

// One line of state. `n` is printed after the text when it is not negative.
static void drawState(DSO152TFT& tft, const char* txt, int n, uint16_t col) {
    int16_t len = (int16_t)strlen(txt);
    if (n >= 0) len = (int16_t)(len + 1 + (n >= 1000 ? 4 : n >= 100 ? 3 : n >= 10 ? 2 : 1));
    tft.fillRect(0, STATE_Y, 320, 16, TH.bg);
    tft.setTextSize(2);
    tft.setTextColor(col, TH.bg);
    tft.setCursor((int16_t)((320 - len * 12) / 2), STATE_Y);
    tft.print(txt);
    if (n >= 0) { tft.print(' '); tft.print(n); }
}

static void drawReady(DSO152TFT& tft, int len) {
    if (len) drawState(tft, "READY", -1, TH.ok);
    else     drawState(tft, "BUFFER EMPTY", -1, TH.muted);
}

// The end of what is chosen, as text, so that it can be seen what is about
// to go out. What does not print is shown as a dot.
static void drawTail(DSO152TFT& tft, const char* buf, int len) {
    uint16_t off, n;
    chosen(len, off, n);
    const int shown = TAIL_COLS * TAIL_ROWS;
    int start = n > shown ? n - shown : 0;
    tft.fillRect(0, TAIL_Y, 320, TAIL_ROWS * 12, TH.bg);
    tft.setTextSize(1);
    tft.setTextColor(TH.text, TH.bg);
    for (int i = start; i < n; i++) {
        int k = i - start;
        if (k % TAIL_COLS == 0) tft.setCursor(4, (int16_t)(TAIL_Y + (k / TAIL_COLS) * 12));
        uint8_t b = (uint8_t)buf[off + i];
        tft.print((char)((b >= 32 && b < 127) ? b : '.'));
    }
}

static void drawChoice(DSO152TFT& tft, const char* buf, int len) {
    drawSize(tft, buf, len);
    drawBlock(tft, len);
    drawTail(tft, buf, len);
}

void enter(DSO152TFT& tft, const char* buf, int len) {
    Keys::sync();
    if (g_sel > blocks(len) || blocks(len) == 1) g_sel = 0;

    tft.fillScreen(TH.bg);
    tft.fillRect(0, 0, 320, 20, TH.bar);
    tft.setTextSize(1);
    tft.setTextColor(TH.barText, TH.bar);
    tft.setCursor(2, 6);
    tft.print("USB SENDER");
    tft.setCursor(130, 6);
    tft.print("the buffer to the PC, over USB");

    drawLink(tft);
    drawChoice(tft, buf, len);
    drawReady(tft, len);

    tft.drawFastHLine(0, TAIL_Y - 6, 320, TH.line);
    tft.drawFastHLine(0, 196, 320, TH.line);
    tft.setTextSize(1);
    tft.setTextColor(TH.muted, TH.bg);
    tft.setCursor(8, 204);
    tft.print((char)0x1E); tft.print((char)0x1F);       // up and down arrows
    tft.print(" block   RUN send / stop");
    tft.setCursor(8, 220);
    tft.print("MODE menu   format and block: Settings");
}

// --- sending --------------------------------------------------------------------
// Asked before each piece. RUN is still down from the press that started the
// sending, so it has to come up before a press of it can mean stop. A port
// the PC has closed stops it too.
static bool g_runUp;
static bool keepSending() {
    IWDG->KR = 0xAAAA;
    if (!portOpen()) return false;
    if (!Keys::down(Keys::RUN)) g_runUp = true;
    else if (g_runUp) return false;
    return true;
}

// The exports hand over a line at a time, some 70 bytes. The core sends what
// is in its queue as one transfer. Transfers of several packets were lost now
// and then on this chip: a packet went missing in the middle of a 66 kB
// export. So what is handed over is gathered here and goes out 63 bytes at a
// time, each a transfer of a single short packet (no packet of length zero
// either), the next only once the one before is gone. That still makes some
// 370 kB/s.
#define PK_SIZE 63
static uint8_t  g_pk[PK_SIZE];
static uint16_t g_pkN;

// Waits until the core has nothing in flight, so that its queue never holds
// more than one packet. A PC that reads slowly is waited for as long as it
// takes; only RUN or a closed port end the wait. False then.
static bool idle() {
#ifdef USBCON
    for (;;) {
        USBD_CDC_HandleTypeDef* h = (USBD_CDC_HandleTypeDef*)hUSBD_Device_CDC.pClassData;
        if (!h || (h->TxState == 0 && CDC_TransmitQueue_ReadSize(&TransmitQueue) == 0)) return true;
        if (!keepSending()) return false;
    }
#else
    return true;
#endif
}

static bool sendPacket(const uint8_t* p, uint16_t n) {
#ifdef USBCON
    if (!idle()) return false;
    return SerialUSB.write(p, n) == n;
#else
    (void)p; (void)n;
    return false;
#endif
}

static bool flush() {
    if (g_pkN && !sendPacket(g_pk, g_pkN)) return false;
    g_pkN = 0;
    return idle();
}

static bool toWire(const uint8_t* p, uint16_t n) {
#ifdef USBCON
    while (n) {
        uint16_t k = (uint16_t)(PK_SIZE - g_pkN);
        if (k > n) k = n;
        memcpy(g_pk + g_pkN, p, k);
        g_pkN = (uint16_t)(g_pkN + k);
        p += k; n = (uint16_t)(n - k);
        if (g_pkN == PK_SIZE) {
            if (!keepSending()) return false;           // a stop is heard within a packet
            if (!sendPacket(g_pk, PK_SIZE)) return false;
            g_pkN = 0;
        }
    }
    return true;
#else
    (void)p; (void)n;
    return false;
#endif
}

// A breath between blocks, so that they stand apart in a log.
static bool pauseBetween() {
    if (!flush()) return false;
    uint32_t t0 = millis();
    while ((millis() - t0) < 20) if (!keepSending()) return false;
    return true;
}

static bool sendAll(const char* buf, int len) {
    const uint8_t* b = (const uint8_t*)buf;
    uint16_t nb = blocks(len), off, n;

    if (g_sel) {
        blockAt(g_sel, len, off, n);
        return Export::block(Config::format, b, off, n, g_sel, nb, toWire)
            && Export::finish(Config::format, b, (uint16_t)len, false, toWire);
    }
    for (uint16_t k = 1; k <= nb; k++) {
        blockAt(k, len, off, n);
        if (k > 1 && !pauseBetween()) return false;
        if (!Export::block(Config::format, b, off, n, k, nb, toWire)) return false;
    }
    return Export::finish(Config::format, b, (uint16_t)len, true, toWire);
}

uint32_t g_sendMs;                     // how long the last export took, for tests over SWD

static bool sendChosen(const char* buf, int len) {
    g_runUp = false;
    g_pkN = 0;
    uint32_t t0 = millis();
    bool ok = sendAll(buf, len) && flush();
    g_sendMs = millis() - t0;
    g_pkN = 0;                          // a stopped export leaves nothing behind for the next
    return ok;
}

bool poll(DSO152TFT& tft, const char* buf, int len) {
    if (portOpen() != g_portOpen) drawLink(tft);

    uint16_t nb = blocks(len), sel = g_sel;
    if (nb > 1) {
        if (Keys::hit(Keys::DOWN) && g_sel < nb) g_sel++;
        if (Keys::hit(Keys::UP)   && g_sel > 0)  g_sel--;
    }
    if (g_sel != sel) { drawChoice(tft, buf, len); drawReady(tft, len); }

    if (Keys::hit(Keys::RUN) && len > 0) {
        if (!portOpen()) { drawState(tft, "OPEN THE PORT ON THE PC", -1, TH.err); return true; }
        drawState(tft, "SENDING", -1, TH.trace);
        if (sendChosen(buf, len)) drawState(tft, "SENT", -1, TH.ok);
        else                      drawState(tft, "STOPPED", -1, TH.err);
        Keys::sync();                   // the RUN that stopped it is not a new start
    }

    if (Keys::hit(Keys::MODE) || Keys::press(Keys::OK) == Keys::LONG) return false;
    return true;
}

void leave() {}

}
