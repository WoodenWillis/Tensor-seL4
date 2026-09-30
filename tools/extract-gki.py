#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import hashlib
import io
import os
import struct
import subprocess
import sys
import urllib.request
import zipfile

BUILD = "caiman-bp1a.250505.005"
FACTORY_ZIP = f"{BUILD}-factory-f158d94e.zip"
FACTORY_URL = f"https://dl.google.com/dl/android/aosp/{FACTORY_ZIP}"
FACTORY_SHA256 = "f158d94e5e9209ed9b22a4a4d81b1ae70a1631410adf75d2a2a8297d02d7363a"
IMAGE_ZIP = f"{BUILD}/image-{BUILD}.zip"
KERNEL_SHA256 = "c822b5f4675020005a028eececa1f6e801e7d1a0cffa2a68cdb8eaa471975530"

BOOT_MAGIC = b"ANDROID!"
BOOT_PAGE_SIZE = 4096
LZ4_LEGACY_MAGIC = b"\x02\x21\x4c\x18"
ARM64_IMAGE_MAGIC = b"ARM\x64"


class ExtractError(Exception):
    pass


class Window(io.RawIOBase):
    def __init__(self, f, start, size):
        self.f, self.start, self.size, self.pos = f, start, size, 0

    def seekable(self):
        return True

    def readable(self):
        return True

    def seek(self, off, whence=io.SEEK_SET):
        base = {io.SEEK_SET: 0, io.SEEK_CUR: self.pos, io.SEEK_END: self.size}[whence]
        self.pos = base + off
        return self.pos

    def tell(self):
        return self.pos

    def readinto(self, b):
        want = max(0, min(len(b), self.size - self.pos))
        self.f.seek(self.start + self.pos)
        n = self.f.readinto(memoryview(b)[:want])
        self.pos += n
        return n


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch_factory_zip(path):
    if os.path.exists(path):
        return
    print(f"downloading {FACTORY_URL}", file=sys.stderr)
    part = path + ".part"
    urllib.request.urlretrieve(FACTORY_URL, part)
    os.replace(part, path)


def open_stored_member(outer_path, name):
    outer = zipfile.ZipFile(outer_path)
    info = outer.getinfo(name)
    if info.compress_type != zipfile.ZIP_STORED:
        raise ExtractError(f"{name} is compressed inside the factory zip; expected it stored")
    f = open(outer_path, "rb")
    f.seek(info.header_offset)
    local = f.read(30)
    name_len, extra_len = struct.unpack("<HH", local[26:30])
    start = info.header_offset + 30 + name_len + extra_len
    return zipfile.ZipFile(io.BufferedReader(Window(f, start, info.compress_size)))


def boot_kernel(boot):
    if boot[:8] != BOOT_MAGIC:
        raise ExtractError("boot.img has no ANDROID! magic")
    kernel_size = struct.unpack("<I", boot[8:12])[0]
    header_version = struct.unpack("<I", boot[40:44])[0]
    if header_version not in (3, 4):
        raise ExtractError(f"boot.img header version {header_version}, expected 3 or 4")
    kernel = boot[BOOT_PAGE_SIZE:BOOT_PAGE_SIZE + kernel_size]
    if kernel[:4] != LZ4_LEGACY_MAGIC:
        raise ExtractError("boot.img kernel is not an LZ4 legacy frame")
    return kernel


def lz4_decompress(data):
    result = subprocess.run(["lz4", "-d", "-c"], input=data, stdout=subprocess.PIPE, check=True)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=f"Extract the GKI kernel Image from the {BUILD} factory image.")
    parser.add_argument("out", help="where to write the uncompressed arm64 Image")
    parser.add_argument("--factory-zip", default=os.path.join("deps", FACTORY_ZIP))
    args = parser.parse_args()

    try:
        fetch_factory_zip(args.factory_zip)
        got = sha256_file(args.factory_zip)
        if got != FACTORY_SHA256:
            raise ExtractError(f"{args.factory_zip} has sha256 {got}, expected {FACTORY_SHA256}")
        boot = open_stored_member(args.factory_zip, IMAGE_ZIP).read("boot.img")
        image = lz4_decompress(boot_kernel(boot))
        if image[0x38:0x3c] != ARM64_IMAGE_MAGIC:
            raise ExtractError("decompressed kernel has no arm64 Image magic")
        got = hashlib.sha256(image).hexdigest()
        if got != KERNEL_SHA256:
            raise ExtractError(f"kernel Image has sha256 {got}, expected {KERNEL_SHA256}")
    except (ExtractError, OSError, zipfile.BadZipFile, subprocess.CalledProcessError) as e:
        print(f"extract-gki: {e}", file=sys.stderr)
        return 1

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(image)
    print(f"{args.out}: {len(image)} bytes, sha256 {KERNEL_SHA256}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
