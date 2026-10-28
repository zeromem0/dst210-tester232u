# RS232 tester for the FNIRSI DST-210

Read an RS232 or TTL serial line **directly through the oscilloscope input**, without a USART or a
MAX3232. The tester displays the waveform, the characters aligned beneath the bits from which they
were derived, and the data stream in both hex and ASCII. What was received can be taken to a PC
over the instrument's own USB socket, which then works as a serial port (USB CDC).

The tester runs **next to the FNIRSI firmware**, which stays as it is. Held at power-on, RUN starts
the tester. Without it, the unit starts the FNIRSI firmware as before.

Every screen, with a made-up line as data: [`docs/screens.mp4`](docs/screens.mp4) (one minute),
[`docs/screens.gif`](docs/screens.gif), or the pictures one by one in [`docs/screens`](docs/screens).

> **Note:** this is [T232USB](https://github.com/zeromem0/dso152-tester232-usb), the same tester
> for the FNIRSI DSO-152, carried over to the DST-210. Its beginnings go back to David's
> [dso152-breakout](https://github.com/davidgiven/dso152-breakout).

## What it does

- **RS232 Tester:**
  - RS232 or TTL levels.
  - 9600 8N1, 19200 8N1, or speed and frame worked out from the line, up to 115200 baud.
  - Views:
    - protocol: the waveform with the decoded characters under it;
    - single character: one frame drawn bit by bit, with the levels, the threshold and where each bit was read;
    - bus health: levels, bit rate, rise and fall times and jitter against the EIA-232 limits;
    - large: the last characters in big type;
    - hex;
    - characters.
  - **SNAP** waits for the first start bit and keeps the message, so that the first exchange on a
    line can be looked at.
- **USB Sender:** sends the received buffer to the PC as raw bytes, or as a hex dump with a CRC-32
  per block. The screen shows the CRC-32 of what is sent, to be compared with the one computed on
  the PC.
- **Settings:** colour theme (eight of them), export format, block size, auto off (off, 10 or
  30 minutes), and erasing the buffer. They hold until the unit is switched off.

Zero volts on each range comes from the unit's own FNIRSI calibration. The tester reads it from
flash and never writes to the flash.

## Connecting

- **Probe tip** on the line to be read (TX or RX), **probe ground** on the signal ground of the
  line. Probe switch on ×1.
- Range **R8** (5 V a division) suits RS232 levels, R5–R6 suit TTL. ◄ ► change it.
- ENTER switches between RS232 (rests negative) and TTL (rests positive).

## Buttons

All the buttons on one printable page: [`docs/buttons.html`](docs/buttons.html)
([view it rendered](https://htmlpreview.github.io/?https://github.com/zeromem0/dst210-tester232u/blob/main/docs/buttons.html)).

| Button | Press | What it does |
|---|---|---|
| RUN + POWER | at power-on | Starts the tester. POWER alone starts the FNIRSI firmware. |
| POWER | held 2 s | Switches the unit off, from anywhere in the tester |
| ▲ ▼ | short | Start menu: RS232 Tester, USB Sender, Settings |
| ENTER, RUN | short | Start menu: start the one chosen |

In the **RS232 Tester**:

| Button | Press | What it does |
|---|---|---|
| RUN | short | `RUN` → `HOLD` → capture (`WAIT`, then `SNAP`); in `WAIT`, back to `RUN` |
| MODE | short | View: protocol → single character → bus health → large → hex → characters |
| MODE | held | In `WAIT` only: a simulated message, received as if from the line |
| ◄ ► | short | Range down / up |
| ▲ ▼ | short | Sample rate up / down: 100 k, 250 k, 500 k, 1.25 M, 2 M samples/s |
| AUTO | short | Speed: 9600 8N1 → 19200 8N1 → worked out from the line (`A` in the bar) |
| ENTER | short | RS232 ↔ TTL |
| ENTER | held | Back to the menu |
| POWER | short | Learn the speed and the frame again |

- **Single character** view: ▲ ▼ move the threshold by hand, and AUTO held puts it back in the
  middle of the levels. On a capture or in `HOLD`, ◄ ► step through the characters under the
  waveform instead of changing the range.
- **Bus health** view: RUN starts the figures again.

Sample rate for each speed: 9600 at 100 k, 19200 at 250 k, 38400 at 250 k or 500 k, 57600 at
500 k, 115200 at 1.25 M. At 2 M the screen still shows the line, but the decoder no longer keeps up.

In **USB Sender**, ▲ ▼ choose the whole buffer or a single block, RUN sends (RUN again stops), and
MODE returns to the menu. In **Settings**, ▲ ▼ choose the row and ◄ ► change the value; on the
Buffer row, AUTO held erases the buffer.

## Making the update file

The FNIRSI firmware is not part of this repository and may not be passed around: everyone takes
it from FNIRSI. `tools/make_update.py` puts it together with the tester into one file for the
FNIRSI USB update. It needs Python 3; nothing else.

1. Download the DST-210 firmware from the FNIRSI website (support / downloads, DST-210). The tester
   was tried with **V0.1.0** (`DST-210_Firmware_V0.1.0.zip`, containing
   `APP_DST-210_V0.1.0_251030.bin`).
2. Make the update file from the zip, either with the ready-made tester:

   ```
   python tools/make_update.py DST-210_Firmware_V0.1.0.zip bin/T232-DSTu_v0.33.bin
   ```

   or, after a build of your own (see below), with that build:

   ```
   python tools/make_update.py DST-210_Firmware_V0.1.0.zip
   ```

The result is `update/APP_DST-210_V0.1.0_251030.bin`, under the same name as the FNIRSI file. Of
the FNIRSI application only one word changes: the address through which its start-up code jumps
to its main program now points to the tester. The tester checks RUN, then goes on to the FNIRSI
program, or starts itself. The tester is placed after the FNIRSI application, from `0x0806B000`.

**Another FNIRSI version.** The script finds that jump in any version, checks the file, and
tells whether the version is one the tester has been tried with. Two things may need adapting:

- **A larger FNIRSI application.** If it reaches past `0x0806B000`, the script stops and gives
  the new address. Set it in `platformio.ini` (`board_build.flash_offset` and
  `board_upload.offset_address`), build again, and run the script again.
- **The calibration.** The tester takes the zero of each range from the FNIRSI settings page
  (`0x08006000`), in the layout of V0.1.0. If a later version lays it out differently, the
  tester's checks reject it and every range uses the same estimated zero
  (`src/app/Frontend.cpp`, `calLoad()`). The traces then sit a little off centre; decoding still
  works.

A version that has not been tried is at your own risk. The way back is always open, see below.

## Installing through the FNIRSI update

1. Switch the unit off and connect it to a Windows 10/11 PC with a USB **data** cable. FNIRSI
   warns that macOS can damage the unit during an update.
2. **Hold MODE and switch it on.** The screen shows *Firmware Upgrade* and a USB drive named
   *DST-210* appears.
3. Copy `APP_DST-210_V0.1.0_251030.bin` into the folder **Upgrade file** on that drive. The unit
   switches itself off when it is done.
4. **Hold RUN and switch it on:** the tester's menu. Switched on normally, the unit starts the
   FNIRSI firmware.

**Going back:** the same steps with the FNIRSI file as downloaded. The FNIRSI bootloader and its
settings page with the calibration are never written by the tester or by the update file.

## Receiving the buffer on the PC

Open **USB Sender** on the instrument, then on the PC run (needs `pip install pyserial`):

```
python tools/usb_receive.py --list
python tools/usb_receive.py COM12
```

and press RUN on the instrument. On Windows the instrument appears as *USB Serial Device (COMx)*,
with the driver included in Windows; on Linux as `/dev/ttyACM0`. The script stops two seconds
after the data ends and writes `received.bin`. A hex dump is checked against its CRC-32 lines and
also kept as text (`received.txt`). The CRC-32 the script prints must match the one on the
instrument's screen. The instrument sends only to a port that the PC holds open (DTR set); the
script sets it.

## Building

You need [PlatformIO](https://platformio.org/): the VS Code extension, or the command line
(`pip install platformio`). The platform (`ststm32@19.0.0`) and the library (Adafruit GFX 1.12.6)
are pinned in `platformio.ini` and are downloaded on the first build.

```
git clone https://github.com/zeromem0/dst210-tester232u.git
cd dst210-tester232u
pio run
```

The result is `.pio/build/flash_t232/firmware.bin`, the tester alone, linked at `0x0806B000`;
`tools/make_update.py` uses it by default. It cannot be uploaded with a debugger: the unit's flash
is read-protected, and the only way in is the FNIRSI update.

## Notes on the hardware

- **Processor:** the DST-210 uses an Artery AT32F403A (1 MB flash, Cortex-M4). The build treats it
  as an STM32F103VE, as the DSO-152 projects treat their CH32F103. The parts that differ are set
  by register in `src/hw/Probe.cpp`:
  - the clock: 240 MHz from the 8 MHz crystal, reached only once the screen is up;
  - the USB clock, from the internal 48 MHz oscillator, as in the FNIRSI firmware.
- **Screen:** an ST7789 on the external memory bus (`src/hw/Lcd.cpp`).
- **Converter:** 8 bits read from GPIOB by DMA, clocked by a timer (`src/app/Acquire.cpp`).
- **Front end:** a relay, a 74HC4051 and a PWM offset, switched as the FNIRSI firmware does
  (`src/app/Frontend.cpp`).

Real RS232 lines have been received at 9600 through 115200 baud, and sent over USB to the PC
unchanged.
