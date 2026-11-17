#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# The template editor and the settings table of the Local Security Settings editor (src/dlls/secpol/template.c,
# settings.c), plain C, built and run natively. Needs gcc.
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
cc -O1 -g -Wall -Wextra -fsanitize=address,undefined -o "$OUT/model-test" "$SRC/scripts/secpol-model-test.c" \
    "$SRC/src/dlls/secpol/template.c" "$SRC/src/dlls/secpol/settings.c" || { echo "FAIL: cannot build"; exit 1; }
"$OUT/model-test"
echo "secpol_model_and_table=PASS"
