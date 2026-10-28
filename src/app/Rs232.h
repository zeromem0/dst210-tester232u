// Recovering an RS232 byte stream from ADC samples of the BNC input, with
// nothing told to it in advance: neither the speed nor the frame shape.
//
// Speed comes from the shortest run of samples at one level. In 8N1 traffic
// that is one bit time, and a start bit on its own already provides one, so
// a single character is often enough.
//
// The frame shape is settled by elimination. Every candidate -- seven or
// eight data bits, no parity or even or odd, one or two stop bits -- is
// checked against each character seen, and any whose stop bit lands on a
// space, or whose parity does not add up, is struck out. What survives in
// order of preference is the answer. The order matters for one case only:
// with the line idle between characters a second stop bit cannot be told
// from idle, so one stop bit is preferred, and two is reported only when the
// traffic is back to back and the longer frame is the one that fits.
//
// Levels: a line at rest sits at mark, the negative one. Through a non
// inverting front end that is the lower count, so `inverted` is true by
// default and a logical one is a count below the threshold.

#ifndef RS232_H
#define RS232_H

#include <stdint.h>

namespace Rs232 {

struct Stats {
    uint16_t lo, hi;
    uint16_t threshold;
    uint32_t baud;              // 0 until enough edges have been seen
    uint32_t bitSamples;        // samples per bit, Q8
    uint32_t bytes;
    uint32_t framingErrors;
    uint32_t parityErrors;
    uint32_t overruns;          // halves of samples that went by undecoded
    uint32_t dropped;           // characters decoded but not collected in time
    bool     locked;            // the bit time is known
    bool     levels;            // lo and hi are a signal's, not one block's extremes
};

struct Format {
    uint8_t dataBits;           // 7 or 8
    char    parity;             // 'N', 'E' or 'O'
    uint8_t stopBits;           // 1 or 2
    bool    decided;            // enough characters agreed on it
    uint8_t framesSeen;
};

const uint8_t MARKS_MAX = 80;
const uint8_t BYTES_MAX = 24;

void  begin();
void  reset();                  // forget the speed and the shape, learn again
// A speed given outright, read as 8N1 with nothing learnt and nothing to get
// wrong, or 0 to work the speed and the shape out from the line. It starts
// at 9600. Follow a change with reset().
void     setFixedBaud(uint32_t baud);
uint32_t fixedBaud();

void  setInverted(bool inv);
bool  inverted();

// The level the line is split at is normally the middle of the two levels
// learnt. Given here it is used instead, and the line is read with it even
// before any levels have been learnt, which helps a signal too small or too
// one-sided to be taken for traffic. 0 goes back to the middle; reset() does
// too, since a level in counts means nothing on another range.
void     setThreshold(uint16_t counts);
uint16_t manualThreshold();     // 0: automatic
uint16_t zeroCounts();          // where zero volts sits on the converter

void  feed(const uint16_t* s, uint16_t n, uint32_t sampleHz);
// The same, with the block's lowest and highest sample already known.
void  feed(const uint16_t* s, uint16_t n, uint32_t sampleHz, uint16_t lo, uint16_t hi);
void  noteOverrun();
// The counts start again from nothing: with the buffer erased.
void  clearCounts();

int   available();
int   read();

Stats  stats();
Format format();

// The marks and bytes below describe one feed, and the next feed replaces
// them. With feed() running in an interrupt the screen cannot read them as
// they are, so latchView() copies the set just produced and the accessors
// read that copy. Call it right after the feed whose samples are drawn.
void     latchView();

// Where the first start bit of the last feed fell, or -1 if it had none.
int32_t  firstStart();

// Reads a stretch of samples that has been kept, from a line taken to be at
// rest where it begins, for the marks and bytes only: nothing is queued and
// nothing is counted. Returns how many of the samples it takes to show the
// first maxBytes characters, and latches the marks and bytes within that.
// Not while feed() may run.
uint16_t review(const uint16_t* s, uint16_t n, uint8_t maxBytes);

// The two levels taken from a capture, which holds a message: its lowest and
// highest sample. Used before review(), so that a capture is read with levels
// that come from itself, whatever the line did before it.
void levelsFrom(const uint16_t* s, uint16_t n);

uint8_t  markCount();
uint16_t markAt(uint8_t i);

uint8_t  byteCount();
uint16_t byteAt(uint8_t i);
uint8_t  byteVal(uint8_t i);

}
#endif
