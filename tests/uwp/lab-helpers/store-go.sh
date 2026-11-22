#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Host side (Git Bash): start store-launch.sh on the guest and print summary2 when it is done.
# usage: store-go.sh [WINEDEBUG]     (env: REGISTER, LAUNCH_SECONDS are passed on)
here=$(dirname "$0")
pad=/c/Users/Owner/AppData/Local/Temp/claude/f10dd599a76cdd8a/cc6dd05f-7e73-49f2-be6b-aa4119777252/scratchpad
cp "$here/store-launch.sh" "$pad/"
"$here/lab.sh" push store-launch.sh
"$here/lab.sh" ssh "cd /home/debian/uwp-u1-20261111/claude && rm -f ../build/candidates/u11-2/store-run/summary2 && (APPID='$APPID' STACK='$STACK' HELLO='$HELLO' WINEBOOT_UPDATE='$WINEBOOT_UPDATE' REGISTER='$REGISTER' LAUNCH_SECONDS='$LAUNCH_SECONDS' DISPLAY=:99 setsid nohup bash store-launch.sh $1 > /dev/null 2>&1 &); echo started"
"$here/lab.sh" wait 'f=/home/debian/uwp-u1-20261111/build/candidates/u11-2/store-run/summary2; for i in $(seq 1 120); do grep -q "^done" $f 2>/dev/null && break; sleep 3; done; cat $f' 400
