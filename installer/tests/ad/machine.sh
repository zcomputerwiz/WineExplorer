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
# a member of Domain Admins (the fixtures' wexadmin1): the guest then also checks the token of a domain administrator
# at a real logon. Wine's whoami does not print the groups, so the project's token probe goes along (this host's
# mingw compiler builds it).
if [ -s "$LAB/wexadmin1_password.txt" ] && [ "$DOMAIN_LOGON" = 1 ] && command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    install -d -m 0755 "$MNT/usr/local/share/wex-lab-probe"
    x86_64-w64-mingw32-gcc -O2 -Wall -municode -o "$MNT/usr/local/share/wex-lab-probe/tierprobe64.exe" \
        "$REPO/scripts/system-tier-probe.c" -ladvapi32 -lntdll && install -m 0600 "$LAB/wexadmin1_password.txt" "$MNT/root/wexadmin1pw" ||
        echo "note domain_admin: not prepared (probe or password)"
fi
install -m 0700 "$HERE/machine-guest.sh" "$MNT/root/ad-guest.sh"
# WEX_LAB_TREE_GPUPDATE=1: this tree's wex-gpupdate.sh in place of the image's (a script change tested without a
# new image; docs/GPO_LAB.md)
if [ "${WEX_LAB_TREE_GPUPDATE:-0}" = 1 ]; then
    install -m 0755 "$REPO/installer/scripts/wex-gpupdate.sh" "$MNT/usr/local/bin/wex-gpupdate.sh"
    echo "using this tree's wex-gpupdate.sh"
fi
# WEX_LAB_TREE_TOOLS=1: this tree's wex-sysrun and wex-accounts (built as installer/package-wine.sh builds them) and
# the PAM step's script and the session scripts in place of the image's, so that a change to any is tested without a
# new image
if [ "${WEX_LAB_TREE_TOOLS:-0}" = 1 ]; then
    TOOLS=$(mktemp -d)
    cc -O2 -Wall -fPIE -pie -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wl,-z,relro,-z,now \
        -o "$TOOLS/wex-sysrun" "$REPO/tools/wex-sysrun/wex-sysrun.c" || fail "cannot build wex-sysrun"
    cc -O2 -Wall -Wextra -fPIE -pie -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wl,-z,relro,-z,now \
        -o "$TOOLS/wex-accounts" "$REPO/tools/wex-accounts/wex-accounts.c" || fail "cannot build wex-accounts"
    LIBEXEC="$MNT/usr/local/libexec/wineexplorer"
    [ -d "$LIBEXEC" ] || fail "the image has no $LIBEXEC"
    install -m 0755 "$TOOLS/wex-accounts" "$LIBEXEC/wex-accounts"
    install -m 0755 "$TOOLS/wex-sysrun" "$LIBEXEC/wex-sysrun"
    chroot "$MNT" chown root:wexsys /usr/local/libexec/wineexplorer/wex-sysrun || fail "chown wex-sysrun"
    chmod 2755 "$LIBEXEC/wex-sysrun"
    install -m 0755 "$REPO/scripts/wex-domain-admit-pam.sh" "$MNT/usr/local/sbin/wex-domain-admit-pam.sh"
    # the session scripts (the refresh after the shell is up is theirs)
    for s in wine-explorer-session.sh wine-explorer-session-common.sh wine-explorer-wayland-session.sh; do
        install -m 0755 "$REPO/scripts/$s" "$MNT/usr/local/bin/$s"
    done
    rm -rf "$TOOLS"
    echo "using this tree's wex-sysrun, wex-accounts, wex-domain-admit-pam.sh and session scripts"
fi
detach

"${V[@]}" start "$TEST" >/dev/null || fail "QEMU did not start"
"${V[@]}" wait "$TEST" "My Documents" --timeout 300 || fail "no desktop after boot"

"${V[@]}" sh "$TEST" root "$PWFILE" "rm -f /root/ad.out; (bash /root/ad-guest.sh > /root/ad.out 2>&1 &)" --timeout 30 >/dev/null

# Host steps (docs/DOMAIN_LOGON.md, WO 4; docs/GPO_LAB.md): the guest asks for what only the host can do, with a line
# "AD-GUEST-HOST-STEP NAME" in its output, and waits for /root/host-done-NAME, which gets "ok" or "fail: why":
#   logon      the classic dialog (Ctrl+Alt+Delete twice, as installer/tests/safe-mode.sh), WEXLAB\wexuser1 and the
#              fixtures' password typed, the desktop waited for
#   logon-bad  the same with a wrong password: the Logon Message has to come, and the dialog stays
#   tile TEXT  the Welcome screen shows TEXT (the user's tile)
#   consent    the User Account Control dialog is shown and answered with Continue (installer/tests/elevation.sh)
# A screenshot of where a step stopped is kept in the test VM's folder.
host_stuck() { "${V[@]}" shot "$TEST" "$STATE/host-$1.png" >/dev/null 2>&1 || true; echo "fail: $2 (screen $STATE/host-$1.png)"; }
host_classic() {   # to the classic dialog from the Welcome screen (or already there, after a refused logon)
    if "${V[@]}" wait "$TEST" "Log On to Windows" --timeout 5 >/dev/null 2>&1; then return 0; fi
    "${V[@]}" wait "$TEST" "To begin" --timeout 300 >/dev/null || { host_stuck welcome "no Welcome screen"; return 1; }
    "${V[@]}" key "$TEST" ctrl-alt-delete >/dev/null; sleep 2
    "${V[@]}" key "$TEST" ctrl-alt-delete >/dev/null
    "${V[@]}" wait "$TEST" "Log On to Windows" --timeout 30 >/dev/null || { host_stuck classic "no classic dialog"; return 1; }
}
host_type_credentials() {   # NAME PASSWORD
    "${V[@]}" key "$TEST" alt-u >/dev/null
    "${V[@]}" type "$TEST" "$1"
    "${V[@]}" key "$TEST" alt-p delete >/dev/null
    "${V[@]}" type "$TEST" "$2"
    "${V[@]}" key "$TEST" ret >/dev/null
}
host_step() {   # NAME ARGS...: prints what goes into the done file (the last line)
    local name=$1; shift
    case $name in
        logon)   # logon [DOMAIN\user]: wexuser1 when no name is given; the password is the fixtures' $LAB/<user>_password.txt
            local who=${1:-'WEXLAB\wexuser1'}
            [[ $who =~ ^WEXLAB\\[a-z0-9]+$ ]] || { echo "fail: not a lab user: $who"; return 0; }
            host_classic || return 0
            host_type_credentials "$who" "$(cat "$LAB/${who#WEXLAB\\}_password.txt")"
            "${V[@]}" wait "$TEST" "My Documents" --timeout 300 >/dev/null || { host_stuck desktop "no desktop after the logon"; return 0; }
            echo ok ;;
        logon-bad)
            host_classic || return 0
            host_type_credentials 'WEXLAB\wexuser1' "not-the-password-$RANDOM"
            "${V[@]}" wait "$TEST" "could not log you on" --timeout 120 >/dev/null || { host_stuck refused "no Logon Message for a wrong password"; return 0; }
            "${V[@]}" key "$TEST" ret >/dev/null
            "${V[@]}" wait "$TEST" "Log On to Windows" --timeout 30 >/dev/null || { host_stuck afterrefusal "the classic dialog closed after the Logon Message"; return 0; }
            echo ok ;;
        consent)   # the User Account Control dialog of an elevation request: Continue
            "${V[@]}" wait "$TEST" "Account Control" --timeout 90 >/dev/null || { host_stuck consent "no User Account Control dialog"; return 0; }
            "${V[@]}" shot "$TEST" "$STATE/host-consent.png" >/dev/null 2>&1 || true
            if "${V[@]}" clicktext "$TEST" Continue --timeout 20 >/dev/null; then echo ok; else host_stuck consent "no Continue button"; fi ;;
        tile)
            "${V[@]}" wait "$TEST" "To begin" --timeout 300 >/dev/null || { host_stuck welcome "no Welcome screen"; return 0; }
            "${V[@]}" shot "$TEST" "$STATE/host-tile.png" >/dev/null 2>&1 || true
            if "${V[@]}" wait "$TEST" "$*" --timeout 60 >/dev/null; then echo ok; else host_stuck tile "no tile with the text '$*'"; fi ;;
        *) echo "fail: unknown host step $name" ;;
    esac
}

# the guest installs packages and runs the logons: at most 15 minutes, 45 with the domain user's logons
START=$(date +%s)
LIMIT=$(( DOMAIN_LOGON ? 2700 : 900 ))
done_seen=0
handled=0
while :; do
    sleep 15
    if [ $(( $(date +%s) - START )) -ge "$LIMIT" ]; then break; fi
    if [ "$DOMAIN_LOGON" = 1 ]; then
        STEPS=$("${V[@]}" sh "$TEST" root "$PWFILE" "grep '^AD-GUEST-HOST-STEP ' /root/ad.out 2>/dev/null" --timeout 30 2>/dev/null) || STEPS=
        count=$(grep -c . <<<"$STEPS" || true)
        while [ "$handled" -lt "$count" ]; do
            handled=$((handled + 1))
            line=$(sed -n "${handled}p" <<<"$STEPS" | tr -d '\r')
            read -r -a words <<<"${line#AD-GUEST-HOST-STEP }"
            result=$(host_step "${words[@]}" 2>&1 | tail -1)
            echo "note host_step ${words[*]}: ${result:-no result}"
            "${V[@]}" sh "$TEST" root "$PWFILE" "printf '%s\n' '${result:-fail: no result}' > /root/host-done-${words[0]}" --timeout 30 >/dev/null
        done
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
