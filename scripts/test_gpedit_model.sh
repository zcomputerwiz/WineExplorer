#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# The policy table and the Registry.pol model of the local policy editor (src/dlls/gpedit/policies.c, model.c), plain
# C, built and run natively. Needs gcc.
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
cc -O1 -g -Wall -Wextra -fsanitize=address,undefined -o "$OUT/model-test" "$SRC/scripts/gpedit-model-test.c" \
    "$SRC/src/dlls/gpedit/model.c" "$SRC/src/dlls/gpedit/policies.c" || { echo "FAIL: cannot build"; exit 1; }
"$OUT/model-test"
echo "gpedit_model_and_table=PASS"
