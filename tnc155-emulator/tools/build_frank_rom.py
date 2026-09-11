#!/usr/bin/env python3
"""Build a TNC155 FRANK P6 from a B or Q support ROM plus PLC 23460102."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


OPCODES = {
    "NOP": 0x0,
    "U": 0x1,
    "UN": 0x2,
    "O": 0x3,
    "ON": 0x4,
    "XO": 0x5,
    "XON": 0x6,
    "S": 0x7,
    "SN": 0x8,
    "R": 0x9,
    "RN": 0xA,
    "=": 0xB,
}

DISPLAY_NAME = b"155FRANK "


def parity_even(value: int) -> bool:
    parity = 0
    while value:
        parity ^= value & 1
        value >>= 1
    return parity == 0


def tnc_crc(data: bytes) -> int:
    accumulator = 0x2800
    for byte in data:
        accumulator ^= byte << 8
        if parity_even(((accumulator >> 8) & 0xFF) & 0xCA):
            accumulator = (accumulator + 0x80) & 0xFFFF
        accumulator = (accumulator << 1) & 0xFFFF
    return (accumulator >> 8) & 0xFF


def operand_address(token: str) -> int:
    kind = token[0]
    number = int(token[1:])
    if kind == "M" and 0 <= number <= 0xCCF:
        return number
    if kind == "E" and 0 <= number <= 127:
        return 0xCD0 + number
    if kind == "A" and 0 <= number <= 63:
        return 0xE50 + number
    if kind == "T" and 0 <= number <= 31:
        return 0xFA0 + number
    if kind == "T" and 48 <= number <= 79:
        return 0xFD0 + number - 48
    raise ValueError(f"unsupported operand {token}")


def assemble(source: Path) -> bytes:
    words: list[int] = []
    for line_number, raw_line in enumerate(source.read_text().splitlines(), 1):
        line = raw_line.partition("#")[0].strip()
        if not line:
            continue
        fields = line.split()
        if len(fields) != 3:
            raise ValueError(f"{source}:{line_number}: expected INDEX OPCODE OPERAND")
        index = int(fields[0])
        if index != len(words):
            raise ValueError(f"{source}:{line_number}: expected command {len(words):04d}")
        opcode = OPCODES[fields[1].upper()]
        operand = operand_address(fields[2].upper())
        words.append((opcode << 12) | operand)
    if len(words) != 607:
        raise ValueError(f"expected 607 manual commands, got {len(words)}")
    program = b"".join(word.to_bytes(2, "big") for word in words)
    return program + b"\xFF" * (0x1000 - len(program))


def build_frank_image(source: Path, base_p6: bytes) -> bytes:
    """Preserve the selected P6 support firmware and replace only its PLC image."""
    if len(base_p6) != 0x10000:
        raise ValueError(f"expected a 65536-byte P6, got {len(base_p6)} bytes")
    image = bytearray(base_p6)
    image[:0x1000] = assemble(source)
    image[0x3900:0x3909] = DISPLAY_NAME
    image[0xFFFE] = tnc_crc(image[:0x1000])
    image[0xFFFF] = tnc_crc(image[0x1000:0xFFFE])
    assert image[0x1000:0x3900] == base_p6[0x1000:0x3900]
    assert image[0x3909:0xFFFE] == base_p6[0x3909:0xFFFE]
    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("base_p6", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    image = build_frank_image(args.source, args.base_p6.read_bytes())
    args.output.write_bytes(image)
    print(f"wrote {args.output} ({len(image)} bytes)")
    print(f"sha256={hashlib.sha256(image).hexdigest()}")
    print(f"C1={image[0xFFFE]:02X} C2={image[0xFFFF]:02X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
