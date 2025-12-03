# stm32h7-hdmi

Firmware project targeting STM32H7 hardware with HDMI output and LVGL integration.

## Build

The project uses the provided `Makefile` and the Arm GNU toolchain.

1. Install `arm-none-eabi-gcc`, `arm-none-eabi-binutils`, and related build tools.
2. Run `make` from the repository root to build the firmware. Artifacts are placed in `build/`.
3. Use `make clean` to remove generated objects and images.

## Flash

After building, flash the generated binary with STM32CubeProgrammer:

```sh
make flash GCC_PATH=/path/to/gcc-arm-none-eabi-
```

The `GCC_PATH` override is optional if the toolchain is already on your `PATH`.
