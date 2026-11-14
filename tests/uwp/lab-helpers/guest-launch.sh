#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Start a workspace script detached from the SSH session. usage: guest-launch.sh LOGNAME SCRIPT [ARGS...]
set -euo pipefail
root=/home/debian/uwp-u1-20261111
log="$root/claude/logs/$1"
shift
mkdir -p "$root/claude/logs"
setsid nohup bash "$@" > "$log.out" 2>&1 < /dev/null &
echo "started pid $! log $log.out"
