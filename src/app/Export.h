// What the sender puts on the wire, for a buffer that may hold anything: a
// scanner's prefix and suffix are not printable, and sent as they are they
// are easily lost or changed on the way to a file on a laptop.
//
//   RAW    the bytes as they are.
//   DUMP   a hex dump in the layout of xxd, sixteen bytes a line with their
//          offset, readable in any terminal and turned back into the bytes
//          by `xxd -r` or by tools/ttl_receive.py. Lines beginning with '#'
//          carry the block number and a CRC-32.
// (Intel HEX, which the other builds have, is left out here for room.)
//
// The link is one way, so nothing can be acknowledged and sent again. What
// there is instead is a CRC-32 (the one of zip and zlib) of what was sent,
// shown on the screen, to be compared with the CRC-32 of what arrived.
//
// A buffer can go out in blocks, so that one that arrived damaged can be
// sent again on its own.

#ifndef EXPORT_H
#define EXPORT_H

#include <stdint.h>

namespace Export {

enum { RAW, DUMP, NFMT };

const char* name(uint8_t fmt);

uint32_t crc32(const uint8_t* p, uint16_t n);

// Takes what is to go out. False stops the export.
typedef bool (*Sink)(const uint8_t* p, uint16_t n);

// Bytes off .. off+len-1 of buf, as block `blk` of `nblk`, counted from 1.
bool block(uint8_t fmt, const uint8_t* buf, uint16_t off, uint16_t len,
           uint16_t blk, uint16_t nblk, Sink out);

// What closes an export: the end record of Intel HEX, and after a whole
// buffer (`all`) the dump's line with the CRC-32 of all of it.
bool finish(uint8_t fmt, const uint8_t* buf, uint16_t total, bool all, Sink out);

}
#endif
