#!/bin/bash
export WINEPREFIX="/home/wineuser/edge-test/pfx"
export WINEDLLOVERRIDES="dcomp=d"
export WINEDEBUG="+token,+security"
export DISPLAY=":95"

cd /home/wineuser/edge-test/runtime-x86/Microsoft.WebView2.FixedVersionRuntime.155.0.4283.62.x86

timeout 15 /home/wineuser/build/wow64/wine msedge.exe \
  --disable-direct-composition \
  --use-gl=angle \
  --use-angle=swiftshader \
  --enable-unsafe-swiftshader \
  --user-data-dir="C:\\edge-data-sb" \
  --disable-crashpad \
  --no-first-run \
  --no-default-browser-check \
  --disable-fre \
  --enable-logging=stderr \
  --v=1 \
  --app="http://example.com" > /tmp/edge-sandbox.log 2>&1

res=$?
echo "PROBE FINISHED with exit $res"
