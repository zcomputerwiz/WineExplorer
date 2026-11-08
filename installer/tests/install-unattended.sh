#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Zero-input install of an ISO built with --unattended and the example account
# (wineuser, hostname WEX-PC). Replaces the named disposable QEMU disk.
# Requires root, QEMU/KVM, qemu-nbd, Tesseract and ImageMagick. The VM ends stopped.
#
#   install-unattended.sh ISO [VM_NAME]
#   WEX_TEST_FIRMWARE=uefi install-unattended.sh ISO [VM_NAME]     (OVMF; default bios, SeaBIOS)
set -euo pipefail
ISO=${1:?usage: install-unattended.sh ISO [VM_NAME]}
VM=${2:-wex-unattended-codex}
[[ "$VM" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid test VM name"; exit 2; }
FIRMWARE=${WEX_TEST_FIRMWARE:-bios}
case "$FIRMWARE" in
    bios) START_ARGS=() ;;
    uefi) START_ARGS=(--efi) ;;
    *) echo "Invalid firmware: $FIRMWARE"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
V=(python3 "$HERE/../../scripts/vmtest.py")
STATE=${VMTEST_DIR:-/tmp/vmtest}/$VM
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
"${V[@]}" start "$VM" --iso "$ISO" "${START_ARGS[@]}"
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
    [ -b "${NBD}p4" ] && break
    sleep 1
done
# GPT with BIOS boot, EFI system, boot storage and the root (docs/VINTNER_BOOT.md section 4.1).
ROOTDEV=$(python3 "$HERE/check-disk-layout.py" "$NBD") || fail "installed partition layout is wrong"
mkdir -p "$STATE/check-root"
mount -o ro,noload "$ROOTDEV" "$STATE/check-root"
MOUNTED=true
ROOT=$STATE/check-root
python3 "$HERE/check-disk-layout.py" "$NBD" "$ROOT" >/dev/null || fail "installed fstab is wrong"
if [ "$FIRMWARE" = uefi ]; then
    grep -q '[[:space:]]/boot/efi[[:space:]]' "$ROOT/etc/fstab" || fail "UEFI install does not mount the EFI system partition"
fi
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
