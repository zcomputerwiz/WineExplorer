#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Provision the WineExplorer system role in the QEMU guest (run as root on the guest).
# Inputs in /tmp/gsr: wex-sysrun (prebuilt launcher), repo/scripts/setup-machine-root.sh
set -eu
R=/opt/wex-rt2/build/wow64
SRC=/tmp/gsr/repo
export WEX_WINE_BIN=$R/loader/wine
export WEX_WINESERVER_BIN=$R/server/wineserver
export WEX_NTDLL=$R/dlls/ntdll/ntdll.so
export WEX_SYSRUN_BIN=/tmp/gsr/wex-sysrun
export WINEDEBUG=-all
export WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d;winex11.drv=d;winewayland.drv=d'
unset DISPLAY WAYLAND_DISPLAY
mkdir -p /run/user/0 && chmod 700 /run/user/0
export XDG_RUNTIME_DIR=/run/user/0
echo "== setup-machine-root"
bash $SRC/scripts/setup-machine-root.sh 2>&1 | tail -25
echo "== state"
getent group wexsys
ls -la /var/lib/wineexplorer/machine | head -12
ls -la /usr/local/libexec/wineexplorer /etc/wineexplorer
cat /etc/wineexplorer/sysrun.conf
