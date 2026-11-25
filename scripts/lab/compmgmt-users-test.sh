#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# compmgmt-users-test.sh - the Users and Groups nodes of Computer Management come up (docs/LOCAL_USERS_DESIGN.md). As root in
# wexgpo (or wexbuild), after a build of the worktree (wex-run.sh build): starts compmgmt.msc from the build tree in a prefix
# of its own under Xvfb, selects Local Users and Groups, then Users and Groups, and saves pictures in /root/out/cmusr-*.png.
# This prefix has no session 0, so netapi32 answers with Wine's own list of accounts: the check is that the nodes show and
# the console stays up; the chain to the accounts service is the suite's (scripts/test_sysrun_server.sh, s0_accounts_chain).
# The coordinates are those of the 800x560 window on an Xvfb of 1024x768 without a window manager.
B=${WEX_BUILD:-/root/WE-prefetch/build/wow64}
S=${WEX_SRC:-/root/WE-prefetch/src}
export WINEPREFIX=/root/cmusr-pfx DISPLAY=:79 WINEDEBUG=-all
OUT=/root/out
fails=0
check() { if [ "$1" = 0 ]; then echo "ok $2"; else echo "FAIL $2"; fails=$((fails+1)); fi; }
shot() { import -window root "$OUT/cmusr-$1.png" 2>/dev/null; }
cleanup() { pkill -f cmusr-pfx 2>/dev/null; pkill -f "Xvfb :79" 2>/dev/null; }
trap cleanup EXIT
mkdir -p "$OUT"
cleanup
Xvfb :79 -screen 0 1024x768x24 >/dev/null 2>&1 &
sleep 2
[ -d "$WINEPREFIX" ] || "$B/wine" wineboot -i >/dev/null 2>&1
MSC=$(ls "$S/programs/mmc/compmgmt.msc" /root/wex-snap/*/src/programs/mmc/compmgmt.msc 2>/dev/null | head -1)
cp "$MSC" "$WINEPREFIX/drive_c/windows/system32/compmgmt.msc"
"$B/wine" "$B/programs/mmc/x86_64-windows/mmc.exe" 'C:\windows\system32\compmgmt.msc' > "$OUT/cmusr.log" 2>&1 &
sleep 12
shot 0
xdotool mousemove "${NODE_X:-103}" "${NODE_Y:-122}" click 1     # Local Users and Groups
sleep 3
shot 1
xdotool mousemove "${USERS_X:-110}" "${USERS_Y:-138}" click 1   # Users
sleep 4
shot 2
xdotool mousemove "${GROUPS_X:-110}" "${GROUPS_Y:-154}" click 1 # Groups
sleep 4
shot 3
pgrep -f mmc.exe >/dev/null; check $? "the console is still running"
grep -q "Unhandled exception" "$OUT/cmusr.log"; [ $? = 0 ] && check 1 "no crash" || check 0 "no crash"
echo "pictures: $OUT/cmusr-0.png (start) 1 (Local Users and Groups) 2 (Users) 3 (Groups)"
if [ $fails = 0 ]; then echo "compmgmt_users=PASS"; else echo "compmgmt_users=FAIL"; exit 1; fi
