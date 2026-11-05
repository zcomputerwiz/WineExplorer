#!/bin/bash
# run the dxgi conformance test in a plain prefix on Xvfb (no shell)
cd /root/WE-prefetch/build/wow64
pkill -9 -x wineserver; pkill -9 Xvfb; sleep 1
export DISPLAY=:80 WINEPREFIX=/root/dxgi-pfx WINEDLLOVERRIDES="mscoree,mshtml="
Xvfb :80 -screen 0 1024x768x24 >/dev/null 2>&1 &
sleep 2
[ -d $WINEPREFIX ] || timeout 150 ./wine wineboot -i >/dev/null 2>&1
timeout 180 ./wine dlls/dxgi/tests/x86_64-windows/dxgi_test.exe dxgi 2>&1 | grep -aE "Test failed|tests executed|todo_wine|fail\(|ok\(" | head -80
./server/wineserver -k; pkill -9 Xvfb
