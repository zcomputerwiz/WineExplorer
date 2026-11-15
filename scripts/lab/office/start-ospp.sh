#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Put the copy made by grab-ospp.sh back into the prefix NAME and start Office's licensing service by
# itself, traced.  Log: ~/office/logs/NAME-ospp.log
#   start-ospp.sh NAME DISPLAY      WINEDEBUG selects the channels
set -uo pipefail
name=$1 disp=$2
root=$HOME/office
export WINEPREFIX=$root/pfx-$name DISPLAY=:$disp
dir="$WINEPREFIX/drive_c/Program Files/Common Files/Microsoft Shared/OfficeSoftwareProtectionPlatform"
copy=$root/ospp-copy
wineserver -k 2>/dev/null
sleep 2
mkdir -p "$dir"
cp -a "$copy/files/." "$dir/"
debug=${WINEDEBUG:-err+all,fixme-all,+service}
WINEDEBUG=-all wine reg import "$(winepath -w "$copy/ospp.reg")" /reg:64 > /dev/null 2>&1
WINEDEBUG=-all wine reg import "$(winepath -w "$copy/service.reg")" > /dev/null 2>&1
wineserver -k 2>/dev/null
sleep 2
WINEDEBUG=$debug timeout 120 wine net start osppsvc > "$root/logs/$name-ospp.log" 2>&1
echo "net start exit: $?"
sleep 5
WINEDEBUG=-all wine sc query osppsvc 2>/dev/null | tr -d '\r' | grep -a 'STATE\|EXIT'
ls -la "$root/logs/$name-ospp.log"
