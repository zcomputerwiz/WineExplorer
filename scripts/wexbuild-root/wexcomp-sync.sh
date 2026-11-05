#!/bin/bash
# wexcomp-sync.sh: copy D:\...\wexcomp into /root/wexcomp-dev (CRs stripped), build, run the focused effects tests
set -e
cd /mnt/d/DeepThought/Projects/WineExplorer/wexcomp
tar --exclude=build --exclude=.git --exclude="*.o" -cf - . | tar -C /root/wexcomp-dev -xf -
cd /root/wexcomp-dev
find . -type f \( -name "*.c" -o -name "*.h" -o -name "*.xml" -o -name "meson.build" -o -name "*.sh" -o -name "*.py" \) -not -path "./build/*" -exec sed -i "s/\r$//" {} +
timeout 1500 ninja -C build 2>&1 | grep -E "error|warning: unused|FAILED" | head -20 || true
if [ "$1" != nobuild ]; then cd tests/wex && timeout 900 bash run-effects.sh /root/wexcomp-out2 > /tmp/eff.log 2>&1 || true; grep -E "\[PASS\]|\[FAIL\]|effects run|Assertion|rror" /tmp/eff.log | tail -${TAILN:-40}; fi
