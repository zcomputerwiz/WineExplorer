#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: cursor-probe.exe on candidate u12 in the application prefix
# the workspace of the lab guest (override with UWP_WS)
UWP_WS=${UWP_WS:-/home/debian/uwp-u1-20261111}
c=${UWP_WS}/claude
C=${UWP_WS}/build/candidates/u12
cd $c && x86_64-w64-mingw32-gcc -O2 -o cursor-probe.exe cursor-probe.c -luser32 2>&1 | head -3
export WINEPREFIX=$C/wasdk-run/prefix WINEDEBUG=${WD:--all} DISPLAY=:99
timeout 60 $C/build/wine cursor-probe.exe 2>&1 | tr -d '\r' | head -40
