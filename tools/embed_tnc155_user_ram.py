#!/usr/bin/env python3
import base64
import hashlib
import pathlib
import sys
import zlib

EXPECTED_SIZE = 0x12000
EXPECTED_SHA256 = "1805c3b50110915c5b46d53751e98a23fcfca427281415965863bd438191179d"

# Exact 73728-byte User RAM image supplied for the STM32H7 TNC155 port.
# Compression exists only to keep the repository source compact; firmware
# receives the uncompressed byte-for-byte image in normal flash rodata.
PACKED = b"".join(
    line.encode("ascii") for line in (
        "eNrt2D9rE2EcwPHfJalJpdqCipUuFZGIqIigk0iLFoqKdPEdODk4O4h0KEVwcdHBxeIkjuILEKxaxSFzFtfO4iTFeH+SNukfKlpq",
        "rZ/vccndJ9fnnlyuhSZikx6v2h+OLe1MKeLkQmevdi7KUSq2b9YiWq10o/Qn43+ajmjej9nm5EyMv/iDgWrZSLuv0b9cSJIkSZK2",
        "paX285X6YI+PDxXPE8O3d8Q8N5pHZ56/2+VLxb/110618tY7ZiPvn0/ndS8q6TKQfUMQ8fLqyrcVY6fG8otbbfTPVxvVRjZGe5xK",
        "ti6P2TN2807XTnZcMju35sTr2Xa0xedtf3aLW3Vce47PNv7E8rJvm763L3B5m65pcv2zvzWSJEmSdkozRwkhhBBCCCGEEEIIIYQQ",
        "QgghhBBCCCGEEEIIIYQQQgghhBBCCCGEEEIIIYQQQgghhBBCCCGEEEIIIYQQQgghhBBCCCGEEEIIIYQQQgghhBBCCCGEEEIIIYQQ",
        "QgghhBBCCCGEEEIIIYQQQgghhBBCCCGEEEIIIYQQQgghhBBCCCGEEEIIIYQQQnaWXMkektZy9cFKeTSZncu2q43CxlYd0/pR7Dfv",
        "R/n5w5n4stTSf1XE2fPxe/VFkj8nsZCu6V55sViWj0jay5qqjfpQlNKN2jrjlnu3b4xGpEcX462MXEpfq6RnLQ5bXDtMfahv78iN",
        "/Gwjl8+OjNcGNn9LpfLE5PhkvEsOH9qTjp6dJSnGyl+eitddM1jdrXRJf986VyGd07lsZgceFEv/fLH8wpV9kv5kX9f+kWxq6fvt",
        "i3+lqbibXrXB+mCcfv9m0xvpYH6FFzrX7diJ2J98W9nPP8MP2RUvNYslyvvGutfW0tc33Wtn5KcRrTV/Kde5Hyv5h9+5b7LHT9PF",
        "Wtyux6d77sqiWvLx7XT3+uPCxZ414tFmb/1VSJIkSZKkXZ9vICVJkiRJ2v39BPx0duI=",
    )
)


def image_bytes() -> bytes:
    data = zlib.decompress(base64.b64decode(PACKED))
    if len(data) != EXPECTED_SIZE:
        raise RuntimeError(f"User RAM size {len(data)} != {EXPECTED_SIZE}")
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise RuntimeError(f"User RAM SHA256 {digest} != {EXPECTED_SHA256}")
    return data


def write_header(data: bytes, output: pathlib.Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="ascii") as out:
        out.write("#ifndef TNC155_DEFAULT_USER_RAM_H\n")
        out.write("#define TNC155_DEFAULT_USER_RAM_H\n\n")
        out.write("#include <stddef.h>\n#include <stdint.h>\n\n")
        out.write("static const uint8_t tnc155_default_user_ram[] = {\n")
        for offset in range(0, len(data), 16):
            chunk = data[offset:offset + 16]
            out.write("    " + ", ".join(f"0x{value:02x}" for value in chunk))
            out.write(",\n")
        out.write("};\n")
        out.write("static const size_t tnc155_default_user_ram_size =\n")
        out.write("    sizeof(tnc155_default_user_ram);\n\n#endif\n")


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} OUTPUT_HEADER", file=sys.stderr)
        return 2
    data = image_bytes()
    write_header(data, pathlib.Path(sys.argv[1]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
