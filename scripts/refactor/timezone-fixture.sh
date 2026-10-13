#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Change timezone files only inside this process tree's mount namespace.
set -euo pipefail
if [ "${1:-}" != --inside ]; then
    exec sudo -n --preserve-env=WINEPREFIX,WINESERVER,WINELOADER,WINEDEBUG,WINEDLLOVERRIDES,WINETEST_INTERACTIVE,WINETEST_DEBUG \
        unshare --mount -- bash "$0" --inside "$@"
fi
shift
fixture="$(mktemp -d /tmp/wex-timezone.XXXXXXXX)"
target="$(readlink -f /etc/localtime)"
case "$target" in /etc/localtime|/usr/share/zoneinfo/*) ;; *) exit 2 ;; esac
cleanup()
{
    if [ -n "${WINESERVER:-}" ]; then
        runuser -u "${SUDO_USER:?}" -- "$WINESERVER" -k || true
        runuser -u "$SUDO_USER" -- "$WINESERVER" -w
    fi
    while mountpoint -q "$target"; do umount "$target" || break; done
    rm -rf -- "$fixture"
}
trap cleanup EXIT
chown "$SUDO_UID:$SUDO_GID" "$fixture"
printf '%s\n' \
    'Rule Wex 2000 max - Mar Sun>=8 2:00 1:00 D' \
    'Rule Wex 2000 max - Nov Sun>=1 2:00 0 S' \
    'Zone WexDST -5:00 Wex Wex%sT' \
    'Zone WexFixed -5:00 - WexST' > "$fixture/zones"
/usr/sbin/zic -d "$fixture" "$fixture/zones"
mount --make-rprivate /
mount --no-canonicalize --bind "$fixture/WexDST" "$target"
unset TZ
export WINETEST_TIMEZONE_FIXTURE="Z:$fixture"
runuser -u "${SUDO_USER:?}" -- "$@" &
command_pid=$!
for stage in 1 2; do
    for ((attempt=0; attempt<1500; ++attempt)); do
        if [ -f "$fixture/request$stage" ]; then break; fi
        if ! kill -0 "$command_pid" 2>/dev/null; then wait "$command_pid"; exit; fi
        sleep 0.02
    done
    test -f "$fixture/request$stage"
    zone=WexFixed
    if [ "$stage" = 2 ]; then zone=WexDST; fi
    mount --no-canonicalize --bind "$fixture/$zone" "$target"
    touch "$fixture/ready$stage"
done
wait "$command_pid"
