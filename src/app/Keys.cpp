#include "Keys.h"
#include "Pins.h"

namespace Keys {

// in the order of the keys in Keys.h: UP PC6, DOWN PC9, LEFT PC7, RIGHT PC8,
// OK (ENTER) PD13, MODE PB14, AUTO PD12, RUN PB15, RELEARN (power) PA6
static const Pins::Pin PIN[COUNT] = {
    PIN_C(6), PIN_C(9), PIN_C(7), PIN_C(8),
    PIN_D(13), PIN_B(14), PIN_D(12), PIN_B(15), PIN_A(6)
};

static const uint32_t SETTLE_MS = 60;
static const uint32_t LONG_MS   = 700;

static bool     g_last[COUNT];
static uint32_t g_upMs[COUNT];      // when the key was last seen down

// for press(): a key is not armed while it is still down from the screen before
static bool     g_armed[COUNT], g_held[COUNT], g_fired[COUNT];
static uint32_t g_since[COUNT];

void begin() {
    for (uint8_t k = 0; k < COUNT; k++) Pins::inputPullUp(PIN[k]);
    sync();
}

bool down(uint8_t k) { return k < COUNT && !Pins::read(PIN[k]); }

// Every key is taken as down this moment, whatever it reads: the press that
// led here may still be bouncing, and a key read up in the middle of that
// would be armed at once and its next bounce taken for a new press (RUN in
// the menu, then out of Settings straight away). A key has to be seen up for
// SETTLE_MS before it counts again.
void sync() {
    uint32_t now = millis();
    for (uint8_t k = 0; k < COUNT; k++) {
        g_last[k]  = true;
        g_upMs[k]  = now;
        g_armed[k] = false;
        g_held[k]  = false;
        g_fired[k] = false;
    }
}

bool hit(uint8_t k) {
    if (k >= COUNT) return false;
    bool d = down(k);
    uint32_t now = millis();
    bool h = d && !g_last[k] && (now - g_upMs[k]) >= SETTLE_MS;
    if (d) g_upMs[k] = now;
    g_last[k] = d;
    return h;
}

uint8_t press(uint8_t k) {
    if (k >= COUNT) return 0;
    bool d = down(k);
    uint32_t now = millis();

    if (d) g_upMs[k] = now;
    if (!g_armed[k]) {
        if (!d && now - g_upMs[k] >= SETTLE_MS) g_armed[k] = true;
        return 0;
    }
    if (d) {
        if (!g_held[k]) { g_held[k] = true; g_since[k] = now; g_fired[k] = false; }
        else if (!g_fired[k] && (now - g_since[k]) >= LONG_MS) { g_fired[k] = true; return LONG; }
        return 0;
    }
    if (g_held[k]) {
        uint32_t held = now - g_since[k];
        g_held[k] = false;
        if (!g_fired[k] && held >= 30) return SHORT;
    }
    return 0;
}

}
