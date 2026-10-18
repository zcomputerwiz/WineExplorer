#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Safe, isolated test runner for token security, elevation, and compatibility (R7)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Find wine binaries
BUILD_DIR="${WINE_BUILD_DIR:-/home/wineuser/build/wow64}"
if [ ! -d "$BUILD_DIR" ]; then
    BUILD_DIR="/usr/local"
fi

WINE_BIN="$BUILD_DIR/wine"
[ -f "$WINE_BIN" ] || WINE_BIN="$BUILD_DIR/bin/wine"
[ -f "$WINE_BIN" ] || WINE_BIN="/usr/local/bin/wine"

WINESERVER_BIN="$BUILD_DIR/server/wineserver"
[ -f "$WINESERVER_BIN" ] || WINESERVER_BIN="$BUILD_DIR/bin/wineserver"
[ -f "$WINESERVER_BIN" ] || WINESERVER_BIN="/usr/local/bin/wineserver"

cleanup() {
    local exit_code=$?
    # Terminate direct children
    kill $(jobs -p) 2>/dev/null || true
    pkill -P $$ 2>/dev/null || true

    # Cleanly stop test wineserver if prefix was allocated
    if [ -n "${TEST_PREFIX:-}" ] && [ -x "${WINESERVER_BIN:-}" ]; then
        WINEPREFIX="$TEST_PREFIX" "$WINESERVER_BIN" -k 2>/dev/null || true
        WINEPREFIX="$TEST_PREFIX" "$WINESERVER_BIN" -w 2>/dev/null || true
    fi

    # Kill Xvfb if started
    if [ -n "${XVFB_PID:-}" ] && kill -0 "$XVFB_PID" 2>/dev/null; then
        kill "$XVFB_PID" 2>/dev/null || true
    fi
    if [ -n "${DISP_NUM:-}" ]; then
        rm -f "/tmp/.X$DISP_NUM-lock" "/tmp/.X11-unix/X$DISP_NUM" "${XVFB_PIDFILE:-}" 2>/dev/null || true
    fi

    # Clean only validated owned test prefix
    if [[ "${TEST_PREFIX:-}" =~ ^/tmp/wex-token-test\.[a-zA-Z0-9]+$ ]] && [ -d "$TEST_PREFIX" ]; then
        rm -rf "$TEST_PREFIX"
    fi
    exit $exit_code
}
trap cleanup EXIT INT TERM HUP

# Mode handling
MODE="${1:---all}"

if [ "$MODE" = "--dry-run" ]; then
    echo "Dry run successful."
    exit 0
fi

if [ "$MODE" = "--simulate-failure" ]; then
    echo "Simulating runner failure..." >&2
    exit 42
fi

if [ "$MODE" = "--simulate-hang" ]; then
    echo "Simulating runner hang..."
    sleep 30 &
    wait $!
    exit 0
fi

# Allocate isolated display
find_free_display() {
    local disp=100
    while [ -e "/tmp/.X11-unix/X$disp" ] || [ -e "/tmp/.X$disp-lock" ]; do
        disp=$((disp + 1))
    done
    echo "$disp"
}

DISP_NUM=$(find_free_display)
XVFB_PID=""
XVFB_PIDFILE="/tmp/wex-xvfb-$DISP_NUM.pid"

echo "Allocating isolated display :$DISP_NUM..."
Xvfb ":$DISP_NUM" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
sleep 0.5
export DISPLAY=":$DISP_NUM"

# Allocate fresh test prefix
TEST_PREFIX="$(mktemp -d /tmp/wex-token-test.XXXXXX)"
export WINEPREFIX="$TEST_PREFIX"
export WINEDEBUG="-all"

echo "=== Wine Token Security Runner ==="
echo "Display: $DISPLAY (PID $XVFB_PID)"
echo "Prefix: $TEST_PREFIX"
echo "Wine: $WINE_BIN"
echo "Wineserver: $WINESERVER_BIN"

run_advapi_tests() {
    echo "--- Running advapi32 security tests (64-bit) ---"
    local adv64="$BUILD_DIR/dlls/advapi32/tests/x86_64-windows/advapi32_test.exe"
    [ -f "$adv64" ] || adv64="$BUILD_DIR/dlls/advapi32/tests/security.exe"
    if [ -f "$adv64" ]; then
        timeout --kill-after=10s 180s "$WINE_BIN" "$adv64" security
    fi

    echo "--- Running advapi32 security tests (32-bit WOW64) ---"
    local adv32="$BUILD_DIR/dlls/advapi32/tests/i386-windows/advapi32_test.exe"
    [ -f "$adv32" ] || adv32="$BUILD_DIR/dlls/advapi32/tests/i386-windows/security.exe"
    if [ -f "$adv32" ]; then
        timeout --kill-after=10s 180s "$WINE_BIN" "$adv32" security
    fi
}

run_ntdll_tests() {
    echo "--- Running ntdll info tests (64-bit) ---"
    local nt64="$BUILD_DIR/dlls/ntdll/tests/x86_64-windows/ntdll_test.exe"
    [ -f "$nt64" ] || nt64="$BUILD_DIR/dlls/ntdll/tests/info.exe"
    if [ -f "$nt64" ]; then
        timeout --kill-after=10s 180s "$WINE_BIN" "$nt64" info
    fi

    echo "--- Running ntdll info tests (32-bit WOW64) ---"
    local nt32="$BUILD_DIR/dlls/ntdll/tests/i386-windows/ntdll_test.exe"
    [ -f "$nt32" ] || nt32="$BUILD_DIR/dlls/ntdll/tests/i386-windows/info.exe"
    if [ -f "$nt32" ]; then
        timeout --kill-after=10s 180s "$WINE_BIN" "$nt32" info
    fi
}

run_shell32_tests() {
    echo "--- Running shell32 shlexec tests (64-bit) ---"
    local sh64="$BUILD_DIR/dlls/shell32/tests/x86_64-windows/shell32_test.exe"
    [ -f "$sh64" ] || sh64="$BUILD_DIR/dlls/shell32/tests/shlexec.exe"
    if [ -f "$sh64" ]; then
        timeout --kill-after=10s 180s "$WINE_BIN" "$sh64" shlexec
    fi

    echo "--- Running shell32 shlexec tests (32-bit WOW64) ---"
    local sh32="$BUILD_DIR/dlls/shell32/tests/i386-windows/shell32_test.exe"
    [ -f "$sh32" ] || sh32="$BUILD_DIR/dlls/shell32/tests/i386-windows/shlexec.exe"
    if [ -f "$sh32" ]; then
        timeout --kill-after=10s 180s "$WINE_BIN" "$sh32" shlexec
    fi
}

case "$MODE" in
    --checkpoint-a)
        run_advapi_tests
        run_ntdll_tests
        ;;
    --checkpoint-b)
        run_shell32_tests
        ;;
    --all)
        run_advapi_tests
        run_ntdll_tests
        run_shell32_tests
        ;;
    *)
        echo "Unknown mode: $MODE" >&2
        exit 1
        ;;
esac

echo "=== All executed test sets PASSED ==="
