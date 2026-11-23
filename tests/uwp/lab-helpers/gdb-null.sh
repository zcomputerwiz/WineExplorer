#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: launch Notepad (wasdk-win.sh), attach gdb (gdbnull.py) to every packaged Notepad process once it exists and stop at a
# call through a null pointer; dump the registers and the top of the stack of that thread. Output: gdb-null-PID.log
root=/home/debian/uwp-u1-20261111
R=$root/build/candidates/u12/wasdk-run
cd $root/claude
pkill -9 -f "WindowsApps.*Notepad.exe"; sleep 1
rm -f $R/win.txt $R/gdb-null-*.log
(WD=err+all WAIT=40 CAND=u12 DISPLAY=:99 setsid nohup bash wasdk-win.sh > /dev/null 2>&1 &)
sleep 3
declare -A seen
for i in $(seq 1 100); do
  for p in $(pgrep -f "WindowsApps.*Notepad.exe"); do
    [ -n "${seen[$p]}" ] && continue
    st=$(awk '{print $3}' /proc/$p/stat 2>/dev/null)
    [ "$st" = Z ] && continue
    seen[$p]=1
    ( timeout 70 gdb -batch -nx -p $p -x $root/claude/gdbnull.py > $R/gdb-null-$p.log 2>&1 & )
  done
  sleep 0.5
done
sleep 20
grep -l NULLCALL $R/gdb-null-*.log
