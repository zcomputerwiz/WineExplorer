#!/bin/bash
# p2-build.sh : register src/programs/dwm in the clone, reconfigure, build dwm.exe and explorer.exe
set -e
bash /root/sync-wine.sh >/dev/null
cd /root/WE-prefetch
python3 scripts/assemble.py --wine-root wine
cp wine/include/Makefile.in /tmp/inc.Makefile.in.assembled 2>/dev/null || true
cd wine && autoconf -f && cd ../build/wow64 && ./config.status --recheck >/tmp/recheck.log 2>&1 && ./config.status >/tmp/status.log 2>&1
make -j16 programs/dwm programs/explorer 2>&1 | grep -E "error|warning: impl|\*\*\*" | head -20
ls -la programs/dwm/*/dwm.exe programs/explorer/*/explorer.exe 2>&1
echo p2-build-done
