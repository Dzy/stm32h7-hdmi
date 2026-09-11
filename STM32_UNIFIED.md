# STM32 unified TNC155 firmware

This branch contains the STM32H7 board support and the TNC155 emulator sources
in one repository. No external TNC155 source checkout is required.

## Build

TNC155B (default):

```sh
make -f Makefile.tnc155 -j2
```

TNC155Q:

```sh
make -f Makefile.tnc155 -j2 TNC155_ROMSET=tnc155q BUILD_DIR=build_tnc155q
```

Use a separate build directory for each ROM set so generated ROM headers and
object files cannot be reused across variants.

## Flash

TNC155B:

```sh
make -f Makefile.tnc155 flash
```

TNC155Q:

```sh
make -f Makefile.tnc155 flash TNC155_ROMSET=tnc155q BUILD_DIR=build_tnc155q
```
