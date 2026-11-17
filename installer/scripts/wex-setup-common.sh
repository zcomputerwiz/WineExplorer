#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Shared helpers for GUI-mode Setup and OOBE. Sourcing this file has no side effects.

wex_setup_prepare_user() {
    if ! id wexsetup >/dev/null 2>&1; then
        useradd -r -m -d /var/lib/wexsetup -s /usr/sbin/nologin -G audio,video,render,users,adm wexsetup
    else
        usermod -a -G adm wexsetup 2>/dev/null || true
    fi

    # Repair ownership left by older GUI runners (chown -R wexsetup of all /etc/wineexplorer), sysrun.conf and
    # accounts.map included on a retry: only what wexsetup owns goes back to root, so files with a group of their
    # own (accounts.map is root:<system group> 0640) keep it. Never follows links; the setup subtree stays wexsetup's.
    find /etc/wineexplorer -path /etc/wineexplorer/setup -prune -o -user wexsetup -exec chown -h root -- {} +
    find /etc/wineexplorer -path /etc/wineexplorer/setup -prune -o -group wexsetup -exec chgrp -h root -- {} +
    mkdir -p /etc/skel/.wine
    chown -R wexsetup:wexsetup /var/lib/wexsetup /etc/wineexplorer/setup /etc/skel/.wine
    chmod 755 /etc/wineexplorer /etc/wineexplorer/setup
    chmod 644 /etc/wineexplorer/setup/* 2>/dev/null || true
}

wex_setup_start_xorg() {
    local ready_fd display_number i
    X_PID=""
    export DISPLAY=:0
    export XAUTHORITY=/var/lib/wexsetup/.Xauthority

    if ! pidof Xorg >/dev/null 2>&1; then
        # Keep :0, vt7 and the original server options. Only the readiness event goes to the pipe;
        # Xorg's normal output still goes to the service log. EOF also reports an early server exit.
        exec {ready_fd}< <(exec Xorg :0 vt7 -nolisten tcp -noreset -keeptty -verbose 3 -displayfd 3 3>&1 1>&2)
        X_PID=$!
        if IFS= read -r -t 15 -u "$ready_fd" display_number && [ "$display_number" = 0 ]; then
            exec {ready_fd}<&-
        else
            exec {ready_fd}<&-
            echo "Setup Xorg did not signal readiness on display :0." >&2
            kill "$X_PID" 2>/dev/null || true
            wait "$X_PID" 2>/dev/null || true
            X_PID=""
            return 1
        fi
    fi

    # An existing server has no displayfd for us; also check that this session can connect.
    for ((i = 0; i < 30; i++)); do
        if xset q >/dev/null 2>&1; then
            chvt 7 2>/dev/null || true
            xhost +SI:localuser:wexsetup 2>/dev/null || true
            return 0
        fi
        sleep 0.5
    done
    echo "Setup cannot connect to Xorg on display :0." >&2
    if [ -n "$X_PID" ]; then
        kill "$X_PID" 2>/dev/null || true
        wait "$X_PID" 2>/dev/null || true
        X_PID=""
    fi
    return 1
}

wex_setup_resolution() {
    local resolution
    resolution=$(DISPLAY=:0 xdpyinfo 2>/dev/null | grep 'dimensions:' | awk '{print $2}' || true)
    [ -n "$resolution" ] || resolution=$(DISPLAY=:0 xdotool getdisplaygeometry 2>/dev/null | awk '{print $1"x"$2}' || true)
    printf '%s\n' "${resolution:-1024x768}"
}

# Publish a setting that setup.exe (as wexsetup) staged in /etc/wineexplorer/setup/NAME as root's
# /etc/wineexplorer/NAME, 0644. wexsetup controls the staging directory, so the file is first renamed into the
# root-only directory (a rename never follows a link) and accepted only as a small regular file with one name: a
# link there could otherwise make root copy any file it can read into a world-readable one. Nothing staged: the
# published file is left as it is.
wex_setup_publish() {
    local etc=${WEX_SETUP_ETC:-/etc/wineexplorer} name=$1
    local staged=$etc/setup/$1 temp=$etc/.$1.publish
    [ -e "$staged" ] || [ -L "$staged" ] || return 0
    rm -f -- "$temp"
    mv -T -- "$staged" "$temp" || return 1
    if [ -L "$temp" ] || [ ! -f "$temp" ] || [ "$(stat -c %h -- "$temp")" != 1 ] ||
       [ "$(stat -c %s -- "$temp")" -gt 4096 ]; then
        rm -f -- "$temp"
        echo "Setup left an invalid $name; not published." >&2
        return 1
    fi
    chown root:root -- "$temp" && chmod 0644 -- "$temp" && mv -T -- "$temp" "$etc/$name"
}

# Arguments: diagnostic label, desktop name, done file, resolution, executable and its arguments.
wex_setup_run() {
    local label=$1 desktop=$2 done_file=$3 resolution=$4
    shift 4
    rm -f -- "$done_file"
    su -s /bin/bash wexsetup -c '
        RES="${1:-1024x768}"
        DESKTOP=$2
        DONE_FILE=$3
        LABEL=$4
        shift 4
        export DISPLAY=:0
        export WINEPREFIX=/etc/skel/.wine
        export WINEDLLOVERRIDES="winemenubuilder.exe=d"
        export WINEDEBUG=-all
        /usr/local/bin/wine explorer "/desktop=$DESKTOP,$RES" "$@" &
        WINE_PID=$!
        while [ ! -f "$DONE_FILE" ]; do
            if ! kill -0 "$WINE_PID" 2>/dev/null; then
                echo "$LABEL exited without completion; preserving setup for retry." >&2
                exit 1
            fi
            sleep 1
        done
        sleep 1
        wineserver -k 2>/dev/null || true
        wait "$WINE_PID" 2>/dev/null || true
    ' bash "$resolution" "$desktop" "$done_file" "$label" "$@"
}
