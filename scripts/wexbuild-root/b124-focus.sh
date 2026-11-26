#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# b124-focus.sh: the keyboard focus of a program that runs across a shell restart (B124), without a VM: headless wexcomp
# (COMP) + explorer /shell on the Wayland driver + focus-probe.exe (scripts/focus-probe.c, built to /root), the shell
# killed and started again. Prints the probe's foreground/focus/activation: it must stay "FocusProbe" after the restart.
# Run in a private mount namespace (xwayland refuses WSLg's /tmp/.X11-unix): unshare -m bash -c 'mount -t tmpfs -o
# mode=1777 tmpfs /tmp/.X11-unix && bash b124-focus.sh'. The wow64 build tree is W.
# B124: keyboard focus of a program across a shell restart, headless wexcomp + explorer /shell + focus-probe.exe
RC=0
COMP=/root/wexcomp-dev/build/labwc
W=/root/WE-chrome/build/wow64
R=$(mktemp -d /tmp/wex-b124-XXXXXX); chmod 700 $R
export XDG_RUNTIME_DIR=$R WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 LABWC_WEX_SHELL_SOCKET=wex-shell-0 WEX_SHELL_SOCKET=wex-shell-0
unset DISPLAY
pkill -9 -x wineserver; pkill -9 -x services.exe; sleep 1
mkdir -p $R/cfg
cat > $R/cfg/rc.xml <<'EOF'
<?xml version="1.0"?>
<labwc_config>
  <core><decoration>none</decoration></core>
  <placement><policy>center</policy></placement>
</labwc_config>
EOF
$COMP -C $R/cfg > $R/comp.log 2>&1 &
CPID=$!
for i in $(seq 1 60); do [ -S $R/wex-shell-0 ] && break; sleep 0.2; done
[ -S $R/wex-shell-0 ] || { echo "compositor socket missing"; head $R/comp.log; kill $CPID; exit 1; }
WAYLAND_DISPLAY=wex-shell-0 wlr-randr --output HEADLESS-1 --custom-mode 1280x800 >/dev/null 2>&1 || true
export WAYLAND_DISPLAY=wex-shell-0 WEX_WAYLAND_NATIVE_WINDOWS=1
export WINEPREFIX=/root/b124-pfx WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=${WINEDEBUG:--all}
L=/root/b124-wine.log; rm -f $L
rm -rf $WINEPREFIX
cd $W
timeout 150 ./wine wineboot -i >>$L 2>&1
./wine reg add 'HKCU\Software\Wine\Drivers' /v Graphics /t REG_SZ /d wayland /f >>$L 2>&1
timeout 30 ./server/wineserver -w
PL=$WINEPREFIX/drive_c/focus-probe.log
rm -f $PL $WINEPREFIX/drive_c/focus.stop
(timeout 200 ./wine explorer.exe /shell >>$L 2>&1 &)
sleep 15
# case 1, a normal start (nothing else in front): the desktop view is the foreground window and has the keyboard focus
QL=$WINEPREFIX/drive_c/focus-query.log; rm -f $QL
timeout 30 ./wine Z:/root/focus-probe.exe query 'C:\focus-query.log' >>$L 2>&1
echo "--- normal start: $(cat $QL)"
grep -q "foreground=SHELLDLL_DefView focus=SHELLDLL_DefView" $QL && echo "PASS normal start: the desktop is active" || { echo "FAIL normal start: the desktop is not active"; RC=1; }
(timeout 150 ./wine Z:/root/focus-probe.exe 'C:\focus-probe.log' >>$L 2>&1 &)
sleep 8
echo "--- before the kill"; tail -n 6 $PL
grim /root/b124-1.png
pkill -9 -f 'C:.windows.system32.explorer.exe /shell'
sleep 3
(timeout 150 ./wine explorer.exe /shell >>$L 2>&1 &)
sleep 15
echo "--- after the restart"; tail -n 8 $PL
grim /root/b124-2.png
# case 2, a restart (a program in front): it keeps the foreground and the focus
tail -n 4 $PL | grep -q "class=FocusProbe" && ! grep -q "WM_ACTIVATE inactive" $PL && echo "PASS restart: the program keeps the foreground and focus" || { echo "FAIL restart: the program lost the foreground or focus"; RC=1; }
touch $WINEPREFIX/drive_c/focus.stop
sleep 2
echo "--- whole probe log"; grep -v tick $PL | uniq -f1 | head -40
grep -ai "error\|assert\|warn" $R/comp.log | head -5
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe
kill $CPID 2>/dev/null; wait $CPID 2>/dev/null
cp $R/comp.log /root/b124-comp.log; rm -rf $R
exit $RC
