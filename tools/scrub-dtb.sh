#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <device.dtb> <scrubbed.dtb>" >&2
    exit 2
fi

in=$1
out=$2
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

cp -- "$in" "$tmp"

fdtput -d "$tmp" /chosen kaslr-seed bootargs linux,initrd-start linux,initrd-end
fdtput -r "$tmp" /chosen/config
fdtput -r "$tmp" /avf
fdtput -d "$tmp" /sjtag_ap pubkey
fdtput -d "$tmp" /sjtag_gsa pubkey

dtc -q -I dtb -O dtb -o "$out" "$tmp"

leaks=$(fdtdump "$out" 2>/dev/null | grep -cE '\b(imei[0-9]*|wlan_mac[0-9]*|bt_addr|dsn|psn|serialno|kaslr-seed|rng-seed|secretkeeper_public_key|pubkey)\b' || true)
if [ "$leaks" -ne 0 ]; then
    echo "$0: $leaks sensitive property name(s) survived scrubbing" >&2
    rm -f -- "$out"
    exit 1
fi

sha256sum -- "$out"
