#!/usr/bin/env python3
import pathlib
import struct
import sys
import zlib

MAGIC = 0x544E4331
VERSION = 1
SEQUENCE = 1
EXPECTED_SIZE = 0x12000


def emit_array(out, name, section, data):
    out.write(f'__attribute__((used, section("{section}"), aligned(32)))\n')
    out.write(f'const uint8_t {name}[{len(data)}] = {{\n')
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        out.write('    ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',\n')
    out.write('};\n\n')


def main() -> int:
    if len(sys.argv) != 3:
        print(f'usage: {sys.argv[0]} INPUT OUTPUT_C', file=sys.stderr)
        return 2

    source = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])
    data = source.read_bytes()
    if len(data) != EXPECTED_SIZE:
        print(f'{source}: expected 0x{EXPECTED_SIZE:x} bytes, got 0x{len(data):x}',
              file=sys.stderr)
        return 1

    crc = zlib.crc32(data) & 0xffffffff
    header = struct.pack('<8I', MAGIC, VERSION, SEQUENCE, len(data), crc,
                         0xffffffff, 0xffffffff, 0xffffffff)
    slot_a = header + data

    # A zero header deliberately invalidates any old runtime B slot whenever
    # a complete firmware image is flashed.  This guarantees that the User RAM
    # compiled into the new firmware is the copy selected on the next boot.
    slot_b_marker = bytes(32)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('w', encoding='ascii') as out:
        out.write('#include <stdint.h>\n\n')
        out.write('/* Auto-generated from tnc155-user-ram.bin. */\n')
        emit_array(out, 'g_tnc155_nvram_factory_slot_a',
                   '.tnc155_nvram_factory_a', slot_a)
        emit_array(out, 'g_tnc155_nvram_factory_slot_b_marker',
                   '.tnc155_nvram_factory_b', slot_b_marker)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
