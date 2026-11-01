#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Connect project modules before configuring Wine. No resource overlays are read.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
wine_root="${1:-$root/wine}"
wine_root="$(realpath "$wine_root")"
test -f "$wine_root/configure.ac"
python3 "$root/scripts/assemble.py" --wine-root "$wine_root"
# Assembly registers project modules for the selected Wine base; regenerate configure.
(cd "$wine_root" && autoconf -f)
