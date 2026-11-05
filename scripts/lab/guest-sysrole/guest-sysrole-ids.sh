#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root on the guest: start a wine client as wextest, then print uid/gid sets of wineserver and services.exe.
R=/opt/wex-rt2/build/wow64
U="runuser -u wextest -- env -i HOME=/home/wextest USER=wextest LOGNAME=wextest PATH=/usr/bin:/bin LANG=C.UTF-8 XDG_RUNTIME_DIR=/run/user/1001 WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d;winex11.drv=d;winewayland.drv=d WINEDEBUG=-all"
mkdir -p /run/user/1001; chown wextest:wextest /run/user/1001; chmod 700 /run/user/1001
$U $R/server/wineserver -k 2>/dev/null
sleep 2
($U $R/loader/wine cmd /c "ping -n 40 127.0.0.1" > /tmp/gsr/hold.log 2>&1 &)
sleep 12
for p in $(pgrep -x wineserver) $(pgrep -f services.exe); do
  echo "pid $p $(tr '\0' ' ' < /proc/$p/cmdline | cut -c1-80)"
  awk '/^Uid:|^Gid:/' /proc/$p/status
done
getent group wexsys; id wextest
cat /tmp/gsr/hold.log | head -5
