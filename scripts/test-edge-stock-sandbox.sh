#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Scoped, safe browser network test runner (R7 & R8)
set -euo pipefail

# Allocate free display
find_free_display() {
    local disp=95
    while [ -e "/tmp/.X11-unix/X$disp" ] || [ -e "/tmp/.X$disp-lock" ]; do
        disp=$((disp + 1))
    done
    echo "$disp"
}

DISP_NUM=$(find_free_display)
export DISPLAY=":$DISP_NUM"

Xvfb "$DISPLAY" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
XPID=$!

WINEPREFIX="$(mktemp -d /tmp/wex-edge-net-pfx.XXXXXX)"
export WINEPREFIX
if [ -d "/home/wineuser/edge-test/pfx" ]; then
    cp -a /home/wineuser/edge-test/pfx/. "$WINEPREFIX/"
fi
export WINEDLLOVERRIDES="dcomp=d"
WINE_BUILD="/home/wineuser/build/wow64"
WINE="$WINE_BUILD/wine"
WINESERVER="$WINE_BUILD/server/wineserver"
[ -x "$WINESERVER" ] || WINESERVER="/usr/local/bin/wineserver"

cleanup() {
    local exit_code=$?
    echo "=== Cleaning up scoped test environment ==="
    if [ -n "${EDGE_PID:-}" ] && kill -0 "$EDGE_PID" 2>/dev/null; then
        kill -TERM "$EDGE_PID" 2>/dev/null || true
    fi
    if [ -n "${EXPLORER_PID:-}" ] && kill -0 "$EXPLORER_PID" 2>/dev/null; then
        kill -TERM "$EXPLORER_PID" 2>/dev/null || true
    fi
    WINEPREFIX="$WINEPREFIX" "$WINESERVER" -k 2>/dev/null || true
    WINEPREFIX="$WINEPREFIX" "$WINESERVER" -w 2>/dev/null || true
    if [ -n "${XPID:-}" ] && kill -0 "$XPID" 2>/dev/null; then
        kill "$XPID" 2>/dev/null || true
    fi
    rm -f "/tmp/.X$DISP_NUM-lock" "/tmp/.X11-unix/X$DISP_NUM" 2>/dev/null || true
    if [[ "$WINEPREFIX" =~ ^/tmp/wex-edge-net-pfx\.[a-zA-Z0-9]+$ ]] && [ -d "$WINEPREFIX" ]; then
        rm -rf "$WINEPREFIX"
    fi
    exit $exit_code
}
trap cleanup EXIT INT TERM HUP

echo "=== Starting Wine desktop shell on $DISPLAY ==="
"$WINE" explorer.exe /desktop=Shell,1024x768 >/dev/null 2>&1 &
EXPLORER_PID=$!
sleep 3

rm -rf "$WINEPREFIX/drive_c/edge-data-sb"

echo "=== Starting Edge WITHOUT --no-sandbox (Stock Policy) ==="
cd /home/wineuser/edge-test/runtime-x86/Microsoft.WebView2.FixedVersionRuntime.155.0.4283.62.x86

"$WINE" msedge.exe \
  --disable-direct-composition \
  --use-gl=angle \
  --use-angle=swiftshader \
  --enable-unsafe-swiftshader \
  --user-data-dir="C:\\edge-data-sb" \
  --disable-crashpad \
  --no-first-run \
  --no-default-browser-check \
  --disable-fre \
  --app="http://example.com" > /tmp/edge-sandbox-run.log 2>&1 &
EDGE_PID=$!

echo "Edge started with PID $EDGE_PID. Waiting 18s for rendering..."
sleep 18

echo "=== Capturing active processes ==="
ps -u "$USER" -o pid,ppid,args | grep -i msedge.exe | grep -v grep > /tmp/edge-processes.txt || true
cat /tmp/edge-processes.txt

echo "=== Capturing screenshot ==="
import -display "$DISPLAY" -window root /tmp/edge_sandbox_capture.png

echo "=== Run complete. Capture size: ==="
ls -lh /tmp/edge_sandbox_capture.png
