#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Group Policy end to end on an installed machine (docs/GROUP_POLICY.md).
#
# Boots a copy-on-write overlay of an installed session 0 disk (made by install-unattended.sh from an ISO
# packaged with WEX_SESSION0=1) with a second card on the lab network, and runs machine-guest.sh in it: the
# guest installs Samba, joins the lab domain of dc-create.sh, and checks that Windows programs are told the
# join and read the domain's registry policy. The controller has to be running, with the test object of
# gpo-create.sh. With the fixtures' domain user ($WEX_AD_LAB/wexuser1_password.txt, gpo-lab.sh fixtures) and an
# image with the admission step (docs/DOMAIN_LOGON.md, WO 3), WEXLAB\wexuser1 then logs on through the classic
# dialog, typed here, and the guest checks the session, the record, the token, the profile and the user policy (WO 4).
#
#   machine.sh SOURCE_VM [TEST_VM]        (default TEST_VM: SOURCE_VM-adtest)
#
# SOURCE_VM (under $VMTEST_DIR) is never written; TEST_VM is replaced by a new overlay, kept for inspection,
# with a random throwaway root password (TEST_VM/rootpw, mode 600, never printed). The lab domain's
# Administrator password is read from $WEX_AD_LAB/admin_password.txt (default /var/tmp/wex-ad-lab; when the
# controller runs in another WSL distribution, copy that one file over first).
#
# Requires root, QEMU/KVM, qemu-nbd, Tesseract and ImageMagick (scripts/vmtest.py). The VM ends stopped.
set -euo pipefail

usage="usage: machine.sh SOURCE_VM [TEST_VM]"
SOURCE=${1:?$usage}
TEST=${2:-${SOURCE}-adtest}
for name in "$SOURCE" "$TEST"; do
    [[ "$name" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid test VM name: $name"; exit 2; }
done
if [ "$SOURCE" = "$TEST" ]; then echo "TEST_VM must differ from SOURCE_VM"; exit 2; fi

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
LAB=${WEX_AD_LAB:-/var/tmp/wex-ad-lab}
export VMTEST_DIR=${VMTEST_DIR:-/tmp/vmtest}
export VMTEST_QEMU_EXTRA="-netdev socket,id=lab,mcast=230.0.0.71:17071 -device virtio-net-pci,netdev=lab,mac=52:54:00:71:00:30"
V=(python3 "$REPO/scripts/vmtest.py")
SRC_DISK=$VMTEST_DIR/$SOURCE/disk.qcow2
STATE=$VMTEST_DIR/$TEST
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
fail() { echo "FAIL: $*"; exit 1; }

[ -s "$LAB/admin_password.txt" ] || fail "no $LAB/admin_password.txt (the lab domain's Administrator password)"
[ -f "$SRC_DISK" ] || fail "no disk $SRC_DISK"
if running "$SRC_DISK"; then fail "$SOURCE is running; stop it first"; fi
if [ -e "$STATE/disk.qcow2" ]; then
    qemu-img info "$STATE/disk.qcow2" 2>/dev/null | grep -qx "backing file: $SRC_DISK" ||
        fail "$STATE/disk.qcow2 exists and is not an overlay of $SOURCE; choose another TEST_VM"
    "${V[@]}" stop "$TEST" >/dev/null 2>&1 || true
    for _ in $(seq 1 30); do running "$STATE/disk.qcow2" || break; sleep 1; done
fi
rm -rf -- "$STATE"
mkdir -p "$STATE"
qemu-img create -q -f qcow2 -b "$SRC_DISK" -F qcow2 "$STATE/disk.qcow2" || fail "cannot create the overlay"
head -c 64 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 16 > "$PWFILE"
chmod 600 "$PWFILE"

echo "=== $TEST: Group Policy from the lab domain on an overlay of $SOURCE"

# offline, on the overlay: the root password, the lab password and the guest script
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
[ -f "$MNT/usr/local/share/wineexplorer/session0.enable" ] || fail "the source was not installed from a session 0 image"
[ -f "$MNT/usr/local/bin/wex-gpupdate.sh" ] || fail "the source has no wex-gpupdate.sh (an image older than Group Policy step 4)"
printf 'root:%s\n' "$(cat "$PWFILE")" | chroot "$MNT" chpasswd || fail "cannot set the root password"
install -m 0600 "$LAB/admin_password.txt" "$MNT/root/adminpw"
# the domain user of the fixtures (gpo-lab.sh fixtures): the guest then has a domain user log on (WO 4)
DOMAIN_LOGON=0
if [ -s "$LAB/wexuser1_password.txt" ]; then
    install -m 0600 "$LAB/wexuser1_password.txt" "$MNT/root/wexuser1pw"
    DOMAIN_LOGON=1
fi
install -m 0700 "$HERE/machine-guest.sh" "$MNT/root/ad-guest.sh"
# WEX_LAB_TREE_GPUPDATE=1: this tree's wex-gpupdate.sh in place of the image's (a script change tested without a
# new image; docs/GPO_LAB.md)
if [ "${WEX_LAB_TREE_GPUPDATE:-0}" = 1 ]; then
    install -m 0755 "$REPO/installer/scripts/wex-gpupdate.sh" "$MNT/usr/local/bin/wex-gpupdate.sh"
    echo "using this tree's wex-gpupdate.sh"
fi
detach

"${V[@]}" start "$TEST" >/dev/null || fail "QEMU did not start"
"${V[@]}" wait "$TEST" "My Documents" --timeout 300 || fail "no desktop after boot"

"${V[@]}" sh "$TEST" root "$PWFILE" "rm -f /root/ad.out; (bash /root/ad-guest.sh > /root/ad.out 2>&1 &)" --timeout 30 >/dev/null

# The domain user's logon (docs/DOMAIN_LOGON.md, WO 4): once the guest says AD-GUEST-LOGON-READY, the Welcome
# screen is up without automatic logon; Ctrl+Alt+Delete twice gives the classic dialog (as installer/tests/safe-mode.sh),
# WEXLAB\wexuser1 is typed with the fixtures' password, and the guest is told once the desktop is there.
domain_logon() {
    "${V[@]}" wait "$TEST" "To begin" --timeout 120 >/dev/null || { echo "note domain_logon_host: no Welcome screen"; return 1; }
    "${V[@]}" key "$TEST" ctrl-alt-delete >/dev/null; sleep 2
    "${V[@]}" key "$TEST" ctrl-alt-delete >/dev/null
    "${V[@]}" wait "$TEST" "Log On to Windows" --timeout 30 >/dev/null || { echo "note domain_logon_host: no classic dialog"; return 1; }
    "${V[@]}" key "$TEST" alt-u >/dev/null
    "${V[@]}" type "$TEST" 'WEXLAB\wexuser1'
    "${V[@]}" key "$TEST" alt-p delete >/dev/null
    "${V[@]}" type "$TEST" "$(cat "$LAB/wexuser1_password.txt")"
    "${V[@]}" key "$TEST" ret >/dev/null
    "${V[@]}" wait "$TEST" "My Documents" --timeout 300 >/dev/null || { echo "note domain_logon_host: no desktop after the logon"; return 1; }
    "${V[@]}" sh "$TEST" root "$PWFILE" "touch /root/logon-done" --timeout 30 >/dev/null
}

# the guest installs packages: at most 15 minutes, 25 with the domain logon
START=$(date +%s)
LIMIT=$(( DOMAIN_LOGON ? 1500 : 900 ))
done_seen=0
logon_tried=0
while :; do
    sleep 15
    if [ $(( $(date +%s) - START )) -ge "$LIMIT" ]; then break; fi
    if [ "$DOMAIN_LOGON" = 1 ] && [ "$logon_tried" = 0 ]; then
        READY=$("${V[@]}" sh "$TEST" root "$PWFILE" "grep -c AD-GUEST-LOGON-READY /root/ad.out 2>/dev/null" --timeout 30 2>/dev/null) || true
        if [ "$READY" = 1 ]; then logon_tried=1; domain_logon || true; fi
    fi
    COUNT=$("${V[@]}" sh "$TEST" root "$PWFILE" "grep -c AD-GUEST-DONE /root/ad.out 2>/dev/null" --timeout 30 2>/dev/null) || true
    if [ "$COUNT" = 1 ]; then done_seen=1; break; fi
done
GUEST_OUT=$("${V[@]}" sh "$TEST" root "$PWFILE" "cat /root/ad.out" --timeout 60 2>/dev/null) || true
printf '%s\n' "$GUEST_OUT"
"${V[@]}" sh "$TEST" root "$PWFILE" "rm -f /root/adminpw" --timeout 30 >/dev/null 2>&1 || true
"${V[@]}" shutdown "$TEST" --timeout 120 >/dev/null 2>&1 || true

[ "$done_seen" -eq 1 ] || { echo "FAIL: Group Policy on the installed machine (the guest did not finish)"; exit 1; }
NOTOK=$(grep '^not ok ' <<<"$GUEST_OUT" || true)
if [ -n "$NOTOK" ]; then echo "FAIL: Group Policy on the installed machine:"; printf '%s\n' "$NOTOK"; exit 1; fi
OKCOUNT=$(grep -c '^ok ' <<<"$GUEST_OUT" || true)
[ "$OKCOUNT" -gt 0 ] || { echo "FAIL: Group Policy on the installed machine (no checks reported)"; exit 1; }
echo "PASS: Group Policy on the installed machine ($OKCOUNT checks)"
