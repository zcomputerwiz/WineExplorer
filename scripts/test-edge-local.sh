#!/bin/bash
set -e

echo "=== Cleaning old processes ==="
killall -9 Xvfb wineserver wine msedge.exe msedgewebview2.exe explorer.exe 2>/dev/null || true
sleep 1

export DISPLAY=:95
rm -f /tmp/.X95-lock /tmp/.X11-unix/X95
Xvfb :95 -screen 0 1024x768x24 >/dev/null 2>&1 &
XPID=$!
sleep 1

export WINEPREFIX="/home/wineuser/edge-test/pfx"
export WINEDLLOVERRIDES="dcomp=d"
export WINE="/home/wineuser/build/wow64/wine"

echo "=== Starting Wine desktop shell ==="
$WINE explorer.exe /desktop=Shell,1024x768 >/dev/null 2>&1 &
sleep 3

rm -rf "$WINEPREFIX/drive_c/edge-data-local"

echo "=== Starting Edge with Local Page and Stock Sandbox (NO --no-sandbox) ==="
cd /home/wineuser/edge-test/runtime-x86/Microsoft.WebView2.FixedVersionRuntime.155.0.4283.62.x86

$WINE msedge.exe \
  --disable-direct-composition \
  --disable-features=RendererAppContainer,Win32kLockdown \
  --use-gl=angle \
  --use-angle=swiftshader \
  --enable-unsafe-swiftshader \
  --user-data-dir="C:\\edge-data-local" \
  --disable-crashpad \
  --no-first-run \
  --no-default-browser-check \
  --disable-fre \
  --test-type \
  --app="file:///Z:/tmp/edge_test.html" > /tmp/edge-local-run.log 2>&1 &
EDGE_PID=$!

echo "Edge started with PID $EDGE_PID. Waiting 25s for rendering..."
sleep 25

echo "=== Capturing active processes ==="
ps aux | grep -i msedge.exe | grep -v grep > /tmp/edge-local-processes.txt
cat /tmp/edge-local-processes.txt

echo "=== Capturing screenshot ==="
import -display :95 -window root /tmp/edge_local_capture.png

echo "=== Cleaning up ==="
kill -TERM $EDGE_PID 2>/dev/null || true
sleep 1
killall -9 wineserver wine msedge.exe explorer.exe 2>/dev/null || true
kill -9 $XPID 2>/dev/null || true

echo "=== Run complete. Capture size: ==="
ls -lh /tmp/edge_local_capture.png
