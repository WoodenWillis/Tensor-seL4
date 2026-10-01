#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import re
import struct
import sys

VERSIONS = (0, 1, 2, 3, 4, 5, 6)
MAGIC = b"SEL4TRC\0"
RECORD = struct.Struct("<QQQQQIHBBBB6xQ")
HEADER = struct.Struct("<8sHHI16s32s48s48s48s64s64s")

KIND_MMIO = 1
KIND_SMC_ENTER = 2
KIND_SMC_REGS = 3
KIND_SMC_EXIT = 4
KIND_CMD = 5
KIND_GUEST = 6
KINDS_BY_VERSION = {
    0: {KIND_MMIO},
    1: {KIND_MMIO, KIND_SMC_ENTER, KIND_SMC_REGS, KIND_SMC_EXIT},
    2: {KIND_MMIO, KIND_SMC_ENTER, KIND_SMC_REGS, KIND_SMC_EXIT, KIND_CMD},
    3: {KIND_MMIO, KIND_SMC_ENTER, KIND_SMC_REGS, KIND_SMC_EXIT, KIND_CMD, KIND_GUEST},
    4: {KIND_MMIO, KIND_SMC_ENTER, KIND_SMC_REGS, KIND_SMC_EXIT, KIND_CMD, KIND_GUEST},
    5: {KIND_MMIO, KIND_SMC_ENTER, KIND_SMC_REGS, KIND_SMC_EXIT, KIND_CMD, KIND_GUEST},
    6: {KIND_MMIO, KIND_SMC_ENTER, KIND_SMC_REGS, KIND_SMC_EXIT, KIND_CMD, KIND_GUEST},
}

MMIO_WRITE = 1 << 0
MMIO_FORWARDED = 1 << 1
MMIO_UNHANDLED = 1 << 2

SMC_REGS_EXIT = 1 << 0
SMC_FORWARDED = 1 << 1
SMC_UNHANDLED = 1 << 2

CMD_ACCEPTED = 1 << 0
CMD_REJECTED_VERB = 1 << 2
CMD_VERBS = {0: "-", 1: "ping", 2: "trace-dump", 3: "help", 4: "guest-start", 5: "guest-stop", 6: "status", 7: "guest-regs", 8: "gic-dump", 9: "pc-sample"}
GUEST_EVENTS = {1: "STARTED", 2: "STOPPED_BY_COMMAND", 3: "STOPPED_BY_FAULT"}

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


def format_cmd(rec):
    flags = rec["flags"]
    if flags & CMD_ACCEPTED:
        how = "ACCEPTED"
    elif flags & CMD_REJECTED_VERB:
        how = "REJECTED_VERB"
    else:
        how = f"flags=0x{flags:x}"
    verb = CMD_VERBS.get(rec["addr"], f"verb{rec['addr']}")
    return f"seq={rec['seq']} t={rec['time']} p{rec['producer']} CMD id={rec['value']} {verb} {how}"


def format_guest(rec):
    event = GUEST_EVENTS.get(rec["addr"], f"event{rec['addr']}")
    return (f"seq={rec['seq']} t={rec['time']} p{rec['producer']} vcpu={rec['vcpu']} "
            f"GUEST run={rec['value']} {event} pc=0x{rec['pc']:x}")


def format_record(rec):
    if rec["kind"] == KIND_GUEST:
        return format_guest(rec)
    if rec["kind"] == KIND_CMD:
        return format_cmd(rec)
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
    header_hex = None
    pending = []
    expected_seq = 0
    decoded = 0
    errors = 0
    console = Console(console_tx)

    def error(lineno, msg):
        nonlocal errors
        errors += 1
        print(f"ERROR line {lineno}: {msg}", file=out)

    def record(lineno, digit, hexdata):
        nonlocal expected_seq, decoded
        version = header[0]
        if digit != version:
            error(lineno, f"TRC{digit} record in a version {version} trace")
            return
        try:
            rec = parse_record(hexdata)
        except (TraceError, ValueError) as e:
            error(lineno, f"record: {e}")
            return
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

    for lineno, line in enumerate(stream, 1):
        m = HEADER_LINE.search(line)
        if m:
            digit, hexdata = int(m.group(1)), m.group(2)
            try:
                parsed = parse_header(hexdata)
            except (TraceError, ValueError) as e:
                error(lineno, f"header: {e}")
                continue
            if digit != parsed[0]:
                error(lineno, f"TRH{digit} line carries a version {parsed[0]} header")
                continue
            if header is not None:
                if hexdata != header_hex:
                    error(lineno, "header differs from the first header in this trace")
                continue
            header, header_hex = parsed, hexdata
            version, cntfrq, stamp = header
            print(f"trace v{version} cntfrq={cntfrq}", file=out)
            for name in STAMP_FIELDS:
                print(f"  {name}: {stamp[name]}", file=out)
            for args in pending:
                record(*args)
            pending = []
            continue

        m = RECORD_LINE.search(line)
        if not m:
            continue
        args = (lineno, int(m.group(1)), m.group(2))
        if header is None:
            pending.append(args)
        else:
            record(*args)

    if header is None:
        error(0, f"no valid trace header found ({len(pending)} records could not be decoded)")
    print(f"{decoded} records, {errors} errors", file=out)
    return errors


def main():
    parser = argparse.ArgumentParser(description="Decode a trace format v0 to v6 serial log.")
    parser.add_argument("log", nargs="?", type=argparse.FileType("r", errors="replace"), default=sys.stdin)
    parser.add_argument("--console-tx", type=lambda s: int(s, 0),
                        help="guest-physical address of a UART TX register to reassemble guest output from")
    args = parser.parse_args()
    return 1 if decode(args.log, sys.stdout, args.console_tx) else 0


if __name__ == "__main__":
    sys.exit(main())
