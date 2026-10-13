#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
set -euo pipefail
root=/srv/protonexplorer/wex-refactor-p3b
test "$(cat "$root/build.status")" = BUILD_PASSED
mkdir -p "$root/checks/services"
exec 9>"$root/checks/services/check.lock"
flock -n 9
echo SERVICES_CHECK_RUNNING > "$root/checks/services/status"
trap 'echo SERVICES_CHECK_FAILED > "$root/checks/services/status"' ERR
x86_64-w64-mingw32-gcc -Wall "$root/checks/tray-service-probe.c" -mwindows \
    -lole32 -luuid -lcomctl32 -o "$root/checks/services/probe.exe"
export WINEPREFIX="$root/prefixes/services" WINESERVER="$root/build/server/wineserver"
export WINELOADER="$root/build/loader/wine" WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml='
export WEX_SERVICES_ROOT="$root"
timeout 240 xvfb-run -a -s '-screen 0 800x600x24' bash -eu -o pipefail -c '
    root="$WEX_SERVICES_ROOT"
    trap "\"$WINESERVER\" -k || true" EXIT
    "$WINELOADER" wineboot -u > "$root/checks/services/boot.log" 2>&1
    "$WINESERVER" -k
    "$WINESERVER" -w
    timeout 30 "$WINELOADER" "$root/checks/services/probe.exe" com
    cp /tmp/tray-service-probe.txt "$root/checks/services/com.txt"
    grep -q "^COMPLETE: 0 failures" "$root/checks/services/com.txt"
    "$WINESERVER" -k
    "$WINESERVER" -w
    key="HKLM\Software\Microsoft\Windows\CurrentVersion\ShellServiceObjectDelayLoad"
    for state in present absent disabled restored; do
        expected=present
        export WINEDLLOVERRIDES="mscoree,mshtml="
        if [ "$state" = absent ]; then
            export WINEDLLOVERRIDES="stobject=;mscoree,mshtml="
            expected=absent
        elif [ "$state" = disabled ]; then
            "$WINELOADER" reg delete "$key" /v SysTray /f >/dev/null
            expected=absent
            "$WINESERVER" -k
            "$WINESERVER" -w
        elif [ "$state" = restored ]; then
            "$WINELOADER" reg add "$key" /v SysTray /t REG_SZ /d "{35CEC8A3-2BE6-11D2-8773-92E220524153}" /f >/dev/null
            "$WINESERVER" -k
            "$WINESERVER" -w
        fi
        rm -f /tmp/tray-service-probe.txt
        "$WINELOADER" explorer /desktop=ServiceCheck,800x600 /shell "$root/checks/services/probe.exe" "$expected" \
            > "$root/checks/services/$state.log" 2>&1 &
        for ((attempt=0; attempt<300; ++attempt)); do
            if grep -q "^COMPLETE:" /tmp/tray-service-probe.txt 2>/dev/null; then break; fi
            sleep 0.1
        done
        cp /tmp/tray-service-probe.txt "$root/checks/services/$state.txt"
        grep -q "^COMPLETE: 0 failures" "$root/checks/services/$state.txt"
        import -window root "$root/checks/services/$state.png"
        "$WINESERVER" -k
        "$WINESERVER" -w
    done
'
echo SERVICES_CHECK_PASSED > "$root/checks/services/status"
