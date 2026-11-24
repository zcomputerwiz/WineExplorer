#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: update the prefix of candidate u12 (new runtime classes are registered by wineboot), then run the applicationdata test for x86
# the workspace of the lab guest (override with UWP_WS)
UWP_WS=${UWP_WS:-/home/debian/uwp-u1-20261111}
C=${UWP_WS}/build/candidates/u12
export WINEPREFIX=$C/wasdk-run/prefix WINEDEBUG=-all DISPLAY=:99
cd $C/build
./server/wineserver -k; sleep 1
timeout 200 ./wine wineboot -u > /tmp/wb.txt 2>&1
./server/wineserver -w
for a in x86_64 i386; do echo "== $a"; timeout 90 ./wine dlls/windows.storage.applicationdata/tests/$a-windows/windows.storage.applicationdata_test.exe 2>&1 | tr -d '\r' | tail -4; done
