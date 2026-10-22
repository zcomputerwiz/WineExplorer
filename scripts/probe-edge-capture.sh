#!/bin/bash
export WINEPREFIX="/home/wineuser/edge-test/pfx"
export WINEDLLOVERRIDES="dcomp=d"
export WINEDEBUG="+process"
export DISPLAY=":95"

cd /home/wineuser/edge-test/runtime-x86/Microsoft.WebView2.FixedVersionRuntime.155.0.4283.62.x86

rm -rf "/home/wineuser/edge-test/pfx/drive_c/edge-data-sb2"

/home/wineuser/build/wow64/wine explorer /desktop=Edge,1024x768 msedge.exe \
  --disable-direct-composition \
  --use-gl=angle \
  --use-angle=swiftshader \
  --enable-unsafe-swiftshader \
  --user-data-dir="C:\\edge-data-sb2" \
  --disable-crashpad \
  --no-first-run \
  --no-default-browser-check \
  --disable-fre \
  --app="http://example.com" > /tmp/edge-run2.log 2>&1 &
EDGE_PID=$!

echo "Edge started with PID $EDGE_PID, sleeping 8s..."
sleep 8

echo "Taking screenshot..."
import -display :95 -window root /tmp/edge_sandbox_rendered.png

echo "Process list in wine:"
/home/wineuser/build/wow64/wine winectl ps 2>/dev/null || true
ps aux | grep -i msedge.exe | grep -v grep

sleep 2
echo "Terminating Edge..."
kill -TERM $EDGE_PID 2>/dev/null
pkill -f msedge.exe 2>/dev/null
echo "Probe complete."
