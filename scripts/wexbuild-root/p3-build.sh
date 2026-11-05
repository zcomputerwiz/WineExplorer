#!/bin/bash
# p3-build.sh : sync the D: trees, rebuild server, dwmapi (+tests), dwm.exe, explorer
bash /root/sync-wine.sh >/dev/null
cp /mnt/d/DeepThought/Projects/WineExplorer/src/programs/dwm/* /root/WE-prefetch/src/programs/dwm/
cd /root/WE-prefetch/build/wow64
make -j16 server/all dlls/dwmapi/all dlls/dwmapi/tests/all programs/dwm/all programs/explorer/all 2>&1 | grep -E "error|warning|\*\*\*" | head -30
echo build-done
