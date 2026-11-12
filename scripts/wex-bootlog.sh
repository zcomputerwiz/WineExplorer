#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# "Enable Boot Logging" and every Safe Mode start (docs/SAFE_MODE.md): append this boot's drivers and failed services
# to ntbtlog.txt in the Windows directory of the machine root, as XP appends to %SystemRoot%\ntbtlog.txt. Kernel
# modules stand for drivers; services that failed are listed as not started.
# The file has XP's format: UTF-16LE with a byte order mark at its start only, CRLF line ends, and for each start a
# header line (a space, the version text, then the date and time) with no empty line between starts. A log from
# before that format is converted once.
# Installed as /usr/local/sbin/wex-bootlog and run by wex-bootlog.service.
set -u
windir=${WEX_BOOTLOG_WINDIR:-/var/lib/wineexplorer/machine/drive_c/windows}
[ -d "$windir" ] || windir=/var/log/wineexplorer
mkdir -p "$windir"
log="$windir/ntbtlog.txt"
system_reg=${WEX_BOOTLOG_SYSTEM_REG:-$windir/../../system.reg}
modules=${WEX_BOOTLOG_MODULES:-/proc/modules}

# The version Wine reports to Windows programs, from the machine registry. A service pack string (CSDVersion, as with
# winecfg's XP setting) gives XP's header. Without one (Wine's default, Windows 10) the product name, version and
# build stand in its place, followed by a space: " Windows 10 Pro Version 10.0 (Build 19045) 11 10 2026 ...".
version=$(awk '
    function hex(s,    n, i) {
        n = 0; s = tolower(s)
        for (i = 1; i <= length(s); i++) n = n * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
        return n
    }
    /^\[/ { if (in_key) exit; in_key = ($0 ~ /^\[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\]/); next }
    !in_key || !/^"/ { next }
    {
        name = $0; sub(/^"/, "", name); sub(/".*/, "", name)
        value = $0; sub(/^"[^"]*"=/, "", value); sub(/[ \t\r]*$/, "", value)
        if (value ~ /^"/) { sub(/^"/, "", value); sub(/"$/, "", value); str[name] = value }
        else if (value ~ /^dword:/) { sub(/^dword:/, "", value); dword[name] = hex(value) }
    }
    END {
        if (str["CSDVersion"] != "") { print str["CSDVersion"]; exit }
        if ("CurrentMajorVersionNumber" in dword)
            ver = dword["CurrentMajorVersionNumber"] "." (dword["CurrentMinorVersionNumber"] + 0)
        else
            ver = str["CurrentVersion"]
        build = str["CurrentBuildNumber"]
        if (build == "") build = str["CurrentBuild"]
        text = str["ProductName"]
        if (ver != "") text = text (text != "" ? " " : "") "Version " ver
        if (build != "") text = text (text != "" ? " " : "") "(Build " build ")"
        if (text != "") print text " "
    }
' "$system_reg" 2>/dev/null)

if [ -s "$log" ] && [ "$(od -An -N2 -tx1 "$log" | tr -d ' \n')" != fffe ]; then
    # plain text with CRLF line ends, as written before this format
    { printf '\377\376'; iconv -f UTF-8 -t UTF-16LE "$log"; } > "$log.new" && mv -f "$log.new" "$log"
fi
[ -s "$log" ] || printf '\377\376' > "$log"

{
    # " Service Pack 311  8 2026 08:29:50.500" on XP: month and day two characters wide, right aligned
    printf ' %s%s\n' "$version" "$(date '+%_m %_d %Y %H:%M:%S.%3N')"
    while read -r module _; do
        path=$(modinfo -n "$module" 2>/dev/null) || path=$module
        echo "Loaded driver $path"
    done < "$modules"
    systemctl list-units --type=service --state=failed --no-legend --plain 2>/dev/null | while read -r unit _; do
        [ -n "$unit" ] && echo "Did not start service $unit"
    done
} | sed 's/$/\r/' | iconv -f UTF-8 -t UTF-16LE >> "$log"
