#!/bin/bash
set -euo pipefail
cd /root/WineExplorer/build/wow64
START=$(date +%s)
echo "=== Configure starting at $(date) ==="
../../wine/configure --enable-archs=i386,x86_64 --prefix="$PWD/install"
END=$(date +%s)
DURATION=$(( END - START ))
echo "=== Configure completed in ${DURATION}s (${DURATION} seconds) ==="
