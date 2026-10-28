#include "Export.h"

namespace Export {

static const char* const NAME[NFMT] = { "Raw", "Hex dump" };
const char* name(uint8_t fmt) { return fmt < NFMT ? NAME[fmt] : "?"; }

// Bit by bit rather than from a table: the buffer is two kilobytes at most
// and the table would be one.
uint32_t crc32(const uint8_t* p, uint16_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (uint16_t i = 0; i < n; i++) {
        c ^= p[i];
        for (uint8_t k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// --- a line being put together --------------------------------------------
static char    g_line[80];
static uint8_t g_n;

static void ch(char c) { if (g_n < sizeof(g_line)) g_line[g_n++] = c; }
static void str(const char* s) { while (*s) ch(*s++); }
static void hex(uint32_t v, uint8_t digits, bool upper) {
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    while (digits--) ch(d[(v >> (digits * 4)) & 0xF]);
}
static void dec(uint32_t v) {
    char t[10];
    uint8_t n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) ch(t[--n]);
}
static bool flush(Sink out) {
    ch('\r'); ch('\n');
    bool ok = out((const uint8_t*)g_line, g_n);
    g_n = 0;
    return ok;
}

static bool dumpBlock(const uint8_t* buf, uint16_t off, uint16_t len,
                      uint16_t blk, uint16_t nblk, Sink out) {
    g_n = 0;
    str("# DST-210 block "); dec(blk); ch('/'); dec(nblk);
    str(" offset "); dec(off); str(" length "); dec(len);
    if (!flush(out)) return false;

    for (uint16_t i = 0; i < len; i += 16) {
        uint16_t n = (uint16_t)(len - i < 16 ? len - i : 16);
        hex((uint32_t)off + i, 8, false); ch(':');
        for (uint16_t k = 0; k < 16; k++) {
            if (!(k & 1)) ch(' ');
            if (k < n) hex(buf[off + i + k], 2, false);
            else       { ch(' '); ch(' '); }
        }
        ch(' '); ch(' ');
        for (uint16_t k = 0; k < n; k++) {
            uint8_t b = buf[off + i + k];
            ch((char)((b >= 32 && b < 127) ? b : '.'));
        }
        if (!flush(out)) return false;
    }

    str("# crc32 "); hex(crc32(buf + off, len), 8, true);
    return flush(out);
}

bool block(uint8_t fmt, const uint8_t* buf, uint16_t off, uint16_t len,
           uint16_t blk, uint16_t nblk, Sink out) {
    if (!buf || !out) return false;
    switch (fmt) {
        case DUMP: return dumpBlock(buf, off, len, blk, nblk, out);
        default:   return out(buf + off, len);
    }
}

bool finish(uint8_t fmt, const uint8_t* buf, uint16_t total, bool all, Sink out) {
    if (!buf || !out) return false;
    g_n = 0;
    if (fmt == DUMP && all) {
        str("# total "); dec(total); str(" bytes crc32 "); hex(crc32(buf, total), 8, true);
        return flush(out);
    }
    return true;
}

}
