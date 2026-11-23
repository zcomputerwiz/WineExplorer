#!/bin/bash
# guest side: normal run (prefix), ud2 at the function entries and inline sites of the installed Microsoft.UI.Input.dll, launch with the trampoline ntdll
g=/home/debian/uwp-u1-20261111
b=$g/build/candidates/u12/build
run=$g/build/candidates/u12/wasdk-run
inst="$run/prefix/drive_c/Program Files/WindowsApps/Microsoft.WindowsAppRuntime.2_2.7.0.0_x64__8wekyb3d8bbwe/Microsoft.UI.Input.dll"
CAND=u12 bash $g/claude/wasdk-run.sh '' 'err+all' > /dev/null 2>&1
cd $g/claude && python3 mkpatch3.py "$inst" 2b020 2b080 2b210 2b2b0 2a44a 2aa87 2ab3f 2acc5 2b08e $(cat $g/claude/rvas.txt) | tail -3
cd $run
export WINEPREFIX=$run/prefix WINESERVER=$b/server/wineserver DISPLAY=:99 WINEDEBUG=err+all,+loaddll
timeout --kill-after=10s 70s $b/wine $g/claude/applaunch.exe 'Microsoft.WindowsNotepad_8wekyb3d8bbwe!App' > tramp.log 2>&1
$b/server/wineserver -k
grep -a -c LABTRAMP tramp.log
grep -a -E 'LABTRAMP' tramp.log | sed 's/^[0-9a-f]*:err:seh:dispatch_exception //' > tramp-lines.txt
head -2 tramp-lines.txt
grep -a -E 'Unhandled|noexcept' tramp.log | head -3 | cut -c1-200
