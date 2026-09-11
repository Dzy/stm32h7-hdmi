# TNC 155 memory evidence

This file separates physical memory documented by Heidenhain from logical
address decoding reconstructed from firmware. A service-manual block diagram
proves that a device exists and gives its capacity; it does not by itself prove
every CPU-visible address.

Canonical primary source: `605133_00_a_02.pdf`, the six-ROM Service Manual
TNC 151/155, section
4.1 and block diagrams on PDF pages 73 and 75. The burn-in descriptions are
on PDF pages 25-26 (printed pages 23-24).

Per project policy this six-ROM manual takes precedence over all other service
manuals if they conflict. (`605133_00_a_0g2.pdf` currently has an identical
SHA-256 digest, but it is not used as the authority.)

## Main Processor Board

| Emulator object | Physical evidence in service manual | Address evidence |
|---|---|---|
| P3 | IC-P3, 64K x 8, first 4K not mapped | Reset vectors and mapper use in P3 firmware |
| P4/P5 | IC-P4/P5, 2 x 64K x 8 | Mapper accesses in P3 firmware |
| P6 | IC-P6, 64K x 8, 8K used for PLC program | PLC execution and mapped reads in P3 firmware |
| `ram[0x2000]` | Q67 shared RAM, 8K x 8; accessible by MAIN and CLP; MAIN workspace | `E000..FFFF` is stated by PROCESSOR CHECK ERROR K and the non-mapped RAM burn-in test |
| `user_ram[0x12000]` | RAM -20.4-, 9 x 8K x 8; user programs, machine parameters and optionally PLC program | Exact mapper-page placement `EE..FF` is firmware-derived; the manual proves mapped RAM and the 72 KiB physical capacity, not those page codes |
| PLC operand memory | PLC RAM -23.2-, 8K x 8 with 4K x 1 used | CRU operand addressing is firmware-derived |

Mapper page `FD` is ordinary memory inside the contiguous 72 KiB User RAM
range `>EE000..>FFFFF`. With logical segment C mapped to `FD`, for example,
logical `>CFDE` translates to physical `>FDFDE` and therefore to
`user_ram[0x0FFDE]`. There is no FD-specific split TX/RX plane, checksum hook,
or transaction engine in the production model.

## CLP/Graphics Board

| Emulator object | Physical evidence in service manual | Address evidence |
|---|---|---|
| P2 | IC-P2, 64K x 8 | Reset vectors and direct execution in P2 firmware |
| `fast_ram[0x2000]` | RAM -30.2-, 8K x 8 wait-free | `A000..BFFF` is confirmed by the burn-in description and P2 firmware |
| `ram[0x2000]` | RAM -30.3-, 8K x 8, one wait cycle | `C000..DFFF` is the CLP operating-program RAM window |
| P1 | Character generator IC-P1, 16K x 8 | P1 address-bit organization is schematic/ROM-derived |
| `gate_array_registers[0x0700]` | CLP/graphics control logic | `F100..F7FF` is a separate register/gate-array decode; P2 uses `F200` as the IC31.1 page latch |
| GDC `character_video_dram[0x40000]` | DRAM video memory -31.6-, 4 x 64K x 8 | uPD7220 display memory used by both coded-character and pure-bitmap display modes; the observed P2 WDAT stream and user-verified text/inverse/brightness output use its first decoded 64 KiB bank; remaining physical bank selection is open |
| `graphics_dram[0x10000]` | Graphics/visualization work memory -31.1-, 2 x 64K x 4 = 64 KiB | P2 proves a 256-byte CPU aperture at `E800..E8FF`; an 8-bit page latch at `F200` supplies A15..A8, giving 256 pages. P2's `WORD >8000 == >5555` board probe selects the `F200` latch path. |
| `scanout_character_video` | No extra physical memory | Emulator-only coherent copy of the currently decoded -31.6- bank |


The fallback `shared_ram_storage` used by an isolated CLP-board unit test is
test backing only. In the complete machine the CLP view is attached to the
single physical Q67 object on the MAIN board.

## Established project-level firmware/hardware model

- CLP CRU bit `>1000` arbitrates the shared memory and asserts MAIN HOLD while
  CLP owns the shared window.  P2 brackets the shared-memory accesses with this
  bit, and the working machine contains no other shared-bus exclusion path.
- `mode_data D1` is the Bright/Dark intensity control with the current polarity;
  this behavior is user-verified on the physical TNC155.
- P2's visualization path programs pure graphics mode, 490 active lines and
  PRAM SAD values `>02C0` / `>42C0`; the emulator now renders those uPD7220
  display-memory partitions directly and executes FIGS/FIGD/GCHRD drawing.

## Still open

- Exact uPD7220 VSYNC/HBLANK timing and FIFO consumption timing. The 16-byte
  command/parameter FIFO itself is implemented with dynamic EMPTY/FULL status
  and ordered tagged entries, but its byte-consumption rate is intentionally
  scheduler-driven until the CLP/graphics board timer frequency is proven. The
  SYNC geometry is decoded and no guessed 50/60 Hz raster clock is used.
- IC31.6 display-memory bank selection beyond the first decoded bank.
- The complete CLP address decoder, including all aliases.
- The exact mapping of the 72 KiB RAM -20.4- to page codes `EE..FF`.

These remaining items stay labeled according to the strongest available
firmware, schematic, or physical-machine evidence.
