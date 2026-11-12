#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Last Known Good Configuration (docs/SAFE_MODE.md). XP keeps a copy of HKLM\SYSTEM\CurrentControlSet, the settings
# of drivers and services, from the last start that succeeded. "Last Known Good Configuration" in the Advanced Options
# menu goes back to that copy and keeps the settings it replaces as the failed control set; installed programs, user
# settings and files stay as they are. WineExplorer does the same with the machine registry, where Wine keeps the
# control set in System\ControlSet001 (System\CurrentControlSet links to it).
#
#   wex-lastgood save      wex-startup-check, once a start counts as successful: copy the control set
#   wex-lastgood restore   the Advanced Options menu, before any Wine program runs: put the copy back
#   wex-lastgood status    exit status 0 when there is a copy to go back to
#
# Installed as /usr/local/sbin/wex-lastgood. The WEX_* variables let scripts/test_safemode.py use other paths.
set -u
umask 077
machine=${WEX_LASTGOOD_MACHINE:-/var/lib/wineexplorer/machine}
state=${WEX_LASTGOOD_DIR:-/var/lib/wineexplorer/lastgood}
system_reg=$machine/system.reg
saved=$state/controlset.reg
failed=$state/failed-controlset.reg
lock=$machine/.reg.lock

# Wine's user servers save system.reg in place, not atomically, every 30 seconds, under an exclusive flock on the
# machine root's .reg.lock (the system tier's session 0, once on, saves by renaming instead). The copy is taken under
# a shared lock and the restore under an exclusive one, so neither a half-written file is copied nor a restore lost.
# No lock file: no server has used the machine root yet.
lock_registry() {   # lock_registry -s|-x
    [ -e "$lock" ] || return 0
    exec 9<"$lock" || return 0
    flock "$1" -w 120 9 || echo "the machine registry stayed locked for 2 minutes; going on without the lock"
}

unlock_registry() {
    exec 9<&-
}

# A key is its "[...]" line and every line up to the next one: Wine indents continuation lines, so only key lines
# start with "[". WANT=1 prints the keys of the control set, WANT=0 everything else (the file's header included).
control_set() {
    awk -v want="$1" '
        /^\[/ { in_set = (tolower($0) ~ /^\[system\\\\controlset001(\]|\\\\)/) }
        in_set == want + 0 { print }
    ' "$2"
}

has_keys() {
    [ -s "$1" ] && grep -q '^\[' "$1"
}

save() {
    [ -s "$system_reg" ] || { echo "no machine registry at $system_reg"; return 1; }
    mkdir -p "$state"
    lock_registry -s
    control_set 1 "$system_reg" > "$saved.tmp" || { unlock_registry; rm -f "$saved.tmp"; return 1; }
    unlock_registry
    if ! has_keys "$saved.tmp"; then
        rm -f "$saved.tmp"
        echo "the machine registry has no control set"
        return 1
    fi
    mv -f "$saved.tmp" "$saved"
    echo "saved the control set ($(grep -c '^\[' "$saved") keys) as the last known good configuration"
}

restore() {
    has_keys "$saved" || { echo "there is no last known good configuration in $state"; return 1; }
    [ -s "$system_reg" ] || { echo "no machine registry at $system_reg"; return 1; }
    lock_registry -x
    replace_control_set
    status=$?
    unlock_registry
    return "$status"
}

replace_control_set() {   # restore's work, under the lock
    control_set 1 "$system_reg" > "$failed.tmp" && mv -f "$failed.tmp" "$failed" || return 1
    new=$system_reg.lastgood-tmp
    # The copy keeps the owner, the mode and the ACLs; prefixes link to system.reg by name, so a rename keeps them.
    cp -a "$system_reg" "$new" || return 1
    # The saved keys go where the control set was.
    if ! awk -v saved="$saved" '
            function put_saved(    line) {
                while ((getline line < saved) > 0) print line
                done = 1
            }
            /^\[/ {
                in_set = (tolower($0) ~ /^\[system\\\\controlset001(\]|\\\\)/)
                if (in_set && !done) put_saved()
            }
            !in_set { print }
            END { if (!done) put_saved() }
        ' "$system_reg" > "$new"; then
        rm -f "$new"
        return 1
    fi
    mv -f "$new" "$system_reg"
    echo "went back to the last known good configuration ($(grep -c '^\[' "$saved") keys); the replaced control" \
         "set is in $failed"
}

case "${1:-}" in
    save) save ;;
    restore) restore ;;
    status) has_keys "$saved" ;;
    *) echo "usage: wex-lastgood save|restore|status" >&2; exit 2 ;;
esac
