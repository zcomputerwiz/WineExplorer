#!/bin/bash
set -euo pipefail
cd /root/WineExplorer/build/wow64
START=$(date +%s)
echo "=== Make starting at $(date) ==="
make -j16
END=$(date +%s)
DURATION=$(( END - START ))
echo "=== Make completed in ${DURATION}s (${DURATION} seconds) ==="
