#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Unprivileged unit tests: no Wine, Xorg, users or system configuration are changed.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
for script in installer/scripts/wex-setup-common.sh installer/scripts/wex-gui-setup.sh \
              installer/scripts/wex-oobe.sh installer/package-wine.sh installer/tests/test-setup-common.sh; do
    bash -n "$ROOT/$script"
done
source "$ROOT/installer/scripts/wex-setup-common.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }
scratch=$(mktemp -d)
X_PID=""
cleanup() {
    if [ -n "$X_PID" ]; then
        kill "$X_PID" 2>/dev/null || true
        wait "$X_PID" 2>/dev/null || true
    fi
    rm -rf -- "$scratch"
}
trap cleanup EXIT

# Resolution selection, including both tool failures.
xdpyinfo() { printf '%s\n' '  dimensions:    1920x1080 pixels (508x285 millimeters)'; }
xdotool() { fail "xdotool must be a fallback"; }
[ "$(wex_setup_resolution)" = 1920x1080 ] || fail "xdpyinfo resolution"
xdpyinfo() { return 1; }
xdotool() { printf '%s\n' '1280 720'; }
[ "$(wex_setup_resolution)" = 1280x720 ] || fail "xdotool resolution"
xdotool() { return 1; }
[ "$(wex_setup_resolution)" = 1024x768 ] || fail "default resolution"
unset -f xdpyinfo xdotool

# Exercise the real find/prune traversal against a private tree. chown only logs its arguments.
mkdir -p "$scratch/bin" "$scratch/etc/setup" "$scratch/etc/component-licenses"
touch "$scratch/etc/setup/stage" "$scratch/etc/component-licenses/vendor.txt"
for name in prefetch.conf owner workgroup sysrun.conf accounts.map .hidden; do
    touch "$scratch/etc/$name"
done
printf '%s\n' '#!/bin/bash' 'printf "%s\n" "$*" >> "$CHOWN_LOG"' > "$scratch/bin/chown"
printf '%s\n' '#!/bin/bash' 'printf "chgrp %s\n" "$*" >> "$CHOWN_LOG"' > "$scratch/bin/chgrp"
chmod +x "$scratch/bin/chown" "$scratch/bin/chgrp"
export CHOWN_LOG="$scratch/chown.log"
# the files stand for ones an older runner gave to wexsetup: -user/-group wexsetup match this test's own user
self_user=$(id -un) self_group=$(id -gn)
old_path=$PATH
export PATH="$scratch/bin:$PATH"
id() { return "$user_exists"; }
useradd() { printf '%s\n' "useradd $*" >> "$scratch/users.log"; }
usermod() { printf '%s\n' "usermod $*" >> "$scratch/users.log"; }
mkdir() { [ "$*" = '-p /etc/skel/.wine' ] || fail "unexpected mkdir: $*"; }
chmod() { :; }
find() {
    local arg
    local -a args=()
    for arg in "$@"; do
        case "$arg" in
            /etc/wineexplorer*) arg="$scratch/etc${arg#/etc/wineexplorer}" ;;
        esac
        if [ "$arg" = wexsetup ]; then
            case "${args[${#args[@]}-1]}" in -user) arg=$self_user ;; -group) arg=$self_group ;; esac
        fi
        args+=("$arg")
    done
    command find "${args[@]}"
}
for user_exists in 1 0; do
    : > "$CHOWN_LOG"
    wex_setup_prepare_user
    [ "$(grep wexsetup:wexsetup "$CHOWN_LOG")" = '-R wexsetup:wexsetup /var/lib/wexsetup /etc/wineexplorer/setup /etc/skel/.wine' ] ||
        fail "setup ownership scope"
    # The repair must cover existing configuration and nested licenses, without taking setup back.
    # Owner and group are repaired separately (only what wexsetup has), so a file's own group stays.
    root_calls=$(grep -- '^-h root --' "$CHOWN_LOG")
    group_calls=$(grep -- '^chgrp -h root --' "$CHOWN_LOG")
    for name in prefetch.conf owner workgroup sysrun.conf accounts.map .hidden component-licenses/vendor.txt; do
        [[ "$root_calls" == *"$scratch/etc/$name"* ]] || fail "root ownership repair missed $name"
        [[ "$group_calls" == *"$scratch/etc/$name"* ]] || fail "group repair missed $name"
    done
    [[ "$root_calls$group_calls" != *"$scratch/etc/setup"* ]] || fail "root repair traversed setup"
    ! grep -q 'root:root' "$CHOWN_LOG" || fail "owner and group were set together"
done
grep -Fxq 'useradd -r -m -d /var/lib/wexsetup -s /usr/sbin/nologin -G audio,video,render,users,adm wexsetup' "$scratch/users.log" ||
    fail "new setup account"
grep -Fxq 'usermod -a -G adm wexsetup' "$scratch/users.log" || fail "existing setup account"
unset -f id useradd usermod mkdir chmod find

# Run the actual su command body in a subshell with fake Wine, preserving positional arguments.
# Like util-linux su, which takes options anywhere before "--": everything after the user must come after "--", or
# an argument such as setup.exe's -newsetup is read as su's own options.
su() {
    [ "$1 $2 $3" = '-s /bin/bash -c' ] || fail "su options"
    local body=$4
    [ "$5 $6" = '-- wexsetup' ] || fail "su: the user and the program's arguments must follow --"
    shift 7
    (eval "$body")
}
function /usr/local/bin/wine {
    printf '%s\n' "$DISPLAY" "$WINEPREFIX" "$WINEDLLOVERRIDES" "$WINEDEBUG" "$@" > "$scratch/wine.log"
    [ "$wine_result" = done ] && touch "$DONE_FILE"
    return 0
}
wineserver() { printf '%s\n' "$*" >> "$scratch/wineserver.log"; }
# Reap the fake child at a polling point, avoiding real sleeps and scheduling races in the test.
sleep() { wait "$WINE_PID" 2>/dev/null || true; }
for desktop in Setup OOBE; do
    if [ "$desktop" = Setup ]; then
        label='GUI-mode Setup'; app=setup.exe; args=(-newsetup)
    else
        label=OOBE; app=oobe.exe; args=()
    fi
    wine_result=done
    wex_setup_run "$label" "$desktop" "$scratch/done" 1600x900 "$app" "${args[@]}" 'argument with spaces;literal'
    [ -f "$scratch/done" ] || fail "$desktop completion"
    expected=$(printf '%s\n' :0 /etc/skel/.wine winemenubuilder.exe=d -all explorer "/desktop=$desktop,1600x900" \
        "$app" "${args[@]}" 'argument with spaces;literal')
    [ "$(cat "$scratch/wine.log")" = "$expected" ] || fail "$desktop command/environment"

    # The previous done file must be removed: an early exit must fail even on a retry.
    wine_result=exit
    if wex_setup_run "$label" "$desktop" "$scratch/done" 1600x900 "$app" "${args[@]}" 2> "$scratch/error"; then
        fail "$desktop accepted an exit without completion"
    fi
    [ ! -f "$scratch/done" ] || fail "$desktop kept a stale done file"
    grep -Fxq "$label exited without completion; preserving setup for retry." "$scratch/error" || fail "$desktop diagnostic"
done
[ "$(cat "$scratch/wineserver.log")" = "$(printf '%s\n' -k -k)" ] || fail "wineserver completion cleanup"
unset -f su /usr/local/bin/wine wineserver sleep

# Reusing a server must check readiness too; an unavailable server must not be killed.
pidof() { return 0; }
xset() { return "$xset_result"; }
chvt() { :; }
xhost() { :; }
sleep() { :; }
xset_result=0
wex_setup_start_xorg
[ -z "$X_PID" ] && [ "$DISPLAY" = :0 ] || fail "reused Xorg ownership/display"
[ "$XAUTHORITY" = /var/lib/wexsetup/.Xauthority ] || fail "Xauthority"
xset_result=1
if wex_setup_start_xorg 2> "$scratch/error"; then fail "unready existing Xorg accepted"; fi
[ -z "$X_PID" ] || fail "claimed an existing Xorg"
unset -f sleep

# Fake Xorg sends a displayfd event (or EOF); only an owned sleep process is launched and killed.
printf '%s\n' '#!/bin/bash' \
    '[ "$*" = ":0 vt7 -nolisten tcp -noreset -keeptty -verbose 3 -displayfd 3" ] || exit 2' \
    '[ "$XORG_EVENT" != eof ] || exit 1' \
    'printf "%s\n" "$XORG_EVENT" >&3' 'exec sleep 60' > "$scratch/bin/Xorg"
chmod +x "$scratch/bin/Xorg"
pidof() { return 1; }
xset_result=0
export XORG_EVENT=0
wex_setup_start_xorg
[ -n "$X_PID" ] && kill -0 "$X_PID" || fail "owned Xorg PID"
kill "$X_PID"
wait "$X_PID" 2>/dev/null || true
X_PID=""
for XORG_EVENT in 1 eof; do
    if wex_setup_start_xorg 2> "$scratch/error"; then fail "bad Xorg readiness accepted: $XORG_EVENT"; fi
    [ -z "$X_PID" ] || fail "failed Xorg not cleaned up"
done
export PATH=$old_path

# Publishing owner/workgroup staged by setup.exe (root only: it changes ownership). A link or a file with a second
# name in the staging directory is refused and never read; a regular file becomes root's 0644.
if [ "$(id -u)" = 0 ]; then
    export WEX_SETUP_ETC="$scratch/pub"
    command mkdir -p "$WEX_SETUP_ETC/setup"
    printf 'secret\n' > "$scratch/secret"; command chmod 600 "$scratch/secret"
    printf 'RegisteredOwner=Owner\n' > "$WEX_SETUP_ETC/setup/owner"; command chown nobody "$WEX_SETUP_ETC/setup/owner"
    wex_setup_publish owner || fail "regular staged file refused"
    [ "$(stat -c '%U:%G:%a' "$WEX_SETUP_ETC/owner")" = root:root:644 ] && [ ! -e "$WEX_SETUP_ETC/setup/owner" ] ||
        fail "published file is $(stat -c '%U:%G:%a' "$WEX_SETUP_ETC/owner")"
    ln -s "$scratch/secret" "$WEX_SETUP_ETC/setup/workgroup"
    if wex_setup_publish workgroup 2>/dev/null; then fail "a staged link was published"; fi
    [ ! -e "$WEX_SETUP_ETC/workgroup" ] || fail "a staged link reached the published file"
    ln "$scratch/secret" "$WEX_SETUP_ETC/setup/workgroup"
    if wex_setup_publish workgroup 2>/dev/null; then fail "a staged hard link was published"; fi
    [ ! -e "$WEX_SETUP_ETC/workgroup" ] && [ "$(stat -c %a "$scratch/secret")" = 600 ] || fail "a hard link's target changed"
    wex_setup_publish workgroup || fail "nothing staged must leave the published file alone"
    unset WEX_SETUP_ETC
    echo 'PASS: staged settings published only as regular files'
else
    echo 'NOTE: publishing cases skipped (not root)'
fi
echo 'PASS: setup runner syntax and shared helpers (all privileged commands faked)'
