#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Opt-in headless protocol tests; no installed desktop/runtime is touched.
set -euo pipefail
if [ "${WEX_BACKEND_TEST_BUS:-}" != 1 ]; then
    exec dbus-run-session -- env WEX_BACKEND_TEST_BUS=1 bash "$0" "$@"
fi
root=${WEX_ASSEMBLED_ROOT:-/srv/protonexplorer/wex-wayland-l1}
b=${WEX_CHECK_BUILD:-$root/wow64-source/build/wine64}
source_root=${WEX_CHECK_SOURCE:-$root/wow64-source}
exec 9>"$root/check.lock"
flock -n 9
export WINEPREFIX=${WEX_CHECK_PREFIX:?Set an isolated test prefix}
export WINELOADER="$b/loader/wine" WINESERVER="$b/server/wineserver" WINEDEBUG=-all
export WINEDLLOVERRIDES='mscoree,mshtml='
probe=$(dirname "$(readlink -f "$0")")/wex-wayland-backend-probe.c
make -C "$b" -j8 dlls/wexhost/all dlls/wexhost/x86_64-windows/libwexhost.a dlls/wexhost/i386-windows/libwexhost.a
for bits in 64 32; do
    if [ "$bits" = 64 ]; then compiler=x86_64-w64-mingw32-gcc; arch=x86_64; else compiler=i686-w64-mingw32-gcc; arch=i386; fi
    "$compiler" "$probe" -I"$source_root/src/include" -L"$b/dlls/wexhost/$arch-windows" -lwexhost -luser32 -o "$root/checks/wayland-backend$bits.exe"
done
state=$(mktemp -d "$root/checks/wayland-backend.XXXXXX")
mkdir -m 700 "$state/runtime" "$state/config"
printf '<labwc_config><core><decoration>server</decoration></core></labwc_config>\n' > "$state/config/rc.xml"
export XDG_RUNTIME_DIR="$state/runtime" WAYLAND_DISPLAY=wex-shell-0
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1
export WEX_TEST_COMPOSITOR=${WEX_WEXCOMP_BIN:-$HOME/wexcomp-build/build/labwc}
export WEX_BACKEND_ROOT="$root" WEX_BACKEND_STATE="$state"
timeout 120 xvfb-run -a -s '-screen 0 800x600x24' bash -eu -c '
    state="$WEX_BACKEND_STATE"
    compositor= native=
    cleanup() {
        "$WINESERVER" -k || true
        if [ -n "$native" ]; then kill "$native" 2>/dev/null || true; wait "$native" 2>/dev/null || true; fi
        if [ -n "$compositor" ]; then kill "$compositor" 2>/dev/null || true; wait "$compositor" 2>/dev/null || true; fi
    }
    trap cleanup EXIT
    env -u WAYLAND_DISPLAY "$WINELOADER" reg add "HKCU\Software\Wine\Drivers" /v Graphics /d x11 /f > "$state/graphics.log" 2>&1
    "$WINESERVER" -k
    "$WINESERVER" -w
    for bits in 64 32; do
        "$WEX_TEST_COMPOSITOR" -C "$state/config" > "$state/compositor$bits.log" 2>&1 & compositor=$!
        for ((i=0;i<100;++i)); do
            if [ -S "$XDG_RUNTIME_DIR/wex-shell-0" ]; then break; fi
            kill -0 "$compositor"
            sleep .1
        done
        test -S "$XDG_RUNTIME_DIR/wex-shell-0"
        WAYLAND_DISPLAY=wayland-0 foot --title=WexWaylandBackend sh -c "sleep 90" > "$state/native$bits.log" 2>&1 & native=$!
        sleep .5
        report="$state/backend$bits.log"
        timeout 55 "$WINELOADER" "$WEX_BACKEND_ROOT/checks/wayland-backend$bits.exe" "Z:$report" disconnect > "$state/launch$bits.log" 2>&1 & probe_pid=$!
        peer_started=0
        for ((i=0;i<500;++i)); do
            if [ "$peer_started" = 0 ] && grep -q READY_PEER "$report" 2>/dev/null; then
                wait "$native" || true
                WAYLAND_DISPLAY=wayland-0 foot --title=WexWaylandPeer sh -c "sleep 90" > "$state/peer$bits.log" 2>&1 & native=$!
                peer_started=1
            fi
            if grep -q READY_DISCONNECT "$report" 2>/dev/null; then
                kill "$compositor"
                wait "$compositor" || true
                compositor=
                break
            fi
            if ! kill -0 "$probe_pid" 2>/dev/null; then break; fi
            sleep .1
        done
        wait "$probe_pid"
        grep "WAYLAND_BACKEND_PASSED bits=$bits failures=0" "$report"
        # Closing the terminal sends SIGHUP to its child; foot may return that
        # child status. Successful withdrawal is asserted by the bridge probe.
        wait "$native" || true
        native=
        "$WINESERVER" -k
        "$WINESERVER" -w
    done
' 9>&-
printf 'Wayland backend logs: %s\n' "$state"
