#!/bin/bash
# run dcomp test with traces in a plain prefix on Xvfb
cd /root/WE-prefetch/build/wow64
pkill -9 -x wineserver; pkill -9 Xvfb; sleep 1
export DISPLAY=:79 WINEPREFIX=/root/dc-pfx WINEDLLOVERRIDES="mscoree,mshtml="
Xvfb :79 -screen 0 1024x768x24 >/dev/null 2>&1 &
sleep 2
[ -d $WINEPREFIX ] || timeout 150 ./wine wineboot -i >/dev/null 2>&1
WINEDEBUG=trace+dcomp,trace+d3d,trace+dxgi
grep -aE "Test failed|tests executed" /root/dc1.log
grep -aiE "aux|target_scene|surface|frame|fixme|err:|warn" /root/dc1.log | grep -v "trace:dcomp:dcomp_visual_Set\|AddRef\|Release" | cut -c1-200 | head -40
./server/wineserver -k; pkill -9 Xvfb
