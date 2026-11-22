#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: start the Store app (lab copy) in the prefix store-run left, let it settle for 25 s and run stackscan.exe (stackscan.c)
# against it: the code addresses on every thread's stack. Output: store-run/scan.txt
# usage: store-scan.sh [WINEDEBUG]
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/u11-2/build
c=$root/claude
run=$root/build/candidates/u11-2/store-run
cd "$run"
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" DISPLAY=${DISPLAY:-:99}
x86_64-w64-mingw32-gcc -O2 -o stackscan.exe $c/stackscan.c 2> scan-build.log
"$build/server/wineserver" -k 2>/dev/null; sleep 1
family=$(tr -d '\r' < install.log | sed -n 's/^Application: \(.*\)!App$/\1/p')
export WINEDEBUG="${1:-err+all}" XAMLSHIM_TRACE=1
( timeout --kill-after=10s 60s "$build/wine" $c/applaunch.exe "$family!App" > scan-app.log 2>&1 & )
sleep 25
timeout 40 "$build/wine" stackscan.exe WinStore.App > scan.txt 2>&1
sleep 2
"$build/server/wineserver" -k
echo done >> scan.txt
