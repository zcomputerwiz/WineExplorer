#!/bin/bash
# p4-build.sh : sync wine + src overlays into the clone, regenerate makefiles when asked (arg "regen"), build the DWM pieces
bash /root/sync-wine.sh >/dev/null
S=/mnt/d/DeepThought/Projects/WineExplorer/src
D=/root/WE-prefetch/src
for p in dlls/wexhost dlls/wexshell programs/dwm; do mkdir -p $D/$p; cp -r $S/$p/. $D/$p/; done
cp $S/include/* $D/include/
if [ "$1" = regen ]; then
  cd /root/WE-prefetch
  W=/mnt/d/DeepThought/Projects/WineExplorer/wine
  cp $W/include/Makefile.in wine/include/Makefile.in; cp $W/server/Makefile.in wine/server/Makefile.in
  python3 scripts/assemble.py --wine-root wine
  (cd wine && autoconf -f)
  cd build/wow64 && ./config.status --recheck >/tmp/recheck.log 2>&1 && ./config.status >/tmp/status.log 2>&1
  tail -3 /tmp/status.log
fi
cd /root/WE-prefetch/build/wow64
make -j16 server/wineserver dlls/dwmapi/all dlls/wexhost/all dlls/wexshell/all programs/dwm/all programs/explorer/all 2>&1 | grep -E "error|warning|\*\*\*" | grep -v "token.c" | head -30
echo build-done
