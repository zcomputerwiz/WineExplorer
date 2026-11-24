#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Shared shell library for WineExplorer scripts.
# Sourcing this file has no side effects.
#
# Scripts find this library as:
#   . "${WEX_LIB_DIR:-/usr/local/lib/wineexplorer}/wex-common.sh" 2>/dev/null || . "$(dirname -- "$(readlink -f -- "$0")")/lib/wex-common.sh"

# Prints the machine root. Order:
# 1. $WEX_MACHINE_ROOT if set.
# 2. The value of exactly one machine_root= line in /etc/wineexplorer/sysrun.conf
#    (or $WEX_SYSRUN_CONF if set) if the file is a regular, root-owned file
#    not writable by group/others (stat -c), refusing a repeated key.
# 3. /var/lib/wineexplorer/machine.
wex_machine_root() {
    if [ -n "${WEX_MACHINE_ROOT:-}" ]; then
        printf '%s\n' "$WEX_MACHINE_ROOT"
        return 0
    fi

    local conf="${WEX_SYSRUN_CONF:-/etc/wineexplorer/sysrun.conf}"
    local default_root="/var/lib/wineexplorer/machine"

    if [ -f "$conf" ] && [ ! -L "$conf" ]; then
        local st_info uid mode
        st_info=$(stat -c '%u %a' "$conf" 2>/dev/null) || { printf '%s\n' "$default_root"; return 0; }
        uid=${st_info% *}
        mode=${st_info#* }

        # Root-owned check: in production (/etc/wineexplorer/sysrun.conf), uid must be 0.
        # When WEX_SYSRUN_CONF is set, allow non-root owner for testing unless stat returns 0.
        if [ "$uid" -ne 0 ] && [ -z "${WEX_SYSRUN_CONF:-}" ]; then
            printf '%s\n' "$default_root"
            return 0
        fi

        # Not writable by group or others: mode & 022 must be 0
        if [ "$(( 0$mode & 022 ))" -ne 0 ]; then
            printf '%s\n' "$default_root"
            return 0
        fi

        # Exactly one machine_root= line, refusing repeated keys
        local count
        count=$(grep -c '^machine_root=' "$conf" 2>/dev/null) || count=0
        if [ "$count" -eq 1 ]; then
            local val
            val=$(sed -n 's/^machine_root=//p' "$conf" 2>/dev/null | tr -d '\r')
            case "$val" in
                /*)
                    printf '%s\n' "$val"
                    return 0
                    ;;
            esac
        fi
    fi

    printf '%s\n' "$default_root"
    return 0
}

# Prints the value lines of one key of a Wine .reg hive, from the line after
# [KEY] (case-insensitive match on the bracketed name, ignoring the timestamp after ])
# up to the next line that starts with [.
# Takes the hive's .reg.lock with flock -s if present (shared lock, short timeout).
wex_reg_section() {
    local file="${1:-}"
    local key="${2:-}"
    [ -n "$file" ] && [ -f "$file" ] || return 1
    [ -n "$key" ] || return 1

    local lock=""
    local dir
    dir=$(dirname -- "$file")
    if [ -e "$dir/.reg.lock" ]; then
        lock="$dir/.reg.lock"
    elif [ -e "$file.lock" ]; then
        lock="$file.lock"
    fi

    (
        if [ -n "$lock" ] && [ -e "$lock" ] && command -v flock >/dev/null 2>&1; then
            exec 9<"$lock" 2>/dev/null && flock -s -w 10 9 2>/dev/null || true
        fi
        TARGET_KEY="$key" awk '
            BEGIN {
                target = tolower(ENVIRON["TARGET_KEY"])
                gsub(/\\/, "/", target)
                gsub(/\/+/, "/", target)
            }
            /^\[/ {
                if (in_key) exit
                line = $0
                sub(/^\[/, "", line)
                sub(/\].*/, "", line)
                gsub(/\\/, "/", line)
                gsub(/\/+/, "/", line)
                line = tolower(line)
                if (line == target) {
                    in_key = 1
                    next
                }
            }
            in_key {
                print
            }
        ' "$file"
    )
}

# Set 640x480 resolution for Safe Mode or VGA mode via xrandr.
wex_low_resolution() {
    local output
    output=$(xrandr --current 2>/dev/null | awk '$2 == "connected" && / primary / { print $1; exit }')
    [ -n "$output" ] || output=$(xrandr --current 2>/dev/null | awk '$2 == "connected" { print $1; exit }')
    [ -n "$output" ] || return 0
    xrandr --output "$output" --mode 640x480 2>/dev/null || echo "$output offers no 640x480 mode"
}

# Prints the parts of a user's prefix that are links into the machine root (docs/PREFIX_SPLIT.md), one per line,
# relative to both: the machine folders under drive_c, then the shared hives. Setup's template prefix and the
# migration of an old prefix both link these; each caller decides what to do with a part the machine root lacks.
wex_machine_links() {
    printf '%s\n' "drive_c/windows" "drive_c/Program Files" "drive_c/Program Files (x86)" "drive_c/ProgramData" \
        "drive_c/users/Public" "system.reg" "userdef.reg"
}

# Returns 0 if prefix needs wineboot -u update, 1 otherwise.
# Checks WEX_NTDLL (or default /usr/local/lib/wine/x86_64-unix/ntdll.so) and
# MACHINE_ROOT/.update-timestamp against PREFIX/.update-timestamp.
wex_prefix_needs_update() {
    local prefix="${1:-}"
    local machine_root="${2:-}"
    [ -n "$prefix" ] || return 1
    [ -n "$machine_root" ] || machine_root=$(wex_machine_root)

    local ntdll="${WEX_NTDLL:-/usr/local/lib/wine/x86_64-unix/ntdll.so}"
    local pfx_stamp="$prefix/.update-timestamp"

    if [ -f "$ntdll" ]; then
        if [ ! -e "$pfx_stamp" ] || [ "$ntdll" -nt "$pfx_stamp" ]; then
            return 0
        fi
    fi

    if [ -n "$machine_root" ] && [ -e "$machine_root/.update-timestamp" ]; then
        if [ ! -e "$pfx_stamp" ] || [ "$machine_root/.update-timestamp" -nt "$pfx_stamp" ]; then
            return 0
        fi
    fi

    return 1
}
