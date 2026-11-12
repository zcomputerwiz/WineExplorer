#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Keeps a Safe Mode start from changing which session, X11 or Wayland, an account logs on to (docs/SAFE_MODE.md).
# LightDM saves the session of every logon through the greeter as the account's choice, in AccountsService's
# XSession and in ~/.dmrc (LightDM 1.32, seat.c, greeter_start_session_cb). A Safe Mode logon, through the other
# session or the GTK greeter's default, would otherwise change that choice for good.
#   --snapshot  as root, before the display manager starts (wex-session-choice.service); in Safe Mode only: each
#               account's saved session, or LightDM's default when it has none, and whether it has a ~/.dmrc, into
#               /run/wineexplorer/session-choice/<account>
#   --restore   as the account, at the start of a Safe Mode session (the session scripts): when LightDM has saved
#               another session, the one from the snapshot goes back, through AccountsService (D-Bus) and ~/.dmrc; a
#               ~/.dmrc that LightDM created at this logon is removed
#   --fallback SESSION
#               as the account, from the Wayland session when its compositor cannot start (docs/WAYLAND_DEFAULT.md):
#               SESSION, the X11 session, becomes the account's saved session, so its next logon starts that one.
#               Not in Safe Mode, whose logons never change the choice.
# The WEX_SESSION_CHOICE_* and WEX_BOOTMODE_HELPER variables let scripts/test_safemode.py point it elsewhere.
users_dir=${WEX_SESSION_CHOICE_USERS:-/var/lib/AccountsService/users}
run_dir=${WEX_SESSION_CHOICE_RUN:-/run/wineexplorer/session-choice}
lightdm_conf=${WEX_SESSION_CHOICE_LIGHTDM:-/etc/lightdm}
bootmode=${WEX_BOOTMODE_HELPER:-/usr/local/bin/wex-bootmode}
root_uid=${WEX_SESSION_CHOICE_ROOT_UID:-0}
accounts=org.freedesktop.Accounts

valid() {   # a session or account name: nothing a sed expression or a path could misread
    case "$1" in ''|*[!A-Za-z0-9._-]*) return 1 ;; esac
}

safe_mode() {
    case "$("$bootmode" 2>/dev/null)" in safe|safenet|safecmd) return 0 ;; esac
    return 1
}

default_session() {   # LightDM's user-session, the last one set in its configuration
    cat "$lightdm_conf/lightdm.conf" "$lightdm_conf"/lightdm.conf.d/*.conf 2>/dev/null | sed -n 's/^user-session=//p' \
        | tail -n 1 | tr -d ' \r'
}

case "${1:-}" in
--snapshot)
    safe_mode || exit 0
    mkdir -p "$run_dir" && chmod 755 "$run_dir" || exit 1
    fallback=$(default_session)
    for f in "$users_dir"/*; do
        [ -f "$f" ] || continue
        name=${f##*/}
        valid "$name" || continue
        session=$(sed -n 's/^XSession=//p' "$f" | head -n 1 | tr -d ' \r')
        valid "$session" || session=$fallback
        valid "$session" || continue
        if [ -n "${WEX_SESSION_CHOICE_HOMES:-}" ]; then home=$WEX_SESSION_CHOICE_HOMES/$name
        else home=$(getent passwd "$name" | cut -d: -f6); fi
        if [ -n "$home" ] && [ -e "$home/.dmrc" ]; then dmrc=present; else dmrc=absent; fi
        printf '%s\ndmrc=%s\n' "$session" "$dmrc" > "$run_dir/$name.tmp" && chmod 644 "$run_dir/$name.tmp" \
            && mv -f "$run_dir/$name.tmp" "$run_dir/$name"
    done
    ;;
--restore)
    safe_mode || exit 0
    user=${USER:-$(id -un)}
    valid "$user" || exit 0
    record=$run_dir/$user
    # only root's record: the account itself must not choose what is written back
    [ -f "$record" ] && [ ! -L "$record" ] && [ "$(stat -c %u "$record" 2>/dev/null)" = "$root_uid" ] || exit 0
    saved=$(sed -n 1p "$record")
    had_dmrc=$(sed -n 's/^dmrc=//p' "$record")
    valid "$saved" || exit 0
    path=$(busctl --system call $accounts /org/freedesktop/Accounts $accounts FindUserByName s "$user" 2>/dev/null \
        | sed -n 's/^o "\(.*\)"$/\1/p')
    if [ -n "$path" ]; then
        current=$(busctl --system get-property $accounts "$path" $accounts.User XSession 2>/dev/null \
            | sed -n 's/^s "\(.*\)"$/\1/p')
        if [ "$current" != "$saved" ]; then
            busctl --system call $accounts "$path" $accounts.User SetXSession s "$saved" \
                && echo "Safe Mode: the saved session is $saved again (this logon saved ${current:-none})"
        fi
    fi
    dmrc=${HOME:-/nonexistent}/.dmrc
    if [ "$had_dmrc" = absent ] && [ -f "$dmrc" ] && [ ! -L "$dmrc" ]; then
        rm -f "$dmrc"   # LightDM made it at this logon
    elif [ -f "$dmrc" ] && [ ! -L "$dmrc" ] && grep -q '^Session=' "$dmrc" && ! grep -qx "Session=$saved" "$dmrc"; then
        sed "s/^Session=.*/Session=$saved/" "$dmrc" > "$dmrc.tmp" && mv -f "$dmrc.tmp" "$dmrc"
    fi
    ;;
--fallback)
    session=${2:-}
    valid "$session" || { echo "usage: wex-session-choice --fallback SESSION" >&2; exit 2; }
    safe_mode && exit 0
    user=${USER:-$(id -un)}
    valid "$user" || exit 0
    path=$(busctl --system call $accounts /org/freedesktop/Accounts $accounts FindUserByName s "$user" 2>/dev/null \
        | sed -n 's/^o "\(.*\)"$/\1/p')
    if [ -n "$path" ] && busctl --system call $accounts "$path" $accounts.User SetXSession s "$session"; then
        echo "the next logon of $user starts $session"
    fi
    dmrc=${HOME:-/nonexistent}/.dmrc
    if [ -f "$dmrc" ] && [ ! -L "$dmrc" ] && grep -q '^Session=' "$dmrc" && ! grep -qx "Session=$session" "$dmrc"; then
        sed "s/^Session=.*/Session=$session/" "$dmrc" > "$dmrc.tmp" && mv -f "$dmrc.tmp" "$dmrc"
    fi
    ;;
*)
    echo "usage: wex-session-choice --snapshot|--restore|--fallback SESSION" >&2
    exit 2
    ;;
esac
exit 0
