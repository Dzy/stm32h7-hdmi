# Live GDC VRAM scanout correction — 2026-09-06

Baseline: `tnc155-emulator-codex-baseline-fixedkeys-rom5-inverse-2026-09-06`.

## Observation

The inverse-video decoding is correct: `mode_data D0` reverses the complete
character cell.  On real startup, however, `MUISTIN TESTAUS` must remain
visible during the memory test.  In the emulator it only flashed immediately
before the axis display appeared.

## Cause

`uPD7220` writes updated what is now named `character_video_dram` immediately,
but `scanout_character_video` was only published when a later GDC command
terminated the current WDAT stream.  The frontend renders
`scanout_character_video`, so a long startup WDAT stream was invisible
until the next command.

This command-boundary pseudo-double-buffering is not hardware behavior.

## Fix

Every completed 16-bit WDAT word is now copied to
`scanout_character_video` immediately.
The copy happens only after both parameter bytes have arrived, preserving
word-level atomicity.  Existing full snapshots on display-enable/command
boundaries are retained as harmless synchronization points.

No mapper, RAM, keyboard, monoflop, watchdog timing, or inverse-video logic
was changed in this patch.

`test_upd7220` now asserts immediate scanout visibility before the next GDC
command.
