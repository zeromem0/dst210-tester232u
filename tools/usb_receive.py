#!/usr/bin/env python3
"""Takes what the tester's USB Sender sends and keeps it.

    python tools/usb_receive.py PORT [OUT]        (default received.bin)
    python tools/usb_receive.py --list

Opens PORT (the DST-210's USB serial port, COMx on Windows, /dev/ttyACM0 on
Linux), waits for RUN on the unit, and stops two seconds after the last byte.
A hex dump (Settings, Export) is read back into bytes, checked against its
CRC-32 lines and saved as OUT, with the dump as it came next to it (.txt).
Raw is saved as it came. Either way the CRC-32 of the bytes is printed, to be
compared with the one on the unit's screen. Nothing is sent to the unit.

Needs pyserial (pip install pyserial).
"""
import os, re, sys, time, zlib


def receive(port):
    import serial
    got = bytearray()
    with serial.Serial(port, 115200, timeout=0.2) as p:
        p.dtr = True                    # the unit sends only to an open port
        print('%s open: press RUN on the unit (Ctrl+C to give up)' % port, flush=True)
        last = None
        while True:
            b = p.read(8192)
            if b:
                if last is None:
                    print('receiving...', flush=True)
                got += b
                last = time.time()
            elif last and time.time() - last > 2:
                return bytes(got)


def from_dump(text):
    """The bytes of a hex dump and its CRC-32 lines (length, crc)."""
    data = bytearray()
    crcs = []
    base = None                         # a single block starts where it was in the buffer
    for line in text.splitlines():
        m = re.match(r'^([0-9a-f]{8}):((?: [0-9a-f ]{4}){8})  ', line)
        if m:
            off = int(m.group(1), 16)
            if base is None:
                base = off
            if off != base + len(data):
                raise SystemExit('dump: line at %08x, expected %08x' % (off, base + len(data)))
            data += bytes.fromhex(m.group(2).replace(' ', ''))
            continue
        m = re.match(r'^# total (\d+) bytes crc32 ([0-9A-F]{8})$', line)
        if m:
            crcs.append((int(m.group(1)), int(m.group(2), 16)))
    # one block alone has no total; its own CRC line stands for it
    if not crcs and base is not None:
        m = re.findall(r'^# crc32 ([0-9A-F]{8})\r?$', text, re.M)
        if len(m) == 1:
            crcs.append((len(data), int(m[0], 16)))
    return bytes(data), crcs


def main(args):
    if args[:1] == ['--list']:
        from serial.tools import list_ports
        for p in list_ports.comports():
            print('%-14s %s' % (p.device, p.description))
        return
    if not args or args[0].startswith('-'):
        raise SystemExit(__doc__)
    out = args[1] if len(args) > 1 else 'received.bin'
    got = receive(args[0])
    if got.startswith(b'# DST-210 block'):
        text = got.decode('latin-1')
        data, crcs = from_dump(text)
        crc = zlib.crc32(data)
        ok = bool(crcs) and all(n == len(data) and c == crc for n, c in crcs)
        open(os.path.splitext(out)[0] + '.txt', 'w', newline='').write(text)
        open(out, 'wb').write(data)
        print('%d bytes, crc32 %08X, %s' % (len(data), crc,
              'matches the dump' if ok else 'DOES NOT MATCH the dump %s' % crcs))
    else:
        open(out, 'wb').write(got)
        print('%d bytes, crc32 %08X' % (len(got), zlib.crc32(got)))
    print('saved', out)


if __name__ == '__main__':
    main(sys.argv[1:])
