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
export WINEPREFIX="$run/prefix" WINESERVER="${WS:-$build/server/wineserver}" WSGDB_REAL="$build/server/wineserver" DISPLAY=${DISPLAY:-:99} WINEDEBUG=${WD:-err+all}
"$build/server/wineserver" -k 2>/dev/null; sleep 1
family=${1:-Microsoft.WindowsNotepad_8wekyb3d8bbwe}
x86_64-w64-mingw32-gcc -O2 -o enumwin.exe $c/enumwin.c -luser32 > win-build.log 2>&1
if [ -n "$RELAY_FROM" ]; then
  WINEDEBUG=-all "$build/wine" reg add 'HKCU\Software\Wine\Debug' /v RelayFromInclude /t REG_SZ /d "$RELAY_FROM" /f > relay-reg.log 2>&1
  "$build/server/wineserver" -w
fi
( timeout --kill-after=10s 80s "$build/wine" $c/applaunch.exe "$family!App" > win-app.log 2>&1 & )
if [ -n "$GDB_WS" ]; then
  for i in $(seq 1 40); do pgrep -x wineserver >/dev/null && break; sleep 0.5; done; sleep 2; ps -eo pid,comm | grep -i wine > ws-ps.log
  ws=
  for p in $(pgrep -x wineserver); do tr '\0' '\n' < /proc/$p/environ 2>/dev/null | grep -qx "WINEPREFIX=$run/prefix" && ws=$p; done
  [ -z "$ws" ] && ws=$(pgrep -n -x wineserver)
  echo "wineserver pid: $ws" > ws-gdb.log
  [ -n "$ws" ] && ( gdb -batch -nx -p $ws -ex "handle SIGPIPE nostop noprint pass" -ex "handle SIGCHLD nostop noprint pass" \
      -ex "handle SIGUSR1 nostop noprint pass" -ex "handle SIGALRM nostop noprint pass" -ex continue -ex "bt 25" \
      -ex "frame 1" -ex "p *packet" -ex "p packet->entry" -ex "p *packet->sync" -ex "p *packet->sync->ops" -ex "p packet->target->ops->name" -ex "p *packet->target" >> ws-gdb.log 2>&1 & )
fi
sleep ${WAIT:-30}
{
  for pid in $(ps -eo pid,stat,cmd | grep WindowsApps | grep Notepad.exe | grep -v grep | awk '$2 !~ /Z/ {print $1}'); do
    echo "process $pid: $(ps -o etime=,pcpu= -p $pid)"
    for t in /proc/$pid/task/*; do
      echo "  $(basename $t) $(cat $t/comm) wchan=$(cat $t/wchan 2>/dev/null) cpu=$(awk '{print $14"/"$15}' $t/stat)"
    done
  done
  echo "-- Wine processes (winedbg info process):"
  timeout 30 "$build/wine" winedbg --command "info process" 2>&1 | tr -d '\r' | grep -v "^[0-9a-f]*:fixme" | head -30
  "$build/wine" enumwin.exe 2>&1
  echo "enumwin rc=$?"
  import -display "$DISPLAY" -window root win.png 2>&1
  ls -la win.png
} > win.txt 2>&1
sleep 2
"$build/server/wineserver" -k
echo done >> win.txt
