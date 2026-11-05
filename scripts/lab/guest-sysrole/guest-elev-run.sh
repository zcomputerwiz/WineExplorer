#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# LAB ONLY. Root on the guest: as wexadm (filtered administrator), start the prefix like the real session
# (hold client + secure desktop holder), then run "runas_wait <image> <args...>" with the arguments of this script.
# Output goes to /tmp/gsr-out/elev.log
R=/opt/wex-rt2/build/wow64
mkdir -p /run/user/1002 /tmp/gsr-out; chown wexadm:wexadm /run/user/1002; chmod 700 /run/user/1002; chmod 777 /tmp/gsr-out
U=(runuser -u wexadm -- env -i HOME=/home/wexadm USER=wexadm LOGNAME=wexadm PATH=/usr/bin:/bin LANG=C.UTF-8 XDG_RUNTIME_DIR=/run/user/1002 WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d;winex11.drv=d;winewayland.drv=d' WINEDEBUG=${WEXDEBUG:--all})
"${U[@]}" $R/server/wineserver -k 2>/dev/null; sleep 1
("${U[@]}" $R/loader/wine Z:\\opt\\wex-lab-sdprobe64.exe hold > /tmp/gsr-out/sdhold.log 2>&1 &)
sleep 12
echo "== ids"; for p in $(pgrep -x wineserver); do awk '/^Gid:/' /proc/$p/status; done
echo "== run: $*"
# Error 1055 (service database locked) means the machine root is still auto-starting services (the installed Chrome
# updater services make that take a minute): retry until the broker answers.
for try in 1 2 3 4 5 6 7 8; do
    timeout ${WEXTIMEOUT:-170} "${U[@]}" $R/loader/wine Z:\\opt\\wex-lab-runas_wait.exe "$@" > /tmp/gsr-out/elev.log 2>&1
    rc=$?
    grep -aq 'runas failed: error 1055' /tmp/gsr-out/elev.log || break
    echo "runas 1055, retry $try"; sleep 15
done
echo "exit $rc"
tail -15 /tmp/gsr-out/elev.log | cut -c1-200
