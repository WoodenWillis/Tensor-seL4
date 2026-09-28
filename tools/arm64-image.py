#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import struct
import sys

HEADER_SIZE = 64
PAYLOAD_OFFSET = 0x1000
B_OVER_HEADER = 0x14000000 | (PAYLOAD_OFFSET // 4)
ARM64_IMAGE_MAGIC = 0x644D5241
FLAGS_PAGE_SIZE_4K = 1 << 1
FLAGS_PHYS_BASE_ANYWHERE = 1 << 3


def arm64_header(payload_size: int) -> bytes:
    return struct.pack(
        "<IIQQQQQQII",
        B_OVER_HEADER,
        0,
        0,
        PAYLOAD_OFFSET + payload_size,
        FLAGS_PAGE_SIZE_4K | FLAGS_PHYS_BASE_ANYWHERE,
        0,
        0,
        0,
        ARM64_IMAGE_MAGIC,
        0,
    )


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <loader.img> <Image>", file=sys.stderr)
        return 2

    with open(sys.argv[1], "rb") as f:
        payload = f.read()

    header = arm64_header(len(payload))
    assert len(header) == HEADER_SIZE

    with open(sys.argv[2], "wb") as f:
        f.write(header)
        f.write(bytes(PAYLOAD_OFFSET - HEADER_SIZE))
        f.write(payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
