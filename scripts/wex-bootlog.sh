#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# "Enable Boot Logging" (docs/SAFE_MODE.md): append this boot's drivers and failed services to ntbtlog.txt in
# the Windows directory of the machine root, as XP appends to %SystemRoot%\ntbtlog.txt. Kernel modules stand for
# drivers; services that failed are listed as not started. CRLF line ends, for Notepad.
# Installed as /usr/local/sbin/wex-bootlog and run by wex-bootlog.service.
set -u
windir=${WEX_BOOTLOG_WINDIR:-/var/lib/wineexplorer/machine/drive_c/windows}
[ -d "$windir" ] || windir=/var/log/wineexplorer
mkdir -p "$windir"
log="$windir/ntbtlog.txt"

{
    echo "WineExplorer $(uname -r) boot log, $(date '+%m %d %Y %H:%M:%S')"
    while read -r module _; do
        path=$(modinfo -n "$module" 2>/dev/null) || path=$module
        echo "Loaded driver $path"
    done < /proc/modules
    systemctl list-units --type=service --state=failed --no-legend --plain 2>/dev/null | while read -r unit _; do
        [ -n "$unit" ] && echo "Did not start service $unit"
    done
    echo
} | sed 's/$/\r/' >> "$log"
