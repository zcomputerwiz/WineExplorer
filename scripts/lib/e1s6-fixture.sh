#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sourced by the disposable sysrun fixture. It owns only these two new files
# and its exact root service PID. It never resets a guest or another prefix.
E1S6_PID=
E1S6_CHILD_ID=
E1S6_POLICY_ID=
E1S6_CHILD=/usr/local/libexec/wineexplorer/e1s6-lab.py
E1S6_POLICY=/etc/dbus-1/system.d/org.wineexplorer.E1S6Lab.conf

e1s6_reload()
{
    dbus-send --system --type=method_call --print-reply --dest=org.freedesktop.DBus \
        /org/freedesktop/DBus org.freedesktop.DBus.ReloadConfig >/dev/null
}

e1s6_stop()
{
    if [ -n "$E1S6_PID" ]; then
        kill "$E1S6_PID" 2>/dev/null || true
        wait "$E1S6_PID" 2>/dev/null || true
        E1S6_PID=
    fi
    if [ -n "$E1S6_CHILD_ID" ]; then
        if [ "$(stat -c '%d:%i' "$E1S6_CHILD" 2>/dev/null)" = "$E1S6_CHILD_ID" ]; then
            rm -- "$E1S6_CHILD"
        else echo 'FAIL: S6 child replaced; refusing foreign cleanup' >&2; fi
        E1S6_CHILD_ID=
    fi
    if [ -n "$E1S6_POLICY_ID" ]; then
        if [ "$(stat -c '%d:%i' "$E1S6_POLICY" 2>/dev/null)" = "$E1S6_POLICY_ID" ]; then
            rm -- "$E1S6_POLICY"
            e1s6_reload || true
        else echo 'FAIL: S6 policy replaced; refusing foreign cleanup' >&2; fi
        E1S6_POLICY_ID=
    fi
}

e1s6_start()  # real wineserver path, system group, owned output directory
{
    local server="$1" group="$2" out="$3" i
    [ "$(id -u)" = 0 ] || fail 'S6 fixture requires root in disposable guest'
    # a restart in the same run keeps the lock it already holds (children inherited the fd)
    if [ -z "${E1S6_LOCK:-}" ]; then
        exec {E1S6_LOCK}>/run/lock/wex-e1s6.lock
        flock -n "$E1S6_LOCK" || fail 'S6 fixture is already owned'
    fi
    [ ! -e "$E1S6_CHILD" ] && [ ! -L "$E1S6_CHILD" ] || fail 'foreign S6 child exists'
    [ ! -e "$E1S6_POLICY" ] && [ ! -L "$E1S6_POLICY" ] || fail 'foreign S6 policy exists'
    /usr/bin/python3 -I -c 'import dbus; from gi.repository import GLib' || fail 'python3-dbus/python3-gi missing'
    install -o root -g root -m 0644 "$SRC/tools/e1s6/e1s6-lab.py" "$E1S6_CHILD"
    E1S6_CHILD_ID="$(stat -c '%d:%i' "$E1S6_CHILD")"
    install -o root -g root -m 0644 "$SRC/tools/e1s6/org.wineexplorer.E1S6Lab.conf" "$E1S6_POLICY"
    E1S6_POLICY_ID="$(stat -c '%d:%i' "$E1S6_POLICY")"
    e1s6_reload || fail 'system bus policy reload failed'
    /usr/bin/python3 -I "$E1S6_CHILD" --lab-service "$(realpath "$server")" --group "$group" \
        >"$out/e1s6-service.log" 2>&1 &
    E1S6_PID=$!
    for i in {1..100}; do
        kill -0 "$E1S6_PID" 2>/dev/null || { cat "$out/e1s6-service.log" >&2; fail 'S6 service exited'; }
        grep -qx 'E1S6 READY' "$out/e1s6-service.log" && return 0
        sleep .05
    done
    fail 'S6 service readiness timeout'
}
