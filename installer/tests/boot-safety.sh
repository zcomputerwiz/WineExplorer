#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Boot an ISO to EXPECTED_TEXT and verify that no logical disk byte changed.
# Use an ISO whose default entry exercises the scenario. Replaces a disposable
# QEMU test disk and ends with the VM stopped. Requires qemu-img and qemu-io.
set -euo pipefail
ISO=${1:?usage: boot-safety.sh ISO EXPECTED_TEXT [VM_NAME]}
EXPECTED=${2:?expected screen text required}
VM=${3:-wex-safety-codex}
[[ "$VM" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid test VM name"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
V=(python3 "$HERE/../../scripts/vmtest.py")
STATE=${VMTEST_DIR:-/tmp/vmtest}/$VM
"${V[@]}" stop "$VM" 2>/dev/null || true
rm -rf "$STATE"
mkdir -p "$STATE"
qemu-img create -q -f qcow2 "$STATE/disk.qcow2" 20G
qemu-io -f qcow2 -c 'write -P 0x5a 0 1048576' "$STATE/disk.qcow2" >/dev/null
cp --reflink=auto "$STATE/disk.qcow2" "$STATE/baseline.qcow2"
"${V[@]}" start "$VM" --iso "$ISO"
if ! "${V[@]}" wait "$VM" "$EXPECTED" --timeout 240; then
    "${V[@]}" shot "$VM" "$STATE/fail.png" >/dev/null
    echo "FAIL: expected safety screen not reached"
    exit 1
fi
"${V[@]}" shot "$VM" "$STATE/safety-screen.png" >/dev/null
"${V[@]}" stop "$VM"
qemu-img compare "$STATE/baseline.qcow2" "$STATE/disk.qcow2"
echo "PASS: expected screen reached and logical disk contents unchanged; evidence in $STATE"
