#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# In the lab guest: run a probe exe (already pushed to the workspace's claude/ directory) under the candidate Wine build in the existing
# Windows App SDK prefix, print its output. usage: wine-run.sh NAME.exe [TIMEOUT_SECONDS]
g=${LAB_ROOT:-/home/debian/uwp-u1-20261111}
b=$g/build/candidates/${CAND:-u13}/build
export WINEPREFIX=$b/../wasdk-run/prefix WINESERVER=$b/server/wineserver DISPLAY=${DISPLAY:-:99} WINEDEBUG=-all
timeout --kill-after=5 "${2:-60}" "$b/wine" "$g/claude/$1" 2>&1 | tr -d '\r' | grep -a -v '^libEGL warning'
"$b/server/wineserver" -k 2>/dev/null
exit 0
