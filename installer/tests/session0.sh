#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Session 0 on an installed machine (docs/SYSTEM_TIER_WORK_ORDERS.md, "Session 0 on an installed machine").
#
# Boots a copy-on-write overlay of an installed disk made by install-unattended.sh from an ISO packaged with
# WEX_SESSION0=1, and runs installer/tests/session0-guest.sh inside it over the serial console. The guest
# script checks session 0 on the running system (the units and the socket, the registry through session 0,
# a second user, a forwarded service, a quiet terminal, and a restart of session 0) and prints one line per
# check; this script decides PASS/FAIL from those lines.
#
#   session0.sh SOURCE_VM [TEST_VM]        (default TEST_VM: SOURCE_VM-s0test)
#
# SOURCE_VM is an installed disk under $VMTEST_DIR (SOURCE_VM/disk.qcow2) and is never written. TEST_VM is
# replaced by a new overlay of it, which is kept for inspection; an existing TEST_VM disk that is not an
# overlay of SOURCE_VM is refused. The overlay's root gets a random throwaway password (TEST_VM/rootpw, mode
# 600, never printed). Before the boot the test checks, offline on the overlay, that the source was installed
# from a session 0 image.
#
#   WEX_TEST_FIRMWARE=uefi        boot with OVMF (default bios, SeaBIOS): the firmware SOURCE_VM was installed with
#
# Requires root, QEMU/KVM, qemu-nbd, Tesseract and ImageMagick (scripts/vmtest.py). The VM ends stopped.
set -euo pipefail

usage="usage: session0.sh SOURCE_VM [TEST_VM]"
SOURCE=${1:?$usage}
TEST=${2:-${SOURCE}-s0test}
for name in "$SOURCE" "$TEST"; do
    [[ "$name" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid test VM name: $name"; exit 2; }
done
if [ "$SOURCE" = "$TEST" ]; then echo "TEST_VM must differ from SOURCE_VM"; exit 2; fi

FIRMWARE=${WEX_TEST_FIRMWARE:-bios}
case "$FIRMWARE" in
    bios) START_ARGS=() ;;
    uefi) START_ARGS=(--efi) ;;
    *) echo "Invalid firmware: $FIRMWARE"; exit 2 ;;
esac

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
export VMTEST_DIR=${VMTEST_DIR:-/tmp/vmtest}
V=(python3 "$REPO/scripts/vmtest.py")
SRC_DISK=$VMTEST_DIR/$SOURCE/disk.qcow2
STATE=$VMTEST_DIR/$TEST
OUT=$STATE/session0
MNT=$STATE/root
PWFILE=$STATE/rootpw
GUEST_USER=wineuser
NBD=

running() { pgrep -f "file=$1," >/dev/null; }
wait_off() {
    local _
    for _ in $(seq 1 "$1"); do
        if ! running "$STATE/disk.qcow2"; then return 0; fi
        sleep 1
    done
    return 1
}

attach() {   # attach ro|rw: connect the overlay and mount its root file system on $MNT
    local entry part rootdev='' _
    modprobe nbd max_part=16 || return 1
    NBD=
    for entry in /sys/block/nbd*; do
        if [ ! -e "$entry/pid" ]; then NBD=/dev/${entry##*/}; break; fi
    done
    if [ -z "$NBD" ]; then echo "no unused NBD device"; return 1; fi
    if [ "$1" = ro ]; then
        qemu-nbd --read-only --connect "$NBD" "$STATE/disk.qcow2" || { NBD=; return 1; }
    else
        qemu-nbd --connect "$NBD" "$STATE/disk.qcow2" || { NBD=; return 1; }
    fi
    udevadm settle 2>/dev/null || true
    for _ in $(seq 1 30); do
        for part in "$NBD"p*; do
            if [ -b "$part" ] && [ "$(blkid -o value -s TYPE "$part" 2>/dev/null)" = ext4 ]; then rootdev=$part; fi
        done
        if [ -n "$rootdev" ]; then break; fi
        sleep 1
    done
    if [ -z "$rootdev" ]; then echo "no ext4 root file system on $NBD"; detach; return 1; fi
    mkdir -p "$MNT"
    if [ "$1" = ro ]; then
        mount -o ro,noload "$rootdev" "$MNT" || { detach; return 1; }
    else
        mount "$rootdev" "$MNT" || { detach; return 1; }
    fi
}

detach() {
    if mountpoint -q "$MNT"; then umount "$MNT" 2>/dev/null || true; fi
    if [ -n "$NBD" ]; then qemu-nbd --disconnect "$NBD" >/dev/null 2>&1 || true; fi
    NBD=
}

cleanup() {
    detach
    "${V[@]}" stop "$TEST" >/dev/null 2>&1 || true
}
trap cleanup EXIT

fail() {
    echo "FAIL: $*"
    "${V[@]}" shot "$TEST" "$OUT/fail.png" >/dev/null 2>&1 || true
    exit 1
}

# ---- the overlay ----

command -v qemu-nbd >/dev/null || fail "qemu-nbd is missing"
if [ ! -f "$SRC_DISK" ]; then fail "no disk $SRC_DISK"; fi
if running "$SRC_DISK"; then fail "$SOURCE is running; stop it first"; fi
if [ -e "$STATE/disk.qcow2" ]; then
    if ! qemu-img info "$STATE/disk.qcow2" 2>/dev/null | grep -qx "backing file: $SRC_DISK"; then
        fail "$STATE/disk.qcow2 exists and is not an overlay of $SOURCE; choose another TEST_VM"
    fi
    "${V[@]}" stop "$TEST" >/dev/null 2>&1 || true
    wait_off 30 || fail "$TEST is still running"
fi
rm -rf -- "$STATE"
mkdir -p "$OUT"
qemu-img create -q -f qcow2 -b "$SRC_DISK" -F qcow2 "$STATE/disk.qcow2" || fail "cannot create the overlay"

# a random throwaway password for the overlay's root, for this test only (never printed)
head -c 64 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 16 > "$PWFILE"
chmod 600 "$PWFILE"

echo "=== $TEST: session 0 on an overlay of $SOURCE, $FIRMWARE"

# offline, on the overlay: the source must be a session 0 image, and the overlay's root gets the password
attach rw || fail "cannot mount the overlay"
if [ ! -f "$MNT/usr/local/share/wineexplorer/session0.enable" ]; then
    fail "the source was not installed from a session 0 image (no /usr/local/share/wineexplorer/session0.enable)"
fi
printf 'root:%s\n' "$(cat "$PWFILE")" | chroot "$MNT" chpasswd || fail "cannot set the root password"
detach

# ---- the boot ----

"${V[@]}" start "$TEST" "${START_ARGS[@]}" >/dev/null || fail "QEMU did not start"
"${V[@]}" wait "$TEST" "My Documents" --timeout 300 || fail "no desktop after boot"

# send the guest script in base64 pieces (the serial console's line length is limited); base64 has no quote,
# so the single quotes keep every piece intact
B64=$(base64 -w0 < "$HERE/session0-guest.sh")
"${V[@]}" sh "$TEST" root "$PWFILE" "rm -f /root/s0.b64" --timeout 30 >/dev/null
while [ -n "$B64" ]; do
    "${V[@]}" sh "$TEST" root "$PWFILE" "printf %s '${B64:0:700}' >> /root/s0.b64" --timeout 30 >/dev/null
    B64="${B64:700}"
done
"${V[@]}" sh "$TEST" root "$PWFILE" "base64 -d /root/s0.b64 > /root/s0.sh; rm -f /root/s0.b64" --timeout 30 >/dev/null

# run it in the background: it restarts session 0, so the serial command must not be held open across it
"${V[@]}" sh "$TEST" root "$PWFILE" "rm -f /root/s0.out; (bash /root/s0.sh $GUEST_USER > /root/s0.out 2>&1 &)" --timeout 30 >/dev/null

# poll for the done line, at most 6 minutes
START=$(date +%s)
done_seen=0
while :; do
    sleep 10
    NOW=$(date +%s)
    if [ $((NOW - START)) -ge 360 ]; then break; fi
    COUNT=$("${V[@]}" sh "$TEST" root "$PWFILE" "grep -c SESSION0-GUEST-DONE /root/s0.out 2>/dev/null" --timeout 30 2>/dev/null) || true
    if [ "$COUNT" = 1 ]; then done_seen=1; break; fi
done

# the guest's result lines
GUEST_OUT=$("${V[@]}" sh "$TEST" root "$PWFILE" "cat /root/s0.out" --timeout 60 2>/dev/null) || true
printf '%s\n' "$GUEST_OUT"

"${V[@]}" shutdown "$TEST" --timeout 120 >/dev/null 2>&1 || true

# ---- the verdict ----

if [ "$done_seen" -ne 1 ]; then
    echo "FAIL: session 0 on the installed machine (the guest did not finish; no SESSION0-GUEST-DONE)"
    exit 1
fi
NOTOK=$(grep '^not ok ' <<<"$GUEST_OUT" || true)
if [ -n "$NOTOK" ]; then
    echo "FAIL: session 0 on the installed machine:"
    printf '%s\n' "$NOTOK"
    exit 1
fi
OKCOUNT=$(grep -c '^ok ' <<<"$GUEST_OUT" || true)
if [ "$OKCOUNT" -eq 0 ]; then
    echo "FAIL: session 0 on the installed machine (no checks reported)"
    exit 1
fi
echo "PASS: session 0 on the installed machine ($OKCOUNT checks)"
exit 0
