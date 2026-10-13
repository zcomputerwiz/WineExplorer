#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Wrong-source guard must fail before any installation is attempted.
set -euo pipefail
project=$(cd "$(dirname "$0")/../.." && pwd)
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
printf 'srcdir = /tmp\ninstall:\n\t@touch %s/install-was-called\n' "$fixture" > "$fixture/Makefile"
if BUILD_DIR="$fixture" OUT_DIR="$fixture/output" bash "$project/installer/package-wine.sh" > "$fixture/log" 2>&1; then
    echo "Wrong-source build unexpectedly packaged"
    exit 1
fi
grep -q "BUILD_DIR belongs to another Wine source checkout" "$fixture/log"
test ! -e "$fixture/install-was-called"
echo "PASS wrong-source build rejected before make install"
