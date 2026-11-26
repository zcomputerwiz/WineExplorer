#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# b121-x11-survivors.sh: B121, without a VM. On Xvfb, explorer /desktop=Default /shell (the X11 session's shell) and
# xwin-probe.exe (scripts/xwin-probe.c, built to /root: it creates a window every second); the shell is killed with -9
# and started again. The probe must still run and keep creating windows, and the new shell must be the only one's
# windows (the dead one's are gone). Needs WINE_X11_RETAIN_DESKTOP=1 (the session scripts set it). The wow64 build is W.
RC=0
W=/root/WE-chrome/build/wow64
export WINE_X11_RETAIN_DESKTOP=${WINE_X11_RETAIN_DESKTOP:-1} DISPLAY=:91 WINEPREFIX=/root/b121-pfx WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=${WINEDEBUG:--all}
unset WAYLAND_DISPLAY
pkill -9 -x Xvfb; pkill -9 -x wineserver; pkill -9 -x services.exe; sleep 1
Xvfb :91 -screen 0 1280x800x24 >/root/b121-xvfb.log 2>&1 &
XPID=$!; sleep 2
L=/root/b121-wine.log; rm -f $L
rm -rf $WINEPREFIX; cd $W
timeout 150 ./wine wineboot -i >>$L 2>&1
timeout 30 ./server/wineserver -w
PL=$WINEPREFIX/drive_c/xwin-probe.log
rm -f $PL $WINEPREFIX/drive_c/xwin.stop
(timeout 200 ./wine explorer.exe /desktop=Default,1024x768 /shell >>$L 2>&1 &)
sleep 12
(timeout 150 ./wine Z:/root/xwin-probe.exe 'C:\xwin-probe.log' >>$L 2>&1 &)
sleep 6
before=$(grep -c "created .* ok" $PL)
pkill -9 -f 'C:.windows.system32.explorer.exe /desktop'
sleep 3
(timeout 150 ./wine explorer.exe /desktop=Default,1024x768 /shell >>$L 2>&1 &)
sleep 15
after=$(grep -c "created .* ok" $PL)
echo "windows created before the kill: $before, in total: $after"
if [ "$after" -ge $((before + 8)) ] && ! grep -q "FAILED\|ended" $PL && pgrep -f xwin-probe >/dev/null; then
    echo "PASS the program keeps creating windows after the shell restart"
else RC=1; echo "FAIL the program stopped or cannot create windows"; tail -n 4 $PL; fi
# a second and third kill in a row: the new manager must retain the desktop for the next restart as well
for n in 2 3; do
    pkill -9 -f 'C:.windows.system32.explorer.exe /desktop'
    sleep 3
    (timeout 150 ./wine explorer.exe /desktop=Default,1024x768 /shell >>$L 2>&1 &)
    sleep 15
    again=$(grep -c "created .* ok" $PL)
    echo "windows created after kill $n: $again"
    if [ "$again" -ge $((after + 8)) ] && ! grep -q "FAILED\|ended" $PL; then echo "PASS kill $n: the program still creates windows"
    else RC=1; echo "FAIL kill $n: the program stopped"; tail -n 3 $PL; fi
    after=$again
done
# the new shell's windows are there once: one taskbar-sized window at the bottom, one WineAppBar
bars=$(xwininfo -root -tree 2>/dev/null | grep -c '"WineAppBar"')
if [ "$bars" = 1 ]; then echo "PASS one shell's windows on the desktop"; else RC=1; echo "FAIL $bars WineAppBar windows (the dead shell's were not removed)"; fi
touch $WINEPREFIX/drive_c/xwin.stop; sleep 2
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe; kill $XPID
exit $RC
