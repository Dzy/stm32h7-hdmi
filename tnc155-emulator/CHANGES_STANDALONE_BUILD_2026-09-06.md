# Standalone build fix — 2026-09-06

The previous F1 emergency-stop archive still inherited an absolute/host-relative ROM path from the development handoff:

`../TNC155_COMPLETE_HANDOFF_2026-09-01/01_ROMS`

That made a clean checkout fail while generating `generated/tnc155_rom_images.h`.

This package now contains the canonical P1, P2, P3, P4 and P6 ROM images under `roms/` and CMake defaults `TNC155_ROM_DIR` to that directory. The verified P5 remains under `rom-overrides/new_2340005E_nr5.bin`.

No emulation behavior was changed by this build fix.
