#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# "Enable Boot Logging" and every Safe Mode start (docs/SAFE_MODE.md): append this boot's drivers and failed services
# to ntbtlog.txt in the Windows directory of the machine root, as XP appends to %SystemRoot%\ntbtlog.txt. Kernel
# modules stand for drivers; services that failed are listed as not started.
# The file has XP's format: UTF-16LE with a byte order mark at its start only, CRLF line ends, and for each start a
# header line (a space, the service pack string, then the date and time) with no empty line between starts. A log
# from before that format is converted once.
# Installed as /usr/local/sbin/wex-bootlog and run by wex-bootlog.service.
set -u
windir=${WEX_BOOTLOG_WINDIR:-/var/lib/wineexplorer/machine/drive_c/windows}
[ -d "$windir" ] || windir=/var/log/wineexplorer
mkdir -p "$windir"
log="$windir/ntbtlog.txt"
system_reg=${WEX_BOOTLOG_SYSTEM_REG:-$windir/../../system.reg}
modules=${WEX_BOOTLOG_MODULES:-/proc/modules}

# The service pack string, as Windows programs see it: CSDVersion in the machine registry (may be empty)
csd=$(awk '
    /^\[/ { in_key = ($0 ~ /^\[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\]/) }
    in_key && /^"CSDVersion"="/ { sub(/^"CSDVersion"="/, ""); sub(/"[ \t\r]*$/, ""); print; exit }
' "$system_reg" 2>/dev/null)

if [ -s "$log" ] && [ "$(od -An -N2 -tx1 "$log" | tr -d ' \n')" != fffe ]; then
    # plain text with CRLF line ends, as written before this format
    { printf '\377\376'; iconv -f UTF-8 -t UTF-16LE "$log"; } > "$log.new" && mv -f "$log.new" "$log"
fi
[ -s "$log" ] || printf '\377\376' > "$log"

{
    # " Service Pack 311  8 2026 08:29:50.500" on XP: month and day two characters wide, right aligned
    echo " $csd$(date '+%_m %_d %Y %H:%M:%S.%3N')"
    while read -r module _; do
        path=$(modinfo -n "$module" 2>/dev/null) || path=$module
        echo "Loaded driver $path"
    done < "$modules"
    systemctl list-units --type=service --state=failed --no-legend --plain 2>/dev/null | while read -r unit _; do
        [ -n "$unit" ] && echo "Did not start service $unit"
    done
} | sed 's/$/\r/' | iconv -f UTF-8 -t UTF-16LE >> "$log"
