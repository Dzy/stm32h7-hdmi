#!/usr/bin/env python3
import pathlib
import sys

ITCM = r'''
  /* TNC155 TMS9995 hot code: copied from flash to 64 KiB ITCM at boot. */
  .itcm_tms9995 :
  {
    . = ALIGN(32);
    __tnc155_itcm_start__ = .;
    KEEP(*tms9995.o(.text .text.*))
    . = ALIGN(32);
    __tnc155_itcm_end__ = .;
  } >ITCMRAM AT> FLASH
  __tnc155_itcm_load__ = LOADADDR(.itcm_tms9995);

'''

DTCM = r'''
  /* TMS9995 compiler-generated lookup/jump/constant data is kept in DTCM. */
  .dtcm_tms9995 :
  {
    . = ALIGN(32);
    __tnc155_dtcm_start__ = .;
    KEEP(*tms9995.o(.rodata .rodata.* .data .data.*))
    . = ALIGN(32);
    __tnc155_dtcm_end__ = .;
  } >DTCMRAM AT> FLASH
  __tnc155_dtcm_load__ = LOADADDR(.dtcm_tms9995);

  .dtcm_tms9995_bss (NOLOAD) :
  {
    . = ALIGN(32);
    __tnc155_dtcm_bss_start__ = .;
    KEEP(*tms9995.o(.bss .bss.* COMMON))
    . = ALIGN(32);
    __tnc155_dtcm_bss_end__ = .;
  } >DTCMRAM

'''


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT.ld OUTPUT.ld", file=sys.stderr)
        return 2
    source = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])
    text = source.read_text(encoding="utf-8")

    code_marker = "  /* The program code and other data goes into FLASH */"
    ro_marker = "  /* Constant data goes into FLASH */"
    if code_marker not in text or ro_marker not in text:
        raise SystemExit("unexpected STM32H743 linker script layout")

    text = text.replace(code_marker, ITCM + code_marker, 1)
    text = text.replace(ro_marker, DTCM + ro_marker, 1)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
