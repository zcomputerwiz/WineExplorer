#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# While an Office 2016 setup runs in Windows 7 mode in the prefix NAME, keep a copy of what it installs
# for Office's own licensing service (files and registry keys), because setup removes them again when
# the service does not start.  The copy goes to ~/office/ospp-copy (local only, never to the repository).
#   grab-ospp.sh NAME DISPLAY [WAIT]
set -uo pipefail
name=$1 disp=$2 wait=${3:-900}
root=$HOME/office
export WINEPREFIX=$root/pfx-$name DISPLAY=:$disp WINEDEBUG=-all
dir="$WINEPREFIX/drive_c/Program Files/Common Files/Microsoft Shared/OfficeSoftwareProtectionPlatform"
copy=$root/ospp-copy
for ((i = 0; i < wait; i++)); do
    [ -f "$dir/OSPPSVC.EXE" ] && break
    sleep 1
done
[ -f "$dir/OSPPSVC.EXE" ] || { echo "the service was not installed within $wait seconds"; exit 1; }
sleep 3
rm -rf "$copy"
mkdir -p "$copy"
cp -a "$dir" "$copy/files"
wine reg export 'HKLM\Software\Microsoft\OfficeSoftwareProtectionPlatform' "$(winepath -w "$copy/ospp.reg")" /y
wine reg export 'HKLM\System\CurrentControlSet\Services\osppsvc' "$(winepath -w "$copy/service.reg")" /y
ls -la "$copy" "$copy/files" | head -40
