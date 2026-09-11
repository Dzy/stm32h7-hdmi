# TNC 155 emulator

Linux/C emulator for the TNC 155 MAIN and CLP processor boards.

The six canonical ROM images are converted into a generated C header during
the build. The resulting executable therefore has no run-time ROM-file
dependency. The original ROM files remain the authoritative bit-exact inputs.

Build and test:

```sh
CC=/usr/bin/gcc cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Public ROM access is provided by `include/tnc155/roms.h`. P1/P2 belong to the
CLP board; P3/P4/P5/P6 belong to the MAIN-board firmware set.

The emulator uses its own TMS9995 core. Its bus API distinguishes data reads,
opcode fetches (IAQ), writes and interrupt acknowledge cycles. This distinction
is required for the MAIN board's hardware PLC stepping aperture.
Both the MAIN and CLP TMS9995 processors are clocked at 12 MHz. The graphical
front end paces each processor from emulated CPU cycles, while the machine
scheduler keeps their two clocks concurrent.

Run the complete machine with its SDL video window. MAIN and CLP are scheduled
together and share RAM. The diagnostic overlay is off by default; F10 toggles
it, F2 operates the external machine START pushbutton (physical PLC input
E22), and Escape closes the window:

```sh
./build/tnc155
```

For automated or terminal-only execution:

```sh
./build/tnc155 --headless 1000000
```

Three ROM sets are selectable through `TNC155_ROMSET`: `tnc155b`, `tnc155q`
and `tnc155frank`.  TNC155FRANK uses the complete TNC155Q P1-P5 firmware and
Q P6 support firmware, but replaces P6's first 4096-byte PLC command area with
the 607-command HEIDENHAIN standard PLC program 23460102 documented in the
TNC 151B/Q and TNC 155B/Q PLC Description.  The remaining command area is
erased (`FF`). Its MOD-screen PLC identifier is `155FRANK`, and both P6
checksums are rebuilt. Start it with:

```sh
TNC155_ROMSET=tnc155frank ./build/tnc155
```

`tools/build_frank_rom.py` reproducibly assembles the checked-in standard PLC
listing and patches it into the unmodified Q P6 image.

P1 2340201A is decoded as 8 modes x 64 coded characters x 32 scan lines.
Its physical address is A13..A11=mode, A10..A5=6-bit character code and
A4..A0=scan line. P2 character cells provide ASCII plus mode_data; mode_data
D4..D2 select the eight P1 modes and ASCII D5..D0 select the 64 characters.
mode_data D0 is the post-font normal/inverse control. D1 selects the separate
bright/dark intensity output. D1's current active-high DARK behavior and
polarity are user-verified on the physical TNC155. The remaining CLP address
decoder is reconstructed from ROM and hardware evidence region by region.

The MAIN PLC execution view is stateful and separate from ordinary mapper
reads. A fetch at the `BL @>A000` target opens the aperture: fetches through
`>A000..>AFFE` clock P6 words into the one-bit PLC while the TMS9995 receives
NOPs. The active aperture supplies P6's `B *R11` word at `>B000` and then
closes. This preserves ordinary MAIN execution in the overlapping A9xx area.

MAIN work RAM and PLC program memory are separate physical resources.  The
fixed MAIN CPU window >E000..>FFFF is 8 KiB of work RAM.  Mapper values
>30..>3F refer instead to the PLC EPROM/RAM's 20-bit virtual range
>30000..>3FFFF; it does not alias MAIN work RAM.  The first page, >30000..
>30FFF, contains 2048 commands for the discrete one-bit PLC. The normal
execution aperture reads these commands directly from P6; no startup copy to
MAIN RAM is required. The `>30/>31` virtual view remains available for
service/data access to the same PLC EPROM content.

P3 selects the first PLC-program page through one of two mapper images.
`>F670` bit 0 set selects P6 page code `>30`; clear selects writable mapper
resource `>FF`.  `>FF` is the top 4 KiB page of the MAIN board's contiguous
72 KiB battery-backed mapped RAM (`>EE000..>FFFFF`); it is not a separate
invented device and its selection is controlled by TNC's machine parameters.
P6 checksum C1 covers physical
`>0000..>0FFF` and is stored at `>FFFE`; C2 covers `>1000..>FFFD` and is
stored at `>FFFF`.

The mapped RAM is persisted beside the emulator executable as
`tnc155b-user-ram.bin` or `tnc155q-user-ram.bin`, matching the selected ROM
set. If the writable image is absent or malformed, the emulator creates it
from that ROM set's embedded checksum-valid default image containing the
project's machine parameters. Changes made by the TNC's own machine-parameter
editor are saved on a clean emulator exit. Every build refreshes both runtime
images from the known-good source images.

Use `--blank-user-ram` only when intentionally testing the original empty
battery-RAM recovery path.  It bypasses both the embedded default and the
writable image, and does not overwrite the persistent image on exit.

The external machine-control keys are intentionally outside the P8279 panel
keyboard: F1 latches the external control-voltage relay on, F2 is the
momentary normally-open START contact (E22), and F3 is the momentary STOP
contact (opening normally-closed E23).  F2 and F3 do not alter the independent
emergency-stop contact or either processor watchdog.  F10 toggles diagnostics;
F12 remains the separate STOP key in the TNC panel keyboard matrix.

See `MEMORY_EVIDENCE.md` for the distinction between memories physically
documented in the TNC 155 service manual and logical address decoding inferred
from the ROMs. Emulator-only snapshots and test backing are identified there
and are not represented as additional TNC RAM devices.

The one-bit PLC core implements the manual-defined 16-bit command format:
four-bit operation code plus a 12-bit bit-memory address.  Implemented commands
are NOP, U, UN, O, ON, XO, XON, S, SN, R, RN and assignment.  Its 4096-bit
operand memory remains physically separate from the machine-I/O backplane.
The MAIN firmware transfers all 128 physical E inputs from CRU `>6400..>647F`
to PLC operands E0..E127, and transfers PLC operands A0..A63 back through CRU
`>6440..>647F`.  The emulator routes both sides through real TMS9995 STCR/LDCR
cycles; there is no hidden RAM alias or per-frame copy.  A regression test
covers every input and output bit, including E63/E127 overload feedback and
A31/A63 overload-reset strobes.  Marker, timer and keyboard-related PLC
semantics remain later integration work.

The MAIN-board P8279-5 keyboard/display controller is emulated at the
ROM-proven byte ports `>F788` (data) and `>F789` (status/command).  Its
eight-entry FIFO, status/error flags, command decoder, sensor RAM and display
RAM are modeled. The SDL frontend maps host keys to the TNC 155 P8279 raw
matrix codes recovered from the P3 key-translation table. Backspace or Delete
selects CE (raw `>7F`), Return is ENT (raw `>65`), F12 is STOP, P selects the P mode, cursor keys map
to the panel arrows, X/Y/Z/I select the axes (I = IV, raw `>7E`), and the host
numeric keypad maps to the corresponding panel digits. The raw matrix mapping
must be preserved: the P3 ROM performs a second translation from raw matrix
code to its internal logical-key code.

F1 is reserved for the machine-side control-voltage return/relay.  The
external +24 V source is permanently present at the TNC supply terminals.
Pressing F1 latches the machine return path on.  Further F1 presses leave it
on; they do not simulate loss of control voltage or emergency stop.  The TNC's
healthy-closed emergency-stop contact/output at J1/8 remains in series, so
the NC hardware can interrupt the feedback at E8/J5/8 during the documented
emergency-stop power-on test.

This package uses the verified replacement IC-P5 image
`rom-overrides/new_2340005E_nr5.bin` (SHA-256
`fed805fdecfeb9ae2f9b9533e0cb5840c1dd2ccbc0c3ebedc0dadd405ef87523`).
It produces the `>41` P5 checksum expected by the unmodified P3 firmware.

The CLP board contains an NEC uPD7220 device model driven by the command and
parameter protocol observed directly in P2. RESET/SYNC, START/STOP, PITCH,
CCHAR, PRAM, ZOOM, CURS, MASK and WDAT are decoded, and the complete FIGS
geometry state now feeds FIGD pixel/line/arc/rectangle drawing and GCHRD
graphics-character drawing into IC31.6 display RAM. The uPD7220 host path now
uses a functional 16-byte tagged FIFO with dynamic EMPTY/FULL status; exact
FIFO consumption timing and raster-clock timing remain separate device-level
work because the discrete board timer frequency has not yet been proven.

The video model keeps the two physical memory paths separate. IC31.1 is 64 KiB
of CLP graphics/visualization work RAM (two 64K x 4 devices), while IC31.6 is
the uPD7220 display/video DRAM (four 64K x 8 devices). P2 accesses IC31.1
through a ROM-proven 256-byte aperture at `>E800..>E8FF`; the gate-array page
latch at `>F200` supplies the upper eight address bits, giving 256 x 256-byte
pages. P2's board-revision probe reads `>5555` at word address `>8000` and
therefore selects `>F200` as the latch address.

The uPD7220 display RAM is used for both coded-character and bitmap display
data; IC31.1 is not scanned out as a framebuffer. P2's normal text setup uses
mixed mode, a 32-word pitch and 468 active lines. Its visualization task uses
SYNC parameter 1 `>16`, selecting pure graphics mode with 32 words = 512 pixels
per line and 490 active lines. The renderer decodes all four uPD7220 PRAM
partitions, honors BCTRL blanking, and exposes the P2-programmed 468/490 active
height instead of clipping graphics mode to 468 lines. The first normal display
partition is a 32 by 18 coded-character area with 16 by 26 character cells; P1 supplies eight glyph
bits and the external character path displays each bit for two pixel clocks.
the renderer obtains its glyph scan lines from the embedded P1 ROM and shifts
P1 D7 first through the board's external character shift register. P1 itself
is one bit deep. The character and graphics shift-register outputs feed the
board's normal/inverse logic; a separate Bright signal forms the other video
output bit, giving four possible output levels. The character-address latch's
six-line and three-line P1 paths, and its three-line connection to the 5-bit
scan-line counter, are being decoded from the hardware and live video words.

CLP CRU `>1000` is the shared-memory arbitration control that asserts MAIN
HOLD while the CLP owns the shared window. P2 brackets its shared-memory
accesses with this bit, and no independent competing arbitration mechanism is
present in the reconstructed machine.

### 2026-09-06 live VRAM scanout correction

Startup character writes are now visible to the renderer immediately after each
complete 16-bit WDAT word.  This fixes the case where the inverse `MUISTIN
TESTAUS` line existed in the emulated -31.6- character/video DRAM but was not
published to scanout until the
next command, making it flash only when the later axis screen appeared.  See
`CHANGES_LIVEVRAM_2026-09-06.md`.
