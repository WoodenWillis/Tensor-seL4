#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import re
import struct
import sys

VERSIONS = (0, 1)
MAGIC = b"SEL4TRC\0"
RECORD = struct.Struct("<QQQQQIHBBBB6xQ")
HEADER = struct.Struct("<8sHHI16s32s48s48s48s64s64s")

KIND_MMIO = 1
KIND_SMC_ENTER = 2
KIND_SMC_REGS = 3
KIND_SMC_EXIT = 4
KINDS_BY_VERSION = {0: {KIND_MMIO}, 1: {KIND_MMIO, KIND_SMC_ENTER, KIND_SMC_REGS, KIND_SMC_EXIT}}

MMIO_WRITE = 1 << 0
MMIO_FORWARDED = 1 << 1
MMIO_UNHANDLED = 1 << 2

SMC_REGS_EXIT = 1 << 0
SMC_FORWARDED = 1 << 1
SMC_UNHANDLED = 1 << 2

HEADER_LINE = re.compile(r"TRH([0-9]) ([0-9a-f]*)")
RECORD_LINE = re.compile(r"TRC([0-9]) ([0-9a-f]*)")

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
    if version not in VERSIONS:
        raise TraceError(f"trace format version {version}, this decoder reads {VERSIONS}")
    if record_size != RECORD.size:
        raise TraceError(f"record size {record_size}, expected {RECORD.size}")
    stamp = {name: text(value) for name, value in zip(STAMP_FIELDS, fields[4:])}
    return version, cntfrq, stamp


def parse_record(hexdata):
    raw = bytes.fromhex(hexdata)
    if len(raw) != RECORD.size:
        raise TraceError(f"record is {len(raw)} bytes, expected {RECORD.size}")
    seq, time, pc, addr, value, esr, vcpu, producer, kind, size, flags, _ = RECORD.unpack(raw)
    return dict(seq=seq, time=time, pc=pc, addr=addr, value=value, esr=esr, vcpu=vcpu,
                producer=producer, kind=kind, size=size, flags=flags)


def format_smc(rec):
    prefix = f"seq={rec['seq']} t={rec['time']} p{rec['producer']} vcpu={rec['vcpu']} pc=0x{rec['pc']:x}"
    kind, flags = rec["kind"], rec["flags"]
    if kind == KIND_SMC_ENTER:
        return f"{prefix} SMC ENTER x0=0x{rec['addr']:x} x1=0x{rec['value']:x} esr=0x{rec['esr']:x}"
    if kind == KIND_SMC_REGS:
        n = rec["size"]
        side = "out" if flags & SMC_REGS_EXIT else "in"
        return f"{prefix} SMC {side} x{n}=0x{rec['addr']:x} x{n + 1}=0x{rec['value']:x}"
    if flags & SMC_UNHANDLED:
        return f"{prefix} SMC EXIT UNHANDLED"
    how = "HW" if flags & SMC_FORWARDED else "EMU"
    return f"{prefix} SMC EXIT {how} x0=0x{rec['addr']:x} x1=0x{rec['value']:x}"


def format_record(rec):
    if rec["kind"] in (KIND_SMC_ENTER, KIND_SMC_REGS, KIND_SMC_EXIT):
        return format_smc(rec)
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
                header = parse_header(m.group(2))
            except (TraceError, ValueError) as e:
                error(lineno, f"header: {e}")
                continue
            version, cntfrq, stamp = header
            if int(m.group(1)) != version:
                error(lineno, f"TRH{m.group(1)} line carries a version {version} header")
            print(f"trace v{version} cntfrq={cntfrq}", file=out)
            for name in STAMP_FIELDS:
                print(f"  {name}: {stamp[name]}", file=out)
            continue

        m = RECORD_LINE.search(line)
        if not m:
            continue
        if header is None:
            error(lineno, "record before header")
            continue
        version = header[0]
        if int(m.group(1)) != version:
            error(lineno, f"TRC{m.group(1)} record in a version {version} trace")
            continue
        try:
            rec = parse_record(m.group(2))
        except (TraceError, ValueError) as e:
            error(lineno, f"record: {e}")
            continue
        if rec["kind"] not in KINDS_BY_VERSION[version]:
            error(lineno, f"kind {rec['kind']} is not defined in version {version}")
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
    parser = argparse.ArgumentParser(description="Decode a trace format v0 or v1 serial log.")
    parser.add_argument("log", nargs="?", type=argparse.FileType("r", errors="replace"), default=sys.stdin)
    parser.add_argument("--console-tx", type=lambda s: int(s, 0),
                        help="guest-physical address of a UART TX register to reassemble guest output from")
    args = parser.parse_args()
    return 1 if decode(args.log, sys.stdout, args.console_tx) else 0


if __name__ == "__main__":
    sys.exit(main())
