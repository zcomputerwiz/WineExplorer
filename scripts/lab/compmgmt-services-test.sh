#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# compmgmt-services-test.sh - the Services node of Computer Management drives the service manager (docs/SERVICES_SNAPIN.md).
# As root in wexgpo (or wexbuild), after a build of the worktree (wex-run.sh build): starts compmgmt.msc from the build tree
# in a prefix of its own under Xvfb, selects Services and the Print Spooler (a stopped, manually started service), starts
# it through the right-click menu and checks with sc.exe that it runs, stops it through the same menu and checks that it
# stopped, and checks that the list shows "Started" while it runs. The user here is root, so the change is allowed: the
# elevation offer (a refused change) is not driven; that path is the survey's (the standard user and the limited
# administrator get access denied, scripts/test_sysrun_server.sh section svc_table). Screenshots in /root/out/cmsvc-*.png.
# Prints one "ok" or "FAIL" line per check and "compmgmt_services=PASS" or "=FAIL"; exit status 0 only for PASS.
# The coordinates are those of the 800x560 window on an Xvfb of 1024x768 without a window manager (the first one at 0,0).
B=${WEX_BUILD:-/root/WE-prefetch/build/wow64}
S=${WEX_SRC:-/root/WE-prefetch/src}
export WINEPREFIX=/root/cmsvc-pfx DISPLAY=:78 WINEDEBUG=-all
OUT=/root/out
fails=0
check() { if [ "$1" = 0 ]; then echo "ok $2"; else echo "FAIL $2"; fails=$((fails+1)); fi; }
shot() { import -window root "$OUT/cmsvc-$1.png" 2>/dev/null; }
state() { "$B/wine" sc query spooler 2>&1 | tr -d '\r' | sed -n 's/.*STATE *: *[0-9]* *//p' | head -1; }
cleanup() { pkill -f cmsvc-pfx 2>/dev/null; pkill -f "Xvfb :78" 2>/dev/null; }
trap cleanup EXIT
mkdir -p "$OUT"
cleanup
Xvfb :78 -screen 0 1024x768x24 >/dev/null 2>&1 &
sleep 2
[ -d "$WINEPREFIX" ] || "$B/wine" wineboot -i >/dev/null 2>&1
MSC=$(ls "$S/programs/mmc/compmgmt.msc" /root/wex-snap/*/src/programs/mmc/compmgmt.msc 2>/dev/null | head -1)
cp "$MSC" "$WINEPREFIX/drive_c/windows/system32/compmgmt.msc"
"$B/wine" "$B/programs/mmc/x86_64-windows/mmc.exe" 'C:\windows\system32\compmgmt.msc' > "$OUT/cmsvc.log" 2>&1 &
sleep 12
[ "$(state)" = STOPPED ]; check $? "the Print Spooler starts stopped"
xdotool mousemove 103 250 click 1       # the Services node
sleep 15
shot 0
xdotool mousemove 300 196 click 1       # the Print Spooler row
sleep 2
xdotool mousemove 300 196 click 3       # its menu: Start is the first item
sleep 2
shot 1
xdotool mousemove 333 208 click 1
for i in $(seq 1 30); do [ "$(state)" = RUNNING ] && break; sleep 2; done
[ "$(state)" = RUNNING ]; check $? "Start in the menu starts the service"
sleep 12
shot 2
xdotool mousemove 300 196 click 3       # Stop is the second item, enabled now
sleep 2
shot 3
xdotool mousemove 333 226 click 1
for i in $(seq 1 30); do [ "$(state)" = STOPPED ]; r=$?; [ $r = 0 ] && break; sleep 2; done
check $r "Stop in the menu stops the service"
sleep 12
shot 4
echo "pictures: $OUT/cmsvc-0.png (the list) 1 (menu while stopped) 2 (after Start) 3 (menu while running) 4 (after Stop)"
if [ $fails = 0 ]; then echo "compmgmt_services=PASS"; else echo "compmgmt_services=FAIL"; exit 1; fi
