#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Run one Windows program in an owned Office test prefix on an owned Xvfb display (guest side).
#   run.sh NAME DISPLAY_NUM TIMEOUT_S EXE [ARGS...]
# Prefix ~/office/pfx-NAME, log ~/office/logs/NAME-TIME.log.  WINE selects the loader (default: wine on PATH),
# WINEDEBUG the channels (default: errors only).  The Xvfb is left running for screenshots (shot.sh).
set -uo pipefail
name=$1 disp=$2 limit=$3
shift 3
root=$HOME/office
mkdir -p "$root/logs"
export WINEPREFIX=$root/pfx-$name DISPLAY=:$disp
export WINEDEBUG=${WINEDEBUG:-err+all,fixme-all}
unset WAYLAND_DISPLAY XAUTHORITY
if ! [ -e "/tmp/.X11-unix/X$disp" ]; then
    setsid nohup Xvfb ":$disp" -screen 0 1280x1024x24 -nolisten tcp > "$root/logs/xvfb-$disp.log" 2>&1 < /dev/null &
    sleep 2
fi
log=$root/logs/$name-$(date +%Y%m%d-%H%M%S).log
echo "log: $log"
timeout "$limit" "${WINE:-wine}" "$@" > "$log" 2>&1
status=$?
echo "exit: $status" | tee -a "$log"
exit $status
