#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Fresh-prefix startup regression: no unhandled exception in any background process.
#
# The "division by zero at 0x14000ca88" that fresh prefixes logged is NOT in explorer: that address is in
# conhost.exe (update_window, programs/conhost/window.c), which divides by a zero console font cell size
# when no usable console font was found. check-token-security.py reproduces it in its fresh prefixes and now
# fails on any "Unhandled" line in the probe's stderr. This script is the explorer-side smoke test: a console
# program in a new console, then the explorer tray balloon path (add, modify, delete), on an owned display
# with the default WINEDEBUG so that diagnostics are not hidden. It does not reproduce the conhost crash by
# itself, so it passes before and after the conhost fix.
#
# Usage: [WEX_B4_HELPER=1] test_b4_explorer_fresh.sh WINE_BUILD_DIR   (run as an unprivileged user)
set -euo pipefail

[ "$(id -u)" -ne 0 ] || { echo "run as an unprivileged user" >&2; exit 2; }
WINE_BUILD="$(realpath "${1:?usage: $0 WINE_BUILD_DIR}")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d /tmp/wex-b4-XXXXXX)"
XVFB_PID=""

cleanup()
{
    WINEPREFIX="$WORK/pfx" "$WINE_BUILD/server/wineserver" -k 2>/dev/null || true
    [ -z "$XVFB_PID" ] || kill "$XVFB_PID" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

export WINEPREFIX="$WORK/pfx" WINESERVER="$WINE_BUILD/server/wineserver" WINE="$WINE_BUILD/wine"
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
export HOME="$WORK/home" XDG_RUNTIME_DIR="$WORK/runtime"
mkdir -p "$HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
unset WINEDEBUG WAYLAND_DISPLAY DISPLAY

Xvfb -displayfd 3 -screen 0 1024x768x24 3>"$WORK/display" >"$WORK/xvfb.log" 2>&1 &
XVFB_PID=$!
for _ in $(seq 1 50); do [ -s "$WORK/display" ] && break; sleep 0.2; done
[ -s "$WORK/display" ] || fail "owned Xvfb did not start"
export DISPLAY=":$(cat "$WORK/display")"

x86_64-w64-mingw32-g++ -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
    "$SCRIPT_DIR/tray_balloon_probe.cpp" -o "$WORK/tray_probe.exe" -lshell32

echo "Booting fresh prefix at $WORK/pfx on $DISPLAY..."
"$WINE" wineboot.exe -i >"$WORK/boot.out" 2>"$WORK/boot.err" || fail "wineboot failed"
"$WINESERVER" -w

# WEX_B4_HELPER=1 configures an elevation helper first, which turns elevation enforcement on in the server:
# explorer then runs with the user's limited token instead of granting itself an admin token.
if [ "${WEX_B4_HELPER:-0}" = 1 ]; then
    # An enforced prefix cannot be updated afterwards (HKLM is read-only), so update it while it is still open.
    "$WINE" wineboot.exe -u >>"$WORK/boot.out" 2>>"$WORK/boot.err" || fail "wineboot -u failed"
    "$WINESERVER" -w
    "$WINE" reg add 'HKLM\Software\Wine\Elevation' /v Helper /t REG_SZ \
        /d 'C:\windows\system32\wex-test-helper-never-run.exe' /f /reg:64 >"$WORK/reg.out" 2>>"$WORK/reg.err" ||
        fail "could not configure the elevation helper"
    "$WINESERVER" -w
fi

# 1. A console program in its own new console, native and WoW64: this creates a conhost.exe window.
for cmd in 'C:\windows\system32\cmd.exe' 'C:\windows\syswow64\cmd.exe'; do
    timeout 120 "$WINE" "$cmd" /c start /wait "$cmd" /c exit 0 >"$WORK/cmd.out" 2>>"$WORK/cmd.err" ||
        fail "$cmd exited non-zero"
done
"$WINESERVER" -w

# 2. Tray balloon path through the explorer desktop shell.
rm -f "$WORK/probe.result"
timeout 120 "$WINE" explorer.exe /desktop=wexb4,800x600 "$WORK/tray_probe.exe" 'Z:'"$(echo "$WORK/probe.result" | tr / '\\')" \
    >"$WORK/explorer.out" 2>>"$WORK/explorer.err" || fail "explorer desktop exited non-zero"
"$WINESERVER" -w
[ "$(cat "$WORK/probe.result" 2>/dev/null)" = "tray_balloon_probe=DONE" ] ||
    fail "tray balloon probe did not complete: $(cat "$WORK/probe.result" 2>/dev/null || echo 'no result')"

cat "$WORK"/*.err 2>/dev/null | grep -v 'libEGL\|winebth\|winemenubuilder\|created the configuration directory' || true
if grep -qi 'unhandled\|division by zero' "$WORK"/*.err; then
    grep -hi 'unhandled\|division by zero' "$WORK"/*.err >&2
    fail "unhandled exception in a background process"
fi

echo "b4_fresh_prefix_startup_clean=PASS"
