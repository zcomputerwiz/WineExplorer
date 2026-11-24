#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab (owned U1 guest): run Notepad (lab copy of the package) on a candidate build: a fresh prefix through wasdk-run.sh, DLL
# overrides, the Segoe UI family mapped to Tahoma (or Selawik when installed) as the WineExplorer image does, then Notepad through
# applaunch for 60 s with a screenshot of the X display at 25 s.
#   notepad-run.sh "dll1 dll2" [WINEDEBUG]       e.g. notepad-run.sh riched20 'err+all,fixme+all,+dcomp,+loaddll'
# Environment: CAND (candidate, default u13), RELAY_FROM (a +relay filter written to the prefix), WINEDLLOVERRIDES.
# Output in the candidate's wasdk-run/: ovr.log (the Wine log of the Notepad run), ovr.png (the screenshot).
# The script and its helpers live in the guest workspace's claude/ directory (lab.sh push).
g=${UWP_WS:-/home/debian/uwp-u1-20261111}
cand=${CAND:-u13}
b=$g/build/candidates/$cand/build
run=$g/build/candidates/$cand/wasdk-run
CAND=$cand bash $g/claude/wasdk-run.sh '' 'err+all' > /dev/null 2>&1
cd $run
export WINEPREFIX=$run/prefix WINESERVER=$b/server/wineserver DISPLAY=:99 WINEDEBUG=-all
for d in $1; do $b/wine reg add 'HKCU\Software\Wine\DllOverrides' /v $d /t REG_SZ /d 'native,builtin' /f > /dev/null 2>&1; done
t=Tahoma; fc-list | grep -q -i selawik && t=Selawik
for n in 'Segoe UI' 'Segoe UI Variable' 'Segoe UI Variable Text' 'Segoe UI Variable Display' 'Segoe UI Variable Small'; do
  $b/wine reg add 'HKLM\Software\Wine\Fonts\Replacements' /v "$n" /t REG_SZ /d "$t" /f > /dev/null 2>&1
done
[ -n "$RELAY_FROM" ] && $b/wine reg add 'HKCU\Software\Wine\Debug' /v RelayFromInclude /t REG_SZ /d "$RELAY_FROM" /f > /dev/null 2>&1
$b/server/wineserver -w
export WINEDEBUG=${2:-err+all,fixme+all,+loaddll}
( sleep 25; xwd -root -silent -display "$DISPLAY" | convert xwd:- ovr.png ) &
timeout --kill-after=10s 60s $b/wine $g/claude/applaunch.exe 'Microsoft.WindowsNotepad_8wekyb3d8bbwe!App' > ovr.log 2>&1
$b/server/wineserver -k
echo "fonts mapped to $t; lines $(wc -l < ovr.log)"
grep -a -E 'RoFailFast|Unhandled|unimplemented function|not implemented\.$' ovr.log | cut -c1-200 | head -8
