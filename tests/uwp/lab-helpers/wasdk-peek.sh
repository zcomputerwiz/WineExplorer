#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: start the Store app in the prefix store-run left, let it settle for 25 s, then look at what it is doing: its threads (wchan,
# cpu time), the Wine debug channels asked for in WINEDEBUG and a window list. Output: store-run/peek.txt
# usage: store-peek.sh [WINEDEBUG]
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/${CAND:-u12}/build
c=$root/claude
run=$root/build/candidates/${CAND:-u12}/wasdk-run
cd "$run"
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" DISPLAY=${DISPLAY:-:99}
"$build/server/wineserver" -k 2>/dev/null; sleep 1
family=Microsoft.WindowsNotepad_8wekyb3d8bbwe
export WINEDEBUG="${1:-err+all}" XAMLSHIM_TRACE=1 XAMLSHIM_CALLS=1
( timeout --kill-after=10s 70s "$build/wine" $c/applaunch.exe "$family!App" > peek.log 2>&1 & )
sleep 25
{
  pid=$(pgrep -f 'Notepad.exe' | head -1)
  echo "pid $pid"
  for t in /proc/$pid/task/*; do
    echo "$(basename $t) $(cat $t/comm) wchan=$(cat $t/wchan 2>/dev/null) $(awk '{print "utime="$14" stime="$15}' $t/stat)"
  done
  echo "--- processes"
  ps -eo pid,ppid,etime,pcpu,cmd | grep -E 'wine|\.exe' | grep -v grep | cut -c1-140
  echo "--- windows"
  xwininfo -root -tree -display "$DISPLAY" 2>/dev/null | head -20
} > peek.txt 2>&1
sleep 5
"$build/server/wineserver" -k
echo done >> peek.txt
