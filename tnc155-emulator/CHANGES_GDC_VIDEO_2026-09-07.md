# uPD7220 / video update — 2026-09-07

This change replaces several shortcuts in the TNC155 video path with behavior
used by the P2 visualization firmware and by the NEC uPD7220 programming model.

## Implemented

- FIGS now decodes the full DIR/TYPE, DC, D, D2, D1 and DM parameter set.
- FIGD executes pixel, line, arc and rectangle drawing into IC31.6 display RAM.
- GCHRD executes graphics-character/area-fill drawing for figure type 2.
- WDAT now keeps the full 16-bit graphics pattern and honors transfer type,
  mask and replace/complement/reset/set modification modes; the old
  graphics-path single-bit-to-FFFF shortcut was removed.
- The drawing engine uses the uPD7220 direction vectors and mask rotation.
- PRAM display areas are decoded as four 4-byte partitions rather than using
  only PRAM 0..3 unconditionally.
- Pure-graphics scanout uses the ROM-proven SAD/LEN page descriptions, including
  P2 visualization page 0 (`SAD >02C0`, 490 lines) and page 1 (`SAD >42C0`).
- BCTRL blank/unblank now gates the rendered video output.
- The video backing frame is 512 x 490. `tnc155_video_active_height()` reports
  the P2-programmed active height (468 in normal text/mixed mode, 490 in pure
  graphics mode). The SDL frontend follows that height when available.
- Drawing-in-progress status is now stateful: a completed synchronous FIGD or
  GCHRD is reported busy for the next status sample and then clears.

## Intentionally still open

- Exact VSYNC/HBLANK raster timing. The P2 timing words are decoded, but the
  discrete board timer / uPD7220 clock frequency has not yet been proven. The
  emulator therefore does not invent a 50/60 Hz clock; VSYNC remains asserted
  for the existing firmware-ready polling path until that hardware timing is
  decoded.
- Cycle-accurate FIFO timing remains open. A later 2026-09-07 update adds the
  functional 16-byte tagged host FIFO and dynamic EMPTY/FULL status; only its
  exact hardware consumption rate remains unproven. See
  `CHANGES_GDC_FIFO_2026-09-07.md`.
- IC31.6 physical bank selection beyond the first decoded 32K x 16 display
  bank. IC31.1 banking is already separate and ROM-proven.
- RDAT/DMA/light-pen behavior not used by the currently reached P2 path.

## Regression coverage

`test_upd7220` now exercises FIGD, GCHRD and drawing-in-progress status.
`test_video` exercises BCTRL blanking, 490-line graphics height, and the P2
visualization PRAM page starting at `>02C0` in addition to the existing
D0 inverse / D1 brightness tests.
