#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# XAML experiment step 2a under Wine x64: native xamlhost.exe hosts CoreCLR and Uno in its own process. usage: xamlhost-run.sh RUN
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/u11-2/build
run=$root/build/candidates/u11-2/xh-$1
rm -rf "$run"; mkdir -p "$run"; cd "$run"
tar -xzf $root/claude/xamlhost.tgz
export WINEPREFIX="$run/prefix" WINEDEBUG=-all WINESERVER="$build/server/wineserver"
WINEDLLOVERRIDES=mscoree,mshtml= "$build/wine" wineboot -i > boot.log 2>&1; "$build/server/wineserver" -w
log="$WINEPREFIX/drive_c/users/$USER/AppData/Local/Temp/uno-spike.txt"
for exe in xamlhost.exe UnoSpike.exe; do
  rm -f "$log"
  timeout --kill-after=10s 120s "$build/wine" "out-win-x64/$exe" > "$exe.log" 2>&1
  echo "$exe exit $?" >> summary
  grep -aE '^(host|shim|main|host |OnLaunched|loaded|text|button|exit|exception|unhandled)' "$log" >> summary 2>/dev/null || echo "no output" >> summary
done
"$build/server/wineserver" -k
echo done >> summary
