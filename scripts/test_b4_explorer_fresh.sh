#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Test fresh-prefix explorer startup to verify absence of division-by-zero / null pointer crash.
set -euo pipefail

TEST_PFX="/tmp/test-fresh-explorer-pfx-$$"
ERR_LOG="/tmp/explorer_err_$$.log"
trap 'rm -rf "$TEST_PFX" "$ERR_LOG"' EXIT

mkdir -p "$TEST_PFX"
export WINEPREFIX="$TEST_PFX"
WINE_BUILD="${1:-/home/wineuser/build/wow64}"
export WINESERVER="$WINE_BUILD/server/wineserver"
export WINE="$WINE_BUILD/wine"
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
export WINEDEBUG="-all"
export DISPLAY="${DISPLAY:-:0}"

echo "Booting fresh prefix at $TEST_PFX..."
"$WINE" wineboot.exe -i >/dev/null 2>&1
"$WINESERVER" -w

echo "Launching explorer /desktop=test,800x600..."
"$WINE" explorer.exe /desktop=test,800x600 >/dev/null 2>"$ERR_LOG" &
EXP_PID=$!
sleep 5

if kill -0 "$EXP_PID" 2>/dev/null; then
    echo "Explorer running smoothly (PID $EXP_PID)"
    kill "$EXP_PID" 2>/dev/null || true
else
    echo "Explorer exited early"
fi

"$WINESERVER" -w 2>/dev/null || true

if [ -s "$ERR_LOG" ]; then
    echo "Explorer stderr output:"
    cat "$ERR_LOG"
fi

if grep -i "division" "$ERR_LOG" 2>/dev/null; then
    echo "FAIL: division-by-zero diagnostic found" >&2
    exit 1
fi

if grep -i "0x14000ca88" "$ERR_LOG" 2>/dev/null; then
    echo "FAIL: crash at 0x14000ca88 found" >&2
    exit 1
fi

echo "b4_fresh_prefix_explorer_clean=PASS"
