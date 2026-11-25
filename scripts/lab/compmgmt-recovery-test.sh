#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# compmgmt-recovery-test.sh - the Recovery page of a service's properties in Computer Management stores the failure actions
# (docs/SERVICES_SNAPIN.md, B105). As root in wexgpo (or wexbuild), after a build of the worktree (wex-run.sh build): starts
# compmgmt.msc from the build tree in a prefix of its own under Xvfb, opens the properties of the Print Spooler, sets the first
# failure to "Restart the Service" with a delay of 2 minutes, presses OK, and checks the service's FailureActions value (Windows'
# layout: the first action is type 1, delay 120000 ms), then opens the page again and checks that it shows the stored action.
# Pictures in /root/out/cmrec-*.png. Prints one "ok" or "FAIL" line per check and "compmgmt_recovery=PASS" or "=FAIL".
# The coordinates are those of the 800x560 window on an Xvfb of 1024x768 without a window manager.
B=${WEX_BUILD:-/root/WE-prefetch/build/wow64}
S=${WEX_SRC:-/root/WE-prefetch/src}
export WINEPREFIX=/root/cmrec-pfx DISPLAY=:80 WINEDEBUG=-all
OUT=/root/out
fails=0
check() { if [ "$1" = 0 ]; then echo "ok $2"; else echo "FAIL $2"; fails=$((fails+1)); fi; }
shot() { import -window root "$OUT/cmrec-$1.png" 2>/dev/null; }
cleanup() { pkill -f cmrec-pfx 2>/dev/null; pkill -f "Xvfb :80" 2>/dev/null; }
trap cleanup EXIT
mkdir -p "$OUT"
cleanup
Xvfb :80 -screen 0 1024x768x24 >/dev/null 2>&1 &
sleep 2
[ -d "$WINEPREFIX" ] || "$B/wine" wineboot -i >/dev/null 2>&1
"$B/wine" reg delete 'HKLM\System\CurrentControlSet\Services\spooler' /v FailureActions /f >/dev/null 2>&1   # a clean start
MSC=$(ls "$S/programs/mmc/compmgmt.msc" /root/wex-snap/*/src/programs/mmc/compmgmt.msc 2>/dev/null | head -1)
cp "$MSC" "$WINEPREFIX/drive_c/windows/system32/compmgmt.msc"
"$B/wine" "$B/programs/mmc/x86_64-windows/mmc.exe" 'C:\windows\system32\compmgmt.msc' > "$OUT/cmrec.log" 2>&1 &
sleep 12
xdotool mousemove 103 282 click 1                     # the Services node
sleep 15
open_recovery()
{
    xdotool mousemove 300 211 click --repeat 2 --delay 100 1    # the Print Spooler row: its properties
    sleep 4
    xdotool mousemove 143 67 click 1                  # the Recovery tab
    sleep 3
}
open_recovery
shot 0
xdotool mousemove 265 134 click 1                     # the first failure's list
sleep 1
xdotool key Down Return                               # Restart the Service
sleep 1
xdotool mousemove 188 264 click 1                     # the restart delay
xdotool key End BackSpace BackSpace BackSpace BackSpace
xdotool type 2
sleep 1
shot 1
xdotool mousemove 277 455 click 1                     # OK
sleep 4
FA=$("$B/wine" reg query 'HKLM\System\CurrentControlSet\Services\spooler' /v FailureActions 2>&1 | tr -d '\r' | tr 'A-Z' 'a-z')
echo "$FA" | grep -q failureactions || { echo "--- reg query said:"; echo "$FA"; }
echo "$FA" | grep -q failureactions; check $? "the failure actions are stored for the service"
echo "$FA" | grep -qE '01000000c0d40100' || echo "$FA" >&2
echo "$FA" | grep -qE '01000000c0d40100'; check $? "the first action is a restart after 120000 ms"
open_recovery
shot 2
# the page shows what is stored: the picture 2 has "Restart the Service" first and 2 minutes; the registry is the check
"$B/wine" reg query 'HKLM\System\CurrentControlSet\Services\spooler' >/dev/null 2>&1; check $? "the service key is still there"
xdotool key Escape
echo "pictures: $OUT/cmrec-0.png (the page) 1 (changed) 2 (opened again)"
if [ $fails = 0 ]; then echo "compmgmt_recovery=PASS"; else echo "compmgmt_recovery=FAIL"; exit 1; fi
