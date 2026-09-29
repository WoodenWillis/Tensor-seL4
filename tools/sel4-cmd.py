#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import sys

import serial


def main():
    parser = argparse.ArgumentParser(description="Type one command line to the phone over the debug UART.")
    parser.add_argument("command", nargs="+", help="command and arguments, e.g. trace-dump")
    parser.add_argument("--device", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=3000000)
    args = parser.parse_args()

    line = " ".join(args.command) + "\r"
    with serial.Serial(args.device, args.baud, timeout=1) as port:
        port.write(line.encode("ascii"))
        port.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
