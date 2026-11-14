#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# run the Uno spike (self-contained .NET 10, Uno Skia desktop) under Wine, x64 and x86, in a fresh prefix, under Xvfb
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/u11-2/build
run=$root/build/candidates/u11-2/uno-$1
rm -rf "$run"; mkdir -p "$run"; cd "$run"
tar -xzf $root/claude/uno-spike.tgz
export WINEPREFIX="$run/prefix" WINEDEBUG=-all WINESERVER="$build/server/wineserver"
WINEDLLOVERRIDES=mscoree,mshtml= "$build/wine" wineboot -i > boot.log 2>&1; "$build/server/wineserver" -w
for arch in x64 x86; do
  rm -f "$WINEPREFIX/drive_c/users/$USER/AppData/Local/Temp/uno-spike.txt"
  timeout --kill-after=10s 120s "$build/wine" "out-win-$arch/UnoSpike.exe" > "$arch.log" 2>&1
  echo "$arch exit $?" >> summary
  cat "$WINEPREFIX/drive_c/users/$USER/AppData/Local/Temp/uno-spike.txt" >> summary 2>/dev/null || echo "no output" >> summary
done
"$build/server/wineserver" -k
echo done >> summary
