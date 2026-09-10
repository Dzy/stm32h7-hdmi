#!/usr/bin/env python3
import hashlib
import pathlib
import sys

EXPECTED_SIZE = 0x12000


def write_header(data: bytes, output: pathlib.Path, source: pathlib.Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(data).hexdigest()
    with output.open("w", encoding="ascii") as out:
        out.write("#ifndef TNC155_DEFAULT_USER_RAM_H\n")
        out.write("#define TNC155_DEFAULT_USER_RAM_H\n\n")
        out.write("#include <stddef.h>\n#include <stdint.h>\n\n")
        out.write(f"/* Source: {source.name}; SHA256: {digest} */\n")
        out.write("static const uint8_t tnc155_default_user_ram[] = {\n")
        for offset in range(0, len(data), 16):
            chunk = data[offset:offset + 16]
            out.write("    " + ", ".join(f"0x{value:02x}" for value in chunk))
            out.write(",\n")
        out.write("};\n")
        out.write("static const size_t tnc155_default_user_ram_size =\n")
        out.write("    sizeof(tnc155_default_user_ram);\n\n#endif\n")


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT_USER_RAM OUTPUT_HEADER", file=sys.stderr)
        return 2

    source = pathlib.Path(sys.argv[1])
    output = pathlib.Path(sys.argv[2])
    data = source.read_bytes()
    if len(data) != EXPECTED_SIZE:
        raise RuntimeError(f"User RAM size {len(data)} != {EXPECTED_SIZE}: {source}")

    write_header(data, output, source)
    print(f"Embedded User RAM: {source} SHA256={hashlib.sha256(data).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
