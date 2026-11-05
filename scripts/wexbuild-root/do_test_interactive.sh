#!/bin/bash
set -euo pipefail
ROOT_DIR=/root/WineExplorer
cd "$ROOT_DIR"

ISO=/root/wineexplorer-wow64.iso
PWFILE=/root/install-test-password.txt
VM=gemini-wow64-test

START=$(date +%s)
echo "=== Interactive Install Test starting at $(date) ==="
bash "$ROOT_DIR/installer/tests/install-interactive.sh" "$ISO" "$PWFILE" "$VM"
END=$(date +%s)
DUR=$(( END - START ))
echo "=== Interactive Install Test completed in ${DUR}s (${DUR} seconds) ==="
