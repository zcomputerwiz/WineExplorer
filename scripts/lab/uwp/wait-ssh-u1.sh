#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Wait for the specifically owned U1 guest, verifying its process and host key.
set -euo pipefail

TIMEOUT=${1:-120}
case "$TIMEOUT" in ''|*[!0-9]*) echo 'timeout must be a positive integer' >&2; exit 2;; esac
[ "$TIMEOUT" -gt 0 ] && [ "$TIMEOUT" -le 600 ] || exit 2
START=$(date +%s)
echo "Waiting for the owned U1 guest (timeout: ${TIMEOUT}s)..."

while true; do
    if timeout --kill-after=2s 20s python3 "$(dirname "$0")/owned-lab.py" ready; then
        NOW=$(date +%s)
        ELAPSED=$((NOW - START))
        echo "U1 guest SSH is ready after ${ELAPSED}s!"
        exit 0
    fi
    NOW=$(date +%s)
    if [ $((NOW - START)) -ge "$TIMEOUT" ]; then
        echo "Timed out waiting for U1 guest SSH after ${TIMEOUT}s." >&2
        exit 1
    fi
    sleep 2
done
