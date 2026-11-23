#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# The Registry.pol reader and writer and the security template's options (src/include/wexpreg.h), plain C, built and
# run natively (src/programs/wexpolicy/test_preg_main.c). Needs gcc.
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
cc -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"$SRC/src/include" -o "$OUT/preg-test" \
    "$SRC/src/programs/wexpolicy/test_preg_main.c" || { echo "FAIL: cannot build"; exit 1; }
"$OUT/preg-test"
echo "preg=PASS"
