#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import re
import struct
import sys

VERSION = 0
MAGIC = b"SEL4TRC\0"
RECORD = struct.Struct("<QQQQQIHBBBB6xQ")
HEADER = struct.Struct("<8sHHI16s32s48s48s48s64s64s")

KIND_MMIO = 1
MMIO_WRITE = 1 << 0
MMIO_FORWARDED = 1 << 1
MMIO_UNHANDLED = 1 << 2

HEADER_LINE = re.compile(r"TRH0 ([0-9a-f]*)")
RECORD_LINE = re.compile(r"TRC0 ([0-9a-f]*)")

STAMP_FIELDS = ("codename", "build_id", "bootloader", "baseband", "git_sha", "toolchain_sha256", "dtb_sha256")


class TraceError(Exception):
    pass


def text(field):
    return field.rstrip(b"\0").decode("ascii")


def parse_header(hexdata):
    raw = bytes.fromhex(hexdata)
    if len(raw) != HEADER.size:
        raise TraceError(f"header is {len(raw)} bytes, expected {HEADER.size}")
    fields = HEADER.unpack(raw)
    magic, version, record_size, cntfrq = fields[:4]
    if magic != MAGIC:
        raise TraceError(f"bad magic {magic!r}")
    if version != VERSION:
        raise TraceError(f"trace format version {version}, this decoder reads {VERSION}")
    if record_size != RECORD.size:
        raise TraceError(f"record size {record_size}, expected {RECORD.size}")
    stamp = {name: text(value) for name, value in zip(STAMP_FIELDS, fields[4:])}
    return cntfrq, stamp


def parse_record(hexdata):
    raw = bytes.fromhex(hexdata)
    if len(raw) != RECORD.size:
        raise TraceError(f"record is {len(raw)} bytes, expected {RECORD.size}")
    seq, time, pc, addr, value, esr, vcpu, producer, kind, size, flags, _ = RECORD.unpack(raw)
    return dict(seq=seq, time=time, pc=pc, addr=addr, value=value, esr=esr, vcpu=vcpu,
                producer=producer, kind=kind, size=size, flags=flags)


def format_record(rec):
    if rec["kind"] != KIND_MMIO:
        return f"seq={rec['seq']} kind={rec['kind']} (unknown kind)"
    flags = rec["flags"]
    direction = "W" if flags & MMIO_WRITE else "R"
    if flags & MMIO_UNHANDLED:
        how, value = "UNHANDLED", "-"
    else:
        how = "HW" if flags & MMIO_FORWARDED else "EMU"
        value = f"0x{rec['value']:x}"
    return (f"seq={rec['seq']} t={rec['time']} p{rec['producer']} vcpu={rec['vcpu']} pc=0x{rec['pc']:x} "
            f"addr=0x{rec['addr']:x} size={rec['size']} {direction} {how} val={value} esr=0x{rec['esr']:x}")


class Console:
    def __init__(self, tx_addr):
        self.tx_addr = tx_addr
        self.line = []

    def feed(self, rec):
        if self.tx_addr is None or rec["kind"] != KIND_MMIO:
            return None
        if rec["addr"] != self.tx_addr or not rec["flags"] & MMIO_WRITE or rec["flags"] & MMIO_UNHANDLED:
            return None
        c = chr(rec["value"] & 0xff)
        if c == "\n":
            done, self.line = "".join(self.line), []
            return done
        self.line.append(c)
        return None


def decode(stream, out, console_tx):
    header = None
    expected_seq = 0
    decoded = 0
    errors = 0
    console = Console(console_tx)

    def error(lineno, msg):
        nonlocal errors
        errors += 1
        print(f"ERROR line {lineno}: {msg}", file=out)

    for lineno, line in enumerate(stream, 1):
        m = HEADER_LINE.search(line)
        if m:
            try:
                header = parse_header(m.group(1))
            except (TraceError, ValueError) as e:
                error(lineno, f"header: {e}")
                continue
            cntfrq, stamp = header
            print(f"trace v{VERSION} cntfrq={cntfrq}", file=out)
            for name in STAMP_FIELDS:
                print(f"  {name}: {stamp[name]}", file=out)
            continue

        m = RECORD_LINE.search(line)
        if not m:
            continue
        if header is None:
            error(lineno, "record before header")
            continue
        try:
            rec = parse_record(m.group(1))
        except (TraceError, ValueError) as e:
            error(lineno, f"record: {e}")
            continue
        if rec["seq"] != expected_seq:
            error(lineno, f"seq {rec['seq']}, expected {expected_seq}")
        expected_seq = rec["seq"] + 1
        decoded += 1
        print(format_record(rec), file=out)
        text_line = console.feed(rec)
        if text_line is not None:
            print(f"  guest| {text_line}", file=out)

    if header is None:
        error(0, "no trace header found")
    print(f"{decoded} records, {errors} errors", file=out)
    return errors


def main():
    parser = argparse.ArgumentParser(description="Decode a trace format v0 serial log.")
    parser.add_argument("log", nargs="?", type=argparse.FileType("r", errors="replace"), default=sys.stdin)
    parser.add_argument("--console-tx", type=lambda s: int(s, 0),
                        help="guest-physical address of a UART TX register to reassemble guest output from")
    args = parser.parse_args()
    return 1 if decode(args.log, sys.stdout, args.console_tx) else 0


if __name__ == "__main__":
    sys.exit(main())
