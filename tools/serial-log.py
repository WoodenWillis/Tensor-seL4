#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import importlib.util
import os
import sys

import serial


def load_decoder():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "trace-decode.py")
    spec = importlib.util.spec_from_file_location("trace_decode", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def summarise(td, line):
    m = td.HEADER_LINE.search(line)
    if m:
        try:
            version, _, stamp = td.parse_header(m.group(2))
        except (td.TraceError, ValueError) as e:
            return f"[trace] bad header: {e}"
        return f"[trace] v{version} header, git {stamp['git_sha']}"
    m = td.RECORD_LINE.search(line)
    if not m:
        return line
    try:
        rec = td.parse_record(m.group(2))
    except (td.TraceError, ValueError) as e:
        return f"[trace] bad record: {e}"
    if rec["kind"] == td.KIND_CMD:
        return f"[trace] {td.format_cmd(rec)}"
    if rec["kind"] == td.KIND_GUEST:
        return f"[trace] {td.format_guest(rec)}"
    if rec["kind"] == td.KIND_MMIO and rec["flags"] & td.MMIO_UNHANDLED:
        return f"[trace] {td.format_record(rec)}"
    if rec["kind"] == td.KIND_SMC_EXIT and rec["flags"] & td.SMC_UNHANDLED:
        return f"[trace] {td.format_record(rec)}"
    return None


def main():
    parser = argparse.ArgumentParser(
        description="Log the debug UART to a file, byte for byte, and show it with trace records hidden.")
    parser.add_argument("log", help="file to write the complete, unfiltered serial output to")
    parser.add_argument("--device", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=3000000)
    args = parser.parse_args()

    td = load_decoder()
    pending = b""
    with serial.Serial(args.device, args.baud, timeout=0.1) as port, open(args.log, "wb") as log:
        try:
            while True:
                data = port.read(4096)
                if not data:
                    continue
                log.write(data)
                log.flush()
                pending += data
                *lines, pending = pending.split(b"\n")
                for raw in lines:
                    shown = summarise(td, raw.decode("ascii", errors="replace").rstrip("\r"))
                    if shown is not None:
                        print(shown, flush=True)
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
