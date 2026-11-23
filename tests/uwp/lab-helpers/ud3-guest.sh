#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab, guest side (needs the ntdll variant that prints LAB av for c000001d): normal run (prefix), ud2 at the RVAS of the installed Microsoft.UI.Input.dll, launch with +seh,+loaddll, map the hits
g=/home/debian/uwp-u1-20261111
b=$g/build/candidates/u12/build
run=$g/build/candidates/u12/wasdk-run
inst="$run/prefix/drive_c/Program Files/WindowsApps/Microsoft.WindowsAppRuntime.2_2.7.0.0_x64__8wekyb3d8bbwe/Microsoft.UI.Input.dll"
CAND=u12 bash $g/claude/wasdk-run.sh '' 'err+all' > /dev/null 2>&1
grep -a -E 'wexappx|family' $run/summary | head -4
cd $g/claude && python3 mkpatch3.py "$inst" ${RVAS:-aa770 9bf60 9c080 9c180 a37c0} | tail -8
cd $run
export WINEPREFIX=$run/prefix WINESERVER=$b/server/wineserver DISPLAY=:99 WINEDEBUG=err+all,fixme+all,+loaddll,+seh
timeout --kill-after=10s 60s $b/wine $g/claude/applaunch.exe 'Microsoft.WindowsNotepad_8wekyb3d8bbwe!App' > ud.log 2>&1
$b/server/wineserver -k
for r in $(grep -a -o 'LAB av rip [0-9A-F]*' ud.log | awk '{print $4}' | uniq); do python3 $g/claude/mapaddr.py ud.log $r; done | grep -v 'applicationmodel' | head -12
grep -a -E 'Unhandled' ud.log | head -3 | cut -c1-200
