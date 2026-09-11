# STM32H7 TNC155 firmware port

This branch makes the TNC155 serial-keyboard protocol usable without POSIX and
is paired with `Dzy/stm32h7-hdmi:tnc155-tinyusb-cdc`.

## Architecture

- STM32H743 at the existing board clock configuration.
- Existing SDRAM + LTDC + TDA998x HDMI path, 1920x1080p60 L8/RGB332.
- TNC155 video is rendered at its native 512 x 468/490 size and centered 1:1.
- The emulated display phosphor uses amber levels; the debug overlay remains
  white on black.
- TinyUSB 0.21.0 host stack, pinned to
  `dae3f9a366bfcddbf9dcf1b48d7500286a849539`.
- USB Host CDC ACM only; USB HID keyboard/mouse is not used by the TNC build.
- CDC line coding is 115200 8N1 with DTR/RTS asserted on enumeration.
- Received `KDxxx`, `KU`, CR/LF and NUL-delimited messages are passed directly
  to `tnc155_serial_keyboard_feed()`, which feeds the emulated P8279 keyboard.
- MAIN and CLP are paced against the original 12 MHz emulated clock in short
  instruction slices so `tuh_task()` is serviced continuously.
- Physical machine I/O, emergency-stop feedback and virtual-axis mechanics are
  serviced from the STM32 1 kHz SysTick. Each tick advances the mechanical
  model by exactly 12000 TMS9995 input-clock periods (1 ms at 12 MHz). These
  services are therefore absent from the per-instruction CPU hot path.
- CRU-visible CPU bus accesses remain synchronous with the emulated TMS9995;
  SysTick owns only the derived physical wiring, watchdog feedback and mechanics.
- The TMS9995 core object is linked into Cortex-M7 ITCM for instruction fetch;
  its object-local constant/data sections use DTCM. The remainder of the
  machine model remains in cacheable SDRAM.
- The supplied 72 KiB TNC User RAM image is compiled into the firmware as
  ordinary read-only data and copied to `main.user_ram[]` in SDRAM at every
  boot. There is no STM32 internal-flash NVRAM persistence path.

## Checkout and build

Place both repositories beside each other:

```sh
git clone https://github.com/Dzy/tnc155-emulator.git
git clone https://github.com/Dzy/stm32h7-hdmi.git

git -C tnc155-emulator switch stm32-portable-serial
git -C stm32h7-hdmi switch tnc155-tinyusb-cdc

cd stm32h7-hdmi
make -f Makefile.tnc155 -j TNC155_DIR=../tnc155-emulator
```

TinyUSB is downloaded automatically and checked against the pinned commit.
The build outputs are:

- `build_tnc155/STM32H7_TNC155.elf`
- `build_tnc155/STM32H7_TNC155.hex`
- `build_tnc155/STM32H7_TNC155.bin`

Flash with STM32CubeProgrammer CLI:

```sh
make -f Makefile.tnc155 flash TNC155_DIR=../tnc155-emulator
```

## Bring-up diagnostics

The firmware renders a live debug panel in the upper-left corner of the HDMI
frame, outside the centered native TNC155 image. It shows MAIN and CLP PC/WP/ST,
CPU cycle counters, emulation lag in milliseconds, HAL tick, USB CDC mount state,
keyboard KD/KU/unmapped/duplicate counts, fault/swap/framebuffer state, and DWT
slice/frame timing. The overlay continues refreshing after an emulation fault so
a frozen emulated PC can be distinguished from a stopped STM32 superloop.

The firmware also exposes these symbols for SWD/GDB inspection:

- `g_tnc155_faulted`
- `g_tnc155_last_slice_core_cycles`
- `g_tnc155_max_slice_core_cycles`
- `g_tnc155_last_frame_core_cycles`

They are intended to establish whether the 480 MHz Cortex-M7 has sufficient
real-time margin for the two 12 MHz TMS9995 emulations plus video rendering.
A red screen denotes a HAL/application initialization failure. A magenta screen
denotes an emulator/runtime failure.

## Validation status

CI builds and tests the desktop emulator and cross-compiles the STM32H743
firmware. Physical USB enumeration, keyboard input, HDMI output and real-time
margin must still be validated on the target board.
