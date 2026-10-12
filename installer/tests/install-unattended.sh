#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Zero-input install of an ISO built with --unattended and the example account
# (wineuser, hostname WEX-PC). Replaces the named disposable QEMU disk.
# Requires root, QEMU/KVM, qemu-nbd, Tesseract and ImageMagick. The VM ends stopped.
set -euo pipefail
ISO=${1:?usage: install-unattended.sh ISO [VM_NAME]}
VM=${2:-wex-unattended-codex}
[[ "$VM" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid test VM name"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
V=(python3 "$HERE/../../scripts/vmtest.py")
STATE=/tmp/vmtest/$VM
NBD=
CONNECTED=false
MOUNTED=false
cleanup() {
    if $MOUNTED; then umount "$STATE/check-root"; fi
    if $CONNECTED; then qemu-nbd --disconnect "$NBD" >/dev/null; fi
}
trap cleanup EXIT
fail() {
    echo "FAIL: $*"
    "${V[@]}" shot "$VM" "$STATE/fail.png" >/dev/null 2>&1 || true
    exit 1
}
"${V[@]}" stop "$VM" 2>/dev/null || true
rm -rf "$STATE"
"${V[@]}" start "$VM" --iso "$ISO"
# No key, click or type commands: both transitions must be automatic.
"${V[@]}" wait "$VM" "has completed successfully" --timeout 1200 || fail "text setup did not complete"
"${V[@]}" shot "$VM" "$STATE/copied.png" >/dev/null
"${V[@]}" wait "$VM" "My Documents" --timeout 900 || fail "no automatic desktop after setup"
"${V[@]}" wait "$VM" "Recycle Bin" --timeout 30 || fail "desktop icons missing"
"${V[@]}" shot "$VM" "$STATE/desktop.png" >/dev/null
"${V[@]}" shutdown "$VM"

# Verify the installed disk, not merely a live desktop bearing the same label.
modprobe nbd max_part=16
for entry in /sys/block/nbd*; do
    if [ ! -e "$entry/pid" ]; then NBD=/dev/${entry##*/}; break; fi
done
[ -n "$NBD" ] || fail "no unused NBD device"
qemu-nbd --read-only --connect "$NBD" "$STATE/disk.qcow2"
CONNECTED=true
udevadm settle
for attempt in $(seq 1 30); do
    [ -b "${NBD}p1" ] && break
    sleep 1
done
mkdir -p "$STATE/check-root"
mount -o ro,noload "${NBD}p1" "$STATE/check-root"
MOUNTED=true
ROOT=$STATE/check-root
python3 "$HERE/check-lightdm.py" "$ROOT" wineuser || fail "installed display manager checks failed"
grep -qx 'WEX-PC' "$ROOT/etc/hostname" || fail "hostname was not applied"
grep -q '^wineuser:' "$ROOT/etc/passwd" || fail "installed user missing"
! grep -q '^wexsetup:' "$ROOT/etc/passwd" || fail "temporary setup account remains"
[ ! -e "$ROOT/etc/wineexplorer/setup" ] || fail "setup state remains"
[ ! -e "$ROOT/etc/polkit-1/rules.d/49-wexsetup.rules" ] || fail "setup authorization remains"
[ -f "$ROOT/home/wineuser/.wine/user.reg" ] || fail "user prefix missing"
for package in wineexplorer wineexplorer-session; do
    [ -s "$ROOT/usr/share/doc/$package/COPYING" ] || fail "$package license missing"
done
echo "PASS: zero-input install and installed-disk checks; evidence in $STATE"
