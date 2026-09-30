#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import sys

S_IFDIR = 0o040000
S_IFCHR = 0o020000
S_IFREG = 0o100000


def pad4(n):
    return (4 - n % 4) % 4


def entry(ino, name, mode, data=b"", rdev=(0, 0)):
    name_bytes = name.encode("ascii") + b"\0"
    fields = [ino, mode, 0, 0, 1, 0, len(data), 0, 0, rdev[0], rdev[1], len(name_bytes), 0]
    header = b"070701" + b"".join(f"{f:08x}".encode("ascii") for f in fields)
    out = header + name_bytes + b"\0" * pad4(len(header) + len(name_bytes))
    return out + data + b"\0" * pad4(len(data))


def main():
    parser = argparse.ArgumentParser(description="Build a newc initramfs with /init, /dev/console and /dev/kmsg.")
    parser.add_argument("init", help="static init executable")
    parser.add_argument("out", help="cpio archive to write")
    args = parser.parse_args()

    with open(args.init, "rb") as f:
        init = f.read()
    archive = b"".join([
        entry(1, "dev", S_IFDIR | 0o755),
        entry(2, "dev/console", S_IFCHR | 0o600, rdev=(5, 1)),
        entry(3, "dev/kmsg", S_IFCHR | 0o600, rdev=(1, 11)),
        entry(4, "init", S_IFREG | 0o755, init),
        entry(0, "TRAILER!!!", 0),
    ])
    with open(args.out, "wb") as f:
        f.write(archive)
    return 0


if __name__ == "__main__":
    sys.exit(main())
