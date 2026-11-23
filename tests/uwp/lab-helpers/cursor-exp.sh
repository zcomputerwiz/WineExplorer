#!/bin/bash
# lab experiment: move the pointer far from where Notepad's window appears, then run Notepad; usage: cursor-exp.sh X Y
g=/home/debian/uwp-u1-20261111
b=$g/build/candidates/u12/build
run=$g/build/candidates/u12/wasdk-run
x86_64-w64-mingw32-gcc -O2 -o $g/claude/setcursor.exe $g/claude/setcursor.c -luser32 || exit 1
CAND=u12 bash $g/claude/wasdk-run.sh '' 'err+all' > /dev/null 2>&1
cd $run
export WINEPREFIX=$run/prefix WINESERVER=$b/server/wineserver DISPLAY=:99 WINEDEBUG=-all
$b/wine $g/claude/setcursor.exe $1 $2 2>&1 | grep -v libEGL
export WINEDEBUG=err+all,+message
timeout --kill-after=10s 60s $b/wine $g/claude/applaunch.exe 'Microsoft.WindowsNotepad_8wekyb3d8bbwe!App' > cur.log 2>&1
$b/server/wineserver -k
echo "NCHITTEST: $(grep -a -c 'WM_NCHITTEST' cur.log)  SETCURSOR: $(grep -a -c 'WM_SETCURSOR' cur.log)"
grep -a -n -E 'Unhandled exception|noexcept' cur.log | head -3 | cut -c1-160
grep -a -c . cur.log
