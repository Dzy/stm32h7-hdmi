# IC31.1 graphics work-RAM banking - 2026-09-07

This change implements the P2-ROM-derived IC31.1 CPU access path without
inventing a 2 KiB linear framebuffer window.

## ROM evidence implemented

P2 initialization at `>5B4C` performs the board-revision probe:

```text
LI    R3,>F200
C     @>8000,@>35F0     ; ROM >35F0 = >5555
JEQ   use_F200
LI    R3,>8000
MOV   R3,@>A324
```

The TNC155 target therefore returns `>5555` at word address `>8000`, causing
unmodified P2 to store `>F200` in `>A324` as the IC31.1 page-latch address.
A regression test executes these actual P2 instructions and verifies that
`>A324` becomes `>F200`.

The downloaded graphics code then writes an 8-bit page number through the
address stored at `>A324` and accesses IC31.1 at `>E800`.  Its exhaustive RAM
loop selects pages `00..FF` and processes 256 bytes per page, proving:

```text
IC31.1 physical address = (page << 8) | (CLP_address & >00FF)
page latch              = >F200 on this board revision
CPU aperture            = >E800..>E8FF
capacity                = 256 pages x 256 bytes = 64 KiB
```

Because a TMS9995 word write to `>F200` is two byte bus cycles, the gate-array
latch is modeled as ignoring A0: writes to either `>F200` or `>F201` update the
same 8-bit page latch.  The second cycle of `MOV Rn,*R4` therefore leaves the
low byte, the page number, selected.

`>E900..>EFFF` is deliberately left unmapped.  The ROM proves the 256-byte
aperture; this change does not infer a larger alias.

## Video-path correction

IC31.1 is CLP graphics/visualization work RAM, not the uPD7220 scanout
framebuffer.  Pure graphics scanout now reads the uPD7220 display/video RAM,
the same GDC-owned memory used for coded-character display data in the other
mode.

The P2 visualization task enters pure graphics mode with SYNC command `>0E`
and parameter 1 `>16`.  The local uPD7220 model now decodes SYNC mode/timing
parameters and BCTRL `>0C/>0D` blank/unblank control.  The ROM graphics SYNC
set decodes to 512 pixels per line and 490 active lines.

Full FIGD/GCHRD drawing execution is still separate uPD7220 work; this change
does not claim those commands are complete.

## Validation

- Clean build uses only `build`.
- All 13 CTest tests pass.
- `tnc155-headless 1000000` completes with MAIN unmapped reads = 0 and CLP
  unmapped reads = 0.
- New regressions cover:
  - `>8000` hardware signature,
  - execution of the actual P2 `>5B4C` probe,
  - 256 x 256-byte IC31.1 page mapping,
  - `>F200/>F201` page-latch bus behavior,
  - `>E900` remaining unmapped,
  - P2 graphics SYNC `>16` -> graphics mode / 490 active lines,
  - BCTRL blank/unblank,
  - graphics scanout using GDC display RAM rather than IC31.1.
