#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Run after build-assembled.sh; no installed runtime or user prefix is touched.
set -euo pipefail
root=${WEX_ASSEMBLED_ROOT:-/srv/protonexplorer/wex-refactor-p2}
case "$root" in
    /srv/protonexplorer/wex-refactor-p2|/srv/protonexplorer/wex-refactor-p3) ;;
    *) echo 'Expected an isolated refactor directory.' >&2; exit 2 ;;
esac
mkdir -p "$root/checks" "$root/prefixes"
exec 9>"$root/check.lock"
flock -n 9
trap 'printf "%s\n" CHECK_FAILED > "$root/checks/status"' ERR
if [ "${1:-}" = --wait-for-build ]; then
    printf '%s\n' CHECK_WAITING_FOR_BUILD > "$root/checks/status"
    for ((attempt=0; attempt<720; ++attempt)); do
        state=$(cat "$root/build.status")
        if [ "$state" = BUILD_PASSED ]; then break; fi
        test "$state" != BUILD_FAILED
        sleep 5
    done
fi
test "$(cat "$root/build.status")" = BUILD_PASSED
printf '%s\n' CHECK_RUNNING > "$root/checks/status"
x86_64-w64-mingw32-gcc -Wall -Wextra "$root/checks/tray-layout-probe.c" -mwindows -o "$root/checks/tray-layout-probe.exe"
cc -Wall -Wextra "$root/checks/linux-dialog-probe.c" -lX11 -o "$root/checks/linux-dialog-probe"
export WINEPREFIX="${WEX_CHECK_PREFIX:-$root/prefixes/smoke}"
export WINESERVER="${WEX_CHECK_SERVER:-$root/build/server/wineserver}"
export WINELOADER="${WEX_CHECK_WINE:-$root/build/loader/wine}"
export WINEDEBUG=-all
export WINEDLLOVERRIDES='mscoree,mshtml='
export WINETEST_INTERACTIVE=0 WINETEST_DEBUG=1
export WEX_P2_ROOT="$root"
timeout 180 xvfb-run -a -s '-screen 0 800x600x24' bash -eu -o pipefail -c '
    root="$WEX_P2_ROOT"
    cleanup() { "$WINESERVER" -k || true; }
    trap cleanup EXIT
    timeout 90 "$WINELOADER" wineboot -u > "$root/checks/wineboot.log" 2>&1
    "$WINESERVER" -k
    "$WINESERVER" -w
    rm -f /tmp/tray-layout-probe.txt
    "$WINELOADER" explorer /desktop=RefactorP2,800x600 /shell "$root/checks/tray-layout-probe.exe" > "$root/checks/shell.log" 2>&1 &
    # The shell launches the probe after startup, on its own desktop.
    for ((attempt=0; attempt<100; ++attempt)); do
        if grep -qE "^(PASS|FAIL):" /tmp/tray-layout-probe.txt 2>/dev/null; then break; fi
        sleep 0.1
    done
    cp /tmp/tray-layout-probe.txt "$root/checks/tray-layout.txt"
    grep -q "^PASS:" "$root/checks/tray-layout.txt"
    import -window root "$root/checks/desktop.png"
    "$root/checks/linux-dialog-probe" > "$root/checks/linux-dialog.log" 2>&1
' 9>&-
printf '%s\n' CHECK_PASSED > "$root/checks/status"
