#!/bin/bash
# build-wine.sh : sync the D: wine tree to the clone and build; prints the first errors
bash /root/sync-wine.sh >/dev/null
cd /root/WE-prefetch/build/wow64
make -j16 > /root/build-p1.log 2>&1; echo make-exit $?
grep -E "error:|Error |warning: .*(dwm|server/dwm)" /root/build-p1.log | head -${1:-30}
