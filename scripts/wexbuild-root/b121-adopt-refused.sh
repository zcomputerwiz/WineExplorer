#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# b121-adopt-refused.sh: B121, without a VM: the takeover of the retained desktop window fails (batch 1147: the shell
# restart left a blank full-screen "Wine Desktop" above the desktop and a second one in the window list). On Xvfb,
# explorer /desktop=Default /shell is killed with -9 and started again with WINE_X11_RETAIN_TEST_REFUSE=1 (the takeover
# is refused as if another client held the window's events). The new shell must then be the only desktop: one viewable
# top-level "Wine Desktop", one WineAppBar, and the dead shell's window gone; the log gives the reason. Run it with
# WINE_X11_RETAIN_TEST_REFUSE=0 for the plain takeover (the same counts). The wow64 build is W.
RC=0
W=${W:-/root/WE-chrome/build/wow64}
export WINE_X11_RETAIN_DESKTOP=1 DISPLAY=:92 WINEPREFIX=/root/b121r-pfx WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=${WINEDEBUG:--all,+x11drv}
REFUSE=${WINE_X11_RETAIN_TEST_REFUSE:-1}
unset WAYLAND_DISPLAY WINE_X11_RETAIN_TEST_REFUSE
pkill -9 -x Xvfb; pkill -9 -x wineserver; pkill -9 -x services.exe; sleep 1
Xvfb :92 -screen 0 1280x800x24 >/root/b121r-xvfb.log 2>&1 &
XPID=$!; sleep 2
L=/root/b121r-wine.log; rm -f $L
rm -rf $WINEPREFIX; cd $W
WINEDEBUG=-all timeout 150 ./wine wineboot -i >>$L 2>&1
timeout 30 ./server/wineserver -w
(timeout 200 ./wine explorer.exe /desktop=Default,1024x768 /shell >>$L 2>&1 &)
sleep 12
echo "before the kill: $(xwininfo -root -tree 2>/dev/null | grep -c '"Wine Desktop"') Wine Desktop windows"
pkill -9 -f 'C:.windows.system32.explorer.exe /desktop'
sleep 3
(WINE_X11_RETAIN_TEST_REFUSE=$REFUSE timeout 150 ./wine explorer.exe /desktop=Default,1024x768 /shell >>$L 2>&1 &)
sleep 15
tree=$(xwininfo -root -tree 2>/dev/null)
desk=$(echo "$tree" | grep '"Wine Desktop"' | grep -vc 'has no name')
bars=$(echo "$tree" | grep -c '"WineAppBar"')
echo "$tree" | grep '"Wine Desktop"\|"WineAppBar"'
if [ "$desk" = 1 ]; then echo "PASS one Wine Desktop window"; else RC=1; echo "FAIL $desk Wine Desktop windows"; fi
if [ "$bars" = 1 ]; then echo "PASS one WineAppBar"; else RC=1; echo "FAIL $bars WineAppBar windows"; fi
if [ "$REFUSE" != 0 ]; then
    if grep -q "cannot take over the desktop window" $L && grep -q "destroying the retained desktop window" $L; then
        echo "PASS the log names the refusal and the destroyed window"
    else RC=1; echo "FAIL no warning in the log"; grep -i "desktop window" $L | tail -n 5; fi
else
    if grep -q "taking over the desktop window" $L; then echo "PASS the window was taken over"
    else RC=1; echo "FAIL the plain takeover did not happen"; fi
fi
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe; kill $XPID
exit $RC
