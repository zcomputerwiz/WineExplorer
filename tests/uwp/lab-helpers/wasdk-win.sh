#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: start the application of the prefix wasdk-run (Notepad by default) and list the windows of the desktop 25 s later
# (enumwin.exe), then take a screenshot of the X screen. Output: wasdk-run/win.txt, win.png.
# usage: wasdk-win.sh [APP_FAMILY]    (env: CAND)
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/${CAND:-u12}/build
c=$root/claude
run=$root/build/candidates/${CAND:-u12}/wasdk-run
cd "$run"
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" DISPLAY=${DISPLAY:-:99} WINEDEBUG=${WD:-err+all}
"$build/server/wineserver" -k 2>/dev/null; sleep 1
family=${1:-Microsoft.WindowsNotepad_8wekyb3d8bbwe}
x86_64-w64-mingw32-gcc -O2 -o enumwin.exe $c/enumwin.c -luser32 > win-build.log 2>&1
if [ -n "$RELAY_FROM" ]; then
  WINEDEBUG=-all "$build/wine" reg add 'HKCU\Software\Wine\Debug' /v RelayFromInclude /t REG_SZ /d "$RELAY_FROM" /f > relay-reg.log 2>&1
  "$build/server/wineserver" -w
fi
( timeout --kill-after=10s 80s "$build/wine" $c/applaunch.exe "$family!App" > win-app.log 2>&1 & )
sleep ${WAIT:-30}
{
  for pid in $(ps -eo pid,stat,cmd | grep WindowsApps | grep Notepad.exe | grep -v grep | awk '$2 !~ /Z/ {print $1}'); do
    echo "process $pid: $(ps -o etime=,pcpu= -p $pid)"
    for t in /proc/$pid/task/*; do
      echo "  $(basename $t) $(cat $t/comm) wchan=$(cat $t/wchan 2>/dev/null) cpu=$(awk '{print $14"/"$15}' $t/stat)"
    done
  done
  "$build/wine" enumwin.exe 2>&1
  echo "enumwin rc=$?"
  xwd -root -silent -display "$DISPLAY" | convert xwd:- win.png 2>&1
  ls -la win.png
} > win.txt 2>&1
sleep 2
"$build/server/wineserver" -k
echo done >> win.txt
