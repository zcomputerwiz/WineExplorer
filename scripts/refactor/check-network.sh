#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
set -euo pipefail
root=/srv/protonexplorer/wex-refactor-p3b
test "$(cat "$root/build.status")" = BUILD_PASSED
mkdir -p "$root/checks/network/include"
exec 9>"$root/checks/network/check.lock"
flock -n 9
echo NETWORK_CHECK_RUNNING > "$root/checks/network/status"
trap 'echo NETWORK_CHECK_FAILED > "$root/checks/network/status"' ERR
cp "$root/src/include/wexnet.h" "$root/checks/network/include/"
x86_64-w64-mingw32-gcc -Wall -I "$root/checks/network/include" "$root/checks/network-module-probe.c" \
    -mwindows -lole32 -luuid -o "$root/checks/network/probe.exe"
export WINEPREFIX="$root/prefixes/network" WINESERVER="$root/build/server/wineserver"
export WINELOADER="$root/build/loader/wine" WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml='
export WEX_NETWORK_ROOT="$root"
timeout 240 xvfb-run -a -s '-screen 0 800x600x24' bash -eu -o pipefail -c '
    root="$WEX_NETWORK_ROOT"
    trap "\"$WINESERVER\" -k || true" EXIT
    "$WINELOADER" wineboot -u > "$root/checks/network/boot.log" 2>&1
    "$WINESERVER" -k
    "$WINESERVER" -w
    timeout 40 "$WINELOADER" "$root/checks/network/probe.exe" com
    cp /tmp/network-module-probe.txt "$root/checks/network/com.txt"
    grep -q "^COMPLETE: 0 failures" "$root/checks/network/com.txt"
    "$WINESERVER" -k
    "$WINESERVER" -w
    timeout 40 "$WINELOADER" "$root/checks/network/probe.exe" cpl
    cp /tmp/network-module-probe.txt "$root/checks/network/cpl.txt"
    grep -q "^COMPLETE: 0 failures" "$root/checks/network/cpl.txt"
    "$WINESERVER" -k
    "$WINESERVER" -w
    key="HKLM\Software\Microsoft\Windows\CurrentVersion\ShellServiceObjectDelayLoad"
    for state in present absent disabled restored; do
        expected=present
        export WINEDLLOVERRIDES="mscoree,mshtml="
        if [ "$state" = absent ]; then
            export WINEDLLOVERRIDES="netshell=;mscoree,mshtml="
            expected=absent
        elif [ "$state" = disabled ]; then
            "$WINELOADER" reg delete "$key" /v "Connections Tray" /f >/dev/null
            expected=absent
            "$WINESERVER" -k
            "$WINESERVER" -w
        elif [ "$state" = restored ]; then
            "$WINELOADER" reg add "$key" /v "Connections Tray" /t REG_SZ /d "{7007ACCF-3202-11D1-AAD2-00805FC1270E}" /f >/dev/null
            "$WINESERVER" -k
            "$WINESERVER" -w
        fi
        rm -f /tmp/network-module-probe.txt
        "$WINELOADER" explorer /desktop=NetworkCheck,800x600 /shell "$root/checks/network/probe.exe" "$expected" \
            > "$root/checks/network/$state.log" 2>&1 &
        for ((attempt=0; attempt<300; ++attempt)); do
            if grep -q "^COMPLETE:" /tmp/network-module-probe.txt 2>/dev/null; then break; fi
            sleep 0.1
        done
        cp /tmp/network-module-probe.txt "$root/checks/network/$state.txt"
        grep -q "^COMPLETE: 0 failures" "$root/checks/network/$state.txt"
        "$WINESERVER" -k
        "$WINESERVER" -w
    done
'
echo NETWORK_CHECK_PASSED > "$root/checks/network/status"
