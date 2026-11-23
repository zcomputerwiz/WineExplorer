#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: run tests/uwp/alpc/pm-probe.c (compiled here) on the Wine build of candidate u12 in a fresh prefix (the classes are registered
# by wineboot). usage: pm-run.sh [stage]
root=/home/debian/uwp-u1-20261111
b=$root/build/candidates/u12/build
p=$root/build/candidates/u12/pm-prefix
c=$root/claude
x86_64-w64-mingw32-gcc -O2 -o $c/pm-probe.exe $c/pm-probe.c -lole32 || exit 1
export WINEPREFIX=$p WINESERVER=$b/server/wineserver WINEDEBUG=-all DISPLAY=:99
$WINESERVER -k 2>/dev/null; rm -rf $p
WINEDLLOVERRIDES=mscoree,mshtml= $b/wine wineboot -i > /dev/null 2>&1; $WINESERVER -w
WINEDEBUG=-all $b/wine reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f > /dev/null 2>&1; $WINESERVER -w
export WINEDEBUG=err+all,fixme+appx
timeout 120 $b/wine $c/pm-probe.exe "$@" 2>&1 | grep -v "libEGL\|gpsvc"
