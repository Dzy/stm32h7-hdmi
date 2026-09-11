#!/usr/bin/env python3
import pathlib
import sys


def emit_array(out, name: str, data: bytes) -> None:
    out.write(f"static const uint8_t {name}[] = {{\n")
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        out.write("    " + ", ".join(f"0x{value:02x}" for value in chunk))
        out.write(",\n")
    out.write("};\n")
    out.write(f"static const size_t {name}_size = sizeof({name});\n\n")


def main() -> int:
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} TNC155B_INPUT TNC155Q_INPUT OUTPUT", file=sys.stderr)
        return 2
    b_source = pathlib.Path(sys.argv[1])
    q_source = pathlib.Path(sys.argv[2])
    output = pathlib.Path(sys.argv[3])
    b_data = b_source.read_bytes()
    q_data = q_source.read_bytes()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="ascii") as out:
        out.write("#ifndef TNC155_DEFAULT_USER_RAM_H\n")
        out.write("#define TNC155_DEFAULT_USER_RAM_H\n\n")
        out.write("#include <stddef.h>\n#include <stdint.h>\n\n")
        emit_array(out, "tnc155b_default_user_ram", b_data)
        emit_array(out, "tnc155q_default_user_ram", q_data)
        out.write("#endif\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
