#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Enforced elevation on an installed machine (docs/SYSTEM_TIER_WORK_ORDERS.md, "Enforced elevation on an
# installed machine"): one real elevation by the installed administrator, through the product's own broker and
# consent dialog.
#
# Boots a copy-on-write overlay of a disk installed by install-unattended.sh from an ISO packaged with
# WEX_SESSION0=1 WEX_ELEVATION=1. In it:
#   - the administrator's own programs are limited: a write to HKLM is refused;
#   - a "runas" request shows the User Account Control dialog; Cancel (its default button, pressed with the
#     Enter key) leaves no write and no elevated program;
#   - a second request, answered with Continue, runs the command elevated: the write is there.
# The dialog is found and clicked on the screen (OCR), as a person would.
#
#   elevation.sh SOURCE_VM [TEST_VM]        (default TEST_VM: SOURCE_VM-eltest)
#
# SOURCE_VM (under $VMTEST_DIR) is never written; TEST_VM is replaced by a new overlay, kept for inspection,
# with a random throwaway root password (TEST_VM/rootpw, mode 600, never printed). Screenshots of the dialog
# are kept in TEST_VM/elevation/.
#
# Requires root, QEMU/KVM, qemu-nbd, the x86_64 MinGW compiler, Tesseract and ImageMagick. The VM ends stopped.
set -euo pipefail

usage="usage: elevation.sh SOURCE_VM [TEST_VM]"
SOURCE=${1:?$usage}
TEST=${2:-${SOURCE}-eltest}
for name in "$SOURCE" "$TEST"; do
    [[ "$name" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid test VM name: $name"; exit 2; }
done
if [ "$SOURCE" = "$TEST" ]; then echo "TEST_VM must differ from SOURCE_VM"; exit 2; fi

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
export VMTEST_DIR=${VMTEST_DIR:-/tmp/vmtest}
V=(python3 "$REPO/scripts/vmtest.py")
SRC_DISK=$VMTEST_DIR/$SOURCE/disk.qcow2
STATE=$VMTEST_DIR/$TEST
OUT=$STATE/elevation
MNT=$STATE/root
PWFILE=$STATE/rootpw
NBD=

running() { pgrep -f "file=$1," >/dev/null; }
detach() {
    if mountpoint -q "$MNT"; then umount "$MNT" 2>/dev/null || true; fi
    if [ -n "$NBD" ]; then qemu-nbd --disconnect "$NBD" >/dev/null 2>&1 || true; fi
    NBD=
}
cleanup() { detach; "${V[@]}" stop "$TEST" >/dev/null 2>&1 || true; }
trap cleanup EXIT
fail() { echo "FAIL: $*"; "${V[@]}" shot "$TEST" "$OUT/fail.png" >/dev/null 2>&1 || true; exit 1; }
guest() { "${V[@]}" sh "$TEST" root "$PWFILE" "$1" --timeout "${2:-120}" 2>/dev/null | tr -d '\r'; }

[ -f "$SRC_DISK" ] || fail "no disk $SRC_DISK"
if running "$SRC_DISK"; then fail "$SOURCE is running; stop it first"; fi
if [ -e "$STATE/disk.qcow2" ]; then
    qemu-img info "$STATE/disk.qcow2" 2>/dev/null | grep -qx "backing file: $SRC_DISK" ||
        fail "$STATE/disk.qcow2 exists and is not an overlay of $SOURCE; choose another TEST_VM"
    "${V[@]}" stop "$TEST" >/dev/null 2>&1 || true
    for _ in $(seq 1 30); do running "$STATE/disk.qcow2" || break; sleep 1; done
fi
rm -rf -- "$STATE"
mkdir -p "$OUT"
qemu-img create -q -f qcow2 -b "$SRC_DISK" -F qcow2 "$STATE/disk.qcow2" || fail "cannot create the overlay"
head -c 64 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 16 > "$PWFILE"
chmod 600 "$PWFILE"
x86_64-w64-mingw32-gcc -O2 -Wall -DWEXELEVATE_TEST_APPROVAL -I"$REPO/src/include" -o "$STATE/elprobe.exe" \
    "$REPO/scripts/wexelevate_probe.c" -ladvapi32 -lshell32 -lmsi || fail "the probe does not build"

echo "=== $TEST: enforced elevation on an overlay of $SOURCE"

modprobe nbd max_part=16
for entry in /sys/block/nbd*; do
    if [ ! -e "$entry/pid" ]; then NBD=/dev/${entry##*/}; break; fi
done
[ -n "$NBD" ] || fail "no unused NBD device"
qemu-nbd --connect "$NBD" "$STATE/disk.qcow2" || { NBD=; fail "cannot connect the overlay"; }
udevadm settle 2>/dev/null || true
rootdev=
for _ in $(seq 1 30); do
    for part in "$NBD"p*; do
        if [ -b "$part" ] && [ "$(blkid -o value -s TYPE "$part" 2>/dev/null)" = ext4 ]; then rootdev=$part; fi
    done
    [ -z "$rootdev" ] || break
    sleep 1
done
[ -n "$rootdev" ] || fail "no ext4 root file system on $NBD"
mkdir -p "$MNT"
mount "$rootdev" "$MNT" || fail "cannot mount the overlay"
[ -f "$MNT/usr/local/share/wineexplorer/elevation.enable" ] ||
    fail "the source was not installed from an image with enforced elevation (no /usr/local/share/wineexplorer/elevation.enable)"
printf 'root:%s\n' "$(cat "$PWFILE")" | chroot "$MNT" chpasswd || fail "cannot set the root password"
install -m 0755 "$STATE/elprobe.exe" "$MNT/opt/elprobe.exe"
install -m 0700 "$HERE/elevation-guest.sh" "$MNT/root/el-guest.sh"
detach

"${V[@]}" start "$TEST" >/dev/null || fail "QEMU did not start"
"${V[@]}" wait "$TEST" "My Documents" --timeout 300 || fail "no desktop after boot"
sleep 5

RESULT=$(guest "bash /root/el-guest.sh before" 300)
printf '%s\n' "$RESULT"

# one request and its answer: ask NAME BUTTON
ask() {
    guest "(bash /root/el-guest.sh ask $1 &); echo started" 30 >/dev/null
    "${V[@]}" wait "$TEST" "Account Control" --timeout 60 || return 1
    "${V[@]}" shot "$TEST" "$OUT/consent-$1.png" >/dev/null 2>&1 || true
    case "$2" in
        key:*) sleep 2; "${V[@]}" key "$TEST" "${2#key:}" >/dev/null || return 1 ;;
        *) "${V[@]}" clicktext "$TEST" "$2" --timeout 20 >/dev/null || return 1 ;;
    esac
    for _ in $(seq 1 30); do
        [ -z "$(guest "grep -c 'probe rc=' /root/el-$1.log 2>/dev/null | grep -x 1" 30)" ] || return 0
        sleep 2
    done
    return 1
}

# Cancel is the dialog's default button: the Enter key presses it (the screen reading does not find the word
# under the button's focus mark)
if ask denied key:ret; then
    if [ -z "$(guest "bash /root/el-guest.sh value denied")" ] && [ -n "$(guest "grep -c 'shexec=PASS' /root/el-denied.log | grep -x 0")" ]; then
        RESULT+=$'\n'"ok cancel_leaves_no_write"
    else RESULT+=$'\n'"not ok cancel_leaves_no_write: value [$(guest "bash /root/el-guest.sh value denied")] $(guest "tail -3 /root/el-denied.log" | tr '\n' ' ')"; fi
else RESULT+=$'\n'"not ok consent_dialog_shown: no User Account Control dialog for the first request, or it did not end"; fi
echo "$RESULT" | tail -1
# the first dialog is gone before the second request
for _ in $(seq 1 15); do "${V[@]}" text "$TEST" 2>/dev/null | grep -q 'Account Control' || break; sleep 2; done

if ask granted Continue; then
    if [ "$(guest "bash /root/el-guest.sh value granted")" = 1 ] && [ -n "$(guest "grep -c 'shexec=PASS' /root/el-granted.log | grep -x 1")" ]; then
        RESULT+=$'\n'"ok elevated_command_wrote_hklm"
    else RESULT+=$'\n'"not ok elevated_command_wrote_hklm: value [$(guest "bash /root/el-guest.sh value granted")] $(guest "tail -3 /root/el-granted.log" | tr '\n' ' ')"; fi
else RESULT+=$'\n'"not ok consent_dialog_answered: no dialog, or no Continue button, for the second request"; fi
echo "$RESULT" | tail -1

"${V[@]}" shutdown "$TEST" --timeout 120 >/dev/null 2>&1 || true

NOTOK=$(grep '^not ok ' <<<"$RESULT" || true)
if [ -n "$NOTOK" ]; then echo "FAIL: enforced elevation on the installed machine:"; printf '%s\n' "$NOTOK"; exit 1; fi
OKCOUNT=$(grep -c '^ok ' <<<"$RESULT" || true)
[ "$OKCOUNT" -ge 6 ] || { echo "FAIL: enforced elevation on the installed machine (only $OKCOUNT checks reported)"; exit 1; }
echo "PASS: enforced elevation on the installed machine ($OKCOUNT checks)"
