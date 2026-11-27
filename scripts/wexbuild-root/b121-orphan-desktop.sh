#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# b121-orphan-desktop.sh: B121, without a VM: the shell is the only Wine program, so killing it with -9 ends its
# wineserver too (batch 1147, shellrestart: the supervisor then starts a fresh server). Nothing stored survives to take
# over, and the X server keeps the dead shell's windows (WINE_X11_RETAIN_DESKTOP): the desktop window, the taskbar, the
# desktop view. The new shell must be the only desktop: one "Wine Desktop" (the taskbar window does not show on headless Xvfb, so it is not counted). (With another Wine
# program running, the server survives: b121-x11-survivors.sh and b121-adopt-refused.sh.) The wow64 build is W.
RC=0
W=${W:-/root/WE-chrome/build/wow64}
export WINE_X11_RETAIN_DESKTOP=1 DISPLAY=:94 WINEPREFIX=/root/b121o-pfx WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=${WINEDEBUG:--all,+x11drv}
unset WAYLAND_DISPLAY
pkill -9 -x Xvfb; pkill -9 -x wineserver; pkill -9 -x services.exe; sleep 1
Xvfb :94 -screen 0 1280x800x24 >/root/b121o-xvfb.log 2>&1 &
XPID=$!; sleep 2
xterm -geometry 10x2+0+0 -e sleep 600 >/dev/null 2>&1 &   # another X client: the X server must not reset when the shell goes (it keeps the compositor and other clients)
TPID=$!; sleep 1
L=/root/b121o-wine.log; rm -f $L
rm -rf $WINEPREFIX; cd $W
WINEDEBUG=-all timeout 150 ./wine wineboot -i >>$L 2>&1
timeout 30 ./server/wineserver -w
(timeout 200 ./wine explorer.exe /desktop=Default,1024x768 /shell >>$L 2>&1 &)
sleep 15
pkill -9 -f 'C:.windows.system32.explorer.exe /desktop'
pkill -9 -x wineserver; pkill -9 -x services.exe     # the server goes with its last client
sleep 3
echo "after the kill, before the restart: $(xwininfo -root -tree 2>/dev/null | grep -c '"Wine Desktop"') Wine Desktop windows (the retained one)"
(timeout 150 ./wine explorer.exe /desktop=Default,1024x768 /shell >>$L 2>&1 &)
sleep 15
tree=$(xwininfo -root -tree 2>/dev/null)
desk=$(echo "$tree" | grep -c '"Wine Desktop"')
echo "$tree" | grep '"Wine Desktop"'
if [ "$desk" = 1 ]; then echo "PASS one Wine Desktop window"; else RC=1; echo "FAIL $desk Wine Desktop windows"; fi
grep "of a manager that is gone" $L | sed 's/^/  log: /' | head -3
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe; kill $TPID $XPID; rm -rf $WINEPREFIX
exit $RC
