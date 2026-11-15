#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: register the XamlProbe package (C++/CX XAML) in a fresh prefix, install the XAML shim when the tar has it
# (C:\xamlshim, classes registered to xamlshim.dll), and launch it. usage: cxprobe-run.sh RUN [WINEDEBUG]
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/u11-2/build
fx=$root/claude/fixture
run=$root/build/candidates/u11-2/cx-$1
rm -rf "$run"; mkdir -p "$run"; cd "$run"
tar -xf $root/claude/cxprobe.tar
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" WINEDEBUG=-all
WINEDLLOVERRIDES=mscoree,mshtml= "$build/wine" wineboot -i > boot.log 2>&1; "$build/server/wineserver" -w
"$build/wine" reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f > reg.log 2>&1
if [ -d cxprobe/xamlshim ]; then
  cp -r cxprobe/xamlshim "$WINEPREFIX/drive_c/xamlshim"
  "$build/wine" regedit /S 'Z:'"$(pwd)/cxprobe/xamlshim.reg" >> reg.log 2>&1
  "$build/server/wineserver" -w
fi
names=$(python3 "$fx/register-wine.py" register "$WINEPREFIX" cxprobe/stage)
IFS=$'\t' read -r full family pkgroot <<< "$names"
echo "registered $full" > summary
export WINEDEBUG="${2:-err+all,fixme-all}" XAMLSHIM_TRACE=1
timeout --kill-after=10s 120s "$build/wine" "$fx/x64/launcher.exe" "$family!App" probe unused "$full" > launch.log 2>&1
echo "launcher exit $?" >> summary
start=$(date +%s)
for i in $(seq 60); do
  log=$(find "$WINEPREFIX/drive_c/users" -name xamlprobe.txt 2>/dev/null | head -1)
  [ -n "$log" ] && grep -q '^exit' "$log" && break
  sleep 1
done
echo "waited $(( $(date +%s) - start )) s" >> summary
sleep 1
log=$(find "$WINEPREFIX/drive_c/users" -name xamlprobe.txt 2>/dev/null | head -1)
if [ -n "$log" ]; then cat "$log" >> summary; else echo "no probe log" >> summary; fi
shimlog=$(find "$WINEPREFIX/drive_c/users" -name xamlshim.txt 2>/dev/null | head -1)
if [ -n "$shimlog" ]; then echo "--- shim" >> summary; grep -v '^[0-9]*: QI ' "$shimlog" | head -60 >> summary; echo "QI misses: $(grep -c ': QI ' "$shimlog")" >> summary; fi
"$build/server/wineserver" -k
grep -aE 'err:|Unhandled|exception' launch.log | sed 's/^[0-9a-f]*://' | sort | uniq -c | sort -rn | head -15 >> summary
echo done >> summary
