#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <loader.img> <boot.img>" >&2
    exit 2
fi

loader=$1
out=$2
image=$(mktemp)
trap 'rm -f "$image"' EXIT

python3 "$(dirname "$0")/arm64-image.py" "$loader" "$image"
mkbootimg --header_version 4 --kernel "$image" --output "$out"
