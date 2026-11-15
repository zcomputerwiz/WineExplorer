#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Run one Windows program as the system account in session 0's machine root (a machine set up with
# WEX_SESSION0=1), the way a deployment tool installs software for all users.  Run as root.
#   run-session0.sh LABEL DISPLAY_NUM TIMEOUT_S EXE [ARGS...]
# Log /var/tmp/office-s0/LABEL-TIME.log.  The Xvfb is started without access control (lab only), so
# both the system account and the test user can use it.
set -uo pipefail
label=$1 disp=$2 limit=$3
shift 3
machine=/var/lib/wineexplorer/machine
out=/var/tmp/office-s0
mkdir -p "$out"
chmod 1777 "$out"
if ! [ -e "/tmp/.X11-unix/X$disp" ]; then
    setsid nohup Xvfb ":$disp" -ac -screen 0 1280x1024x24 -nolisten tcp > "$out/xvfb-$disp.log" 2>&1 < /dev/null &
    sleep 2
fi
log=$out/$label-$(date +%Y%m%d-%H%M%S).log
echo "log: $log"
runuser -u wexsys -- env -i PATH=/usr/local/bin:/usr/bin:/bin HOME=$machine WINEPREFIX=$machine DISPLAY=":$disp" \
    WINEDEBUG="${WINEDEBUG:-err+all,fixme-all}" "WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d" \
    timeout "$limit" wine "$@" > "$log" 2>&1
status=$?
chmod a+r "$log"
echo "exit: $status" | tee -a "$log"
exit $status
