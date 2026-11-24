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
#
# WEX_AD_BASELINE (docs/GPO_LAB.md, "The lab's base disk"), one provisioning for many guests:
#   make   TEST_VM becomes the base: base-guest.sh installs the guests' packages and the PowerShell archives, the
#          machine shuts down, and its disk is made read-only with a record of the inputs (SOURCE_VM's disk,
#          base-guest.sh, join-ps-guest.sh's release lines). No guest, no admin password needed.
#   use    SOURCE_VM is such a base: refused when its inputs changed (make it again). The guest then finds its
#          packages and archives present and skips the downloads.
#   check  only says (exit status) whether TEST_VM is a current base of SOURCE_VM; nothing is started or changed.
# gpo-lab.sh machines runs the guests this way, each on its own overlay of the base.
#
# The desktop is waited for by its windows (installer/tests/session-run.py over the serial console, as the chain does:
# docs/TOOLING.md, "the chain's rework"), with OCR of "My Documents" when the console or the probe gives no answer.
set -euo pipefail

usage="usage: machine.sh SOURCE_VM [TEST_VM]"
SOURCE=${1:?$usage}
TEST=${2:-${SOURCE}-adtest}
for name in "$SOURCE" "$TEST"; do
    [[ "$name" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid test VM name: $name"; exit 2; }
done
if [ "$SOURCE" = "$TEST" ]; then echo "TEST_VM must differ from SOURCE_VM"; exit 2; fi
BASELINE=${WEX_AD_BASELINE:-}
case "$BASELINE" in ''|make|use|check) ;; *) echo "Invalid WEX_AD_BASELINE: $BASELINE"; exit 2 ;; esac

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
LAB=${WEX_AD_LAB:-/var/tmp/wex-ad-lab}
export VMTEST_DIR=${VMTEST_DIR:-/tmp/vmtest}
export VMTEST_QEMU_EXTRA="-netdev socket,id=lab,mcast=230.0.0.71:17071 -device virtio-net-pci,netdev=lab,mac=52:54:00:71:00:30"
V=(python3 "$REPO/scripts/vmtest.py")
HELPER=/usr/local/libexec/wex-tests/session-run.py
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

[ -f "$SRC_DISK" ] || fail "no disk $SRC_DISK"
inputs_stamp() {   # inputs_stamp INSTALLED_DISK: what the base depends on, as one checksum
    {
        sha256sum < "$HERE/base-guest.sh"
        grep -E '^PS(32)?_(URL|SHA)=' "$HERE/join-ps-guest.sh"
        stat -L -c '%s %Y %i' "$1"
    } | sha256sum | cut -c1-64
}
if [ "$BASELINE" = check ]; then   # is TEST_VM a current base of SOURCE_VM? (gpo-lab.sh machines asks)
    trap - EXIT
    [ -f "$STATE/baseline.stamp" ] && [ "$(cat "$STATE/baseline.source" 2>/dev/null)" = "$SRC_DISK" ] &&
        [ "$(stat -c %a "$STATE/disk.qcow2" 2>/dev/null)" = 444 ] &&
        [ "$(inputs_stamp "$SRC_DISK")" = "$(cat "$STATE/baseline.stamp")" ]
    exit $?
fi
[ "$BASELINE" = make ] || [ -s "$LAB/admin_password.txt" ] ||
    fail "no $LAB/admin_password.txt (the lab domain's Administrator password)"
if running "$SRC_DISK"; then fail "$SOURCE is running; stop it first"; fi
if [ -e "$STATE/disk.qcow2" ]; then
    qemu-img info "$STATE/disk.qcow2" 2>/dev/null | grep -qx "backing file: $SRC_DISK" ||
        fail "$STATE/disk.qcow2 exists and is not an overlay of $SOURCE; choose another TEST_VM"
    "${V[@]}" stop "$TEST" >/dev/null 2>&1 || true
    for _ in $(seq 1 30); do running "$STATE/disk.qcow2" || break; sleep 1; done
fi
if [ "$BASELINE" = use ]; then
    [ -f "$VMTEST_DIR/$SOURCE/baseline.stamp" ] && [ "$(stat -c %a "$SRC_DISK")" = 444 ] ||
        fail "$SOURCE is not a base disk (WEX_AD_BASELINE=make)"
    [ "$(inputs_stamp "$(cat "$VMTEST_DIR/$SOURCE/baseline.source")")" = "$(cat "$VMTEST_DIR/$SOURCE/baseline.stamp")" ] ||
        fail "the base $SOURCE was made from other inputs (a new install or a changed base-guest.sh); make it again"
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
# the window probe of the desktop (as the chain installs it)
install -D -m 0755 "$REPO/installer/tests/session-run.py" "$MNT$HELPER" || fail "cannot install the window probe"

gsh() { "${V[@]}" sh "$TEST" root "$PWFILE" "$1" --timeout "$2"; }   # gsh COMMAND TIMEOUT: as root, serial console
desktop() {   # desktop TIMEOUT [USER]: USER's Explorer desktop (default the installed user) by its windows, else OCR
    local user=${2:-wineuser} end=$((SECONDS + $1)) left out
    # short calls first: a long one sent while the machine starts would wait behind the getty (docs/TOOLING.md)
    while [ "$SECONDS" -lt "$end" ]; do
        gsh 'echo serial=up' 20 2>/dev/null | grep -q '^serial=up' && break
        if "${V[@]}" wait "$TEST" "My Documents" --timeout 5 >/dev/null 2>&1; then
            echo "note desktop: found by OCR (the serial console gave no prompt)"; return 0
        fi
        sleep 5
    done
    while left=$((end - SECONDS)); [ "$left" -gt 0 ]; do
        out=$(gsh "python3 $HELPER --user '$user' --wait-desktop --timeout $left" $((left + 40)) 2>/dev/null) || true
        case "$out" in
            *desktop=ready*) echo "note desktop: $(grep -m1 -o 'desktop=ready.*' <<< "$out" | tr -d '\r') ($user)"; return 0 ;;
            *probe=unavailable*)
                "${V[@]}" wait "$TEST" "My Documents" --timeout "$((end - SECONDS))" >/dev/null 2>&1 && return 0
                return 1 ;;
            *desktop=none*) printf '%s\n' "$out" | tr -d '\r' | tail -8 | sed 's/^/  | /'; return 1 ;;
        esac
        if "${V[@]}" wait "$TEST" "My Documents" --timeout 5 >/dev/null 2>&1; then
            echo "note desktop: found by OCR (no answer from the window probe)"; return 0
        fi
        sleep 5
    done
    return 1
}

if [ "$BASELINE" = make ]; then
    # the base: the guests' packages and archives, then read-only (see the top)
    install -m 0700 "$HERE/base-guest.sh" "$MNT/root/ad-base.sh"
    install -m 0600 "$HERE/join-ps-guest.sh" "$MNT/root/ad-base-ps.sh"
    detach
    echo "=== $TEST: the lab's base disk, from $SOURCE"
    T0=$SECONDS
    "${V[@]}" start "$TEST" >/dev/null || fail "QEMU did not start"
    desktop 300 || fail "no desktop after boot"
    echo "note boot: $((SECONDS - T0)) s"
    OUT=$(gsh "bash /root/ad-base.sh 2>&1; rm -f /root/ad-base.sh" 1500 2>/dev/null) || true
    printf '%s\n' "$OUT" | tr -d '\r' | grep -E '^(ok|not ok|note) |AD-BASE-DONE'
    "${V[@]}" shutdown "$TEST" --timeout 120 >/dev/null 2>&1 || true
    for _ in $(seq 1 60); do running "$STATE/disk.qcow2" || break; sleep 1; done
    running "$STATE/disk.qcow2" && fail "the base did not shut down"
    grep -q 'AD-BASE-DONE' <<< "$OUT" || fail "the base's provisioning did not finish"
    ! grep -q '^not ok packages_installed' <<< "$OUT" || fail "the base's packages did not install"
    # (an archive that did not download is fetched by join-ps-guest.sh itself, as before)
    inputs_stamp "$SRC_DISK" > "$STATE/baseline.stamp" && echo "$SRC_DISK" > "$STATE/baseline.source" &&
        chmod 444 "$STATE/disk.qcow2" || fail "cannot record the base"
    trap - EXIT
    echo "PASS: base disk $TEST of $SOURCE ($((SECONDS - T0)) s); guests use it with WEX_AD_BASELINE=use"
    exit 0
fi
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
# WEX_GUEST=NAME.sh: another guest script of this folder (join-guest.sh: the join from netdom, docs/DOMAIN_JOIN.md)
GUEST=${WEX_GUEST:-machine-guest.sh}
[ -f "$HERE/$GUEST" ] || fail "no guest script $HERE/$GUEST"
install -m 0700 "$HERE/$GUEST" "$MNT/root/ad-guest.sh"
# WEX_LAB_EXTRA=DIR: a folder copied into the guest as /root/extra (files a guest script needs: dlls of a build under
# test, a module; join-ps-guest.sh)
if [ -n "${WEX_LAB_EXTRA:-}" ]; then
    [ -d "$WEX_LAB_EXTRA" ] || fail "no folder $WEX_LAB_EXTRA"
    rm -rf "$MNT/root/extra"
    cp -a "$WEX_LAB_EXTRA" "$MNT/root/extra" || fail "cannot copy $WEX_LAB_EXTRA"
    echo "extra files from $WEX_LAB_EXTRA in /root/extra"
fi
# WEX_LAB_TREE_GPUPDATE=1: this tree's wex-gpupdate.sh in place of the image's (a script change tested without a
# new image; docs/GPO_LAB.md)
if [ "${WEX_LAB_TREE_GPUPDATE:-0}" = 1 ]; then
    install -m 0755 "$REPO/installer/scripts/wex-gpupdate.sh" "$MNT/usr/local/bin/wex-gpupdate.sh"
    echo "using this tree's wex-gpupdate.sh"
fi
# WEX_LAB_TREE_JOIN=1: this tree's join engine (wex-domain-join.sh) in place of the image's
if [ "${WEX_LAB_TREE_JOIN:-0}" = 1 ]; then
    install -m 0755 "$REPO/installer/scripts/wex-domain-join.sh" "$MNT/usr/local/sbin/wex-domain-join.sh"
    install -m 0755 "$REPO/installer/scripts/wex-domain-state.sh" "$MNT/usr/local/bin/wex-domain-state.sh"
    echo "using this tree's wex-domain-join.sh and wex-domain-state.sh"
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

T0=$SECONDS
"${V[@]}" start "$TEST" >/dev/null || fail "QEMU did not start"
desktop 300 || fail "no desktop after boot"
echo "note boot: $((SECONDS - T0)) s"

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
            # (how the desktop was found goes to the run's log at the end: the step's own output is its answer)
            local how
            how=$(desktop 300 "$who") || { printf '%s\n' "$how" >> "$STATE/host-notes.txt"; host_stuck desktop "no desktop after the logon"; return 0; }
            printf '%s\n' "$how" >> "$STATE/host-notes.txt"
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
            # (the pointer of an earlier click may sit on the button and hide its text from the reader: move it off)
            "${V[@]}" click "$TEST" 640 300 >/dev/null 2>&1 || true
            if "${V[@]}" clicktext "$TEST" Continue --timeout 20 >/dev/null; then echo ok; else host_stuck consent "no Continue button"; fi ;;
        tile)
            "${V[@]}" wait "$TEST" "To begin" --timeout 300 >/dev/null || { host_stuck welcome "no Welcome screen"; return 0; }
            "${V[@]}" shot "$TEST" "$STATE/host-tile.png" >/dev/null 2>&1 || true
            if "${V[@]}" wait "$TEST" "$*" --timeout 60 >/dev/null; then echo ok; else host_stuck tile "no tile with the text '$*'"; fi ;;
        shot)   # shot LABEL: the screen as it is, kept as $STATE/shot-LABEL.png
            [[ ${1:-} =~ ^[a-z0-9-]+$ ]] || { echo "fail: bad label"; return 0; }
            "${V[@]}" shot "$TEST" "$STATE/shot-$1.png" >/dev/null 2>&1 && echo ok || echo "fail: no screenshot" ;;
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
[ ! -s "$STATE/host-notes.txt" ] || cat "$STATE/host-notes.txt"   # the host steps' desktops (window probe or OCR)
"${V[@]}" sh "$TEST" root "$PWFILE" "rm -f /root/adminpw" --timeout 30 >/dev/null 2>&1 || true
"${V[@]}" shutdown "$TEST" --timeout 120 >/dev/null 2>&1 || true

MIN="$(( (SECONDS - T0 + 30) / 60 )) min"
OKCOUNT=$(grep -c '^ok ' <<<"$GUEST_OUT" || true)
[ "$done_seen" -eq 1 ] || { echo "FAIL: Group Policy on the installed machine (the guest did not finish; $OKCOUNT checks passed, $MIN)"; exit 1; }
NOTOK=$(grep '^not ok ' <<<"$GUEST_OUT" || true)
if [ -n "$NOTOK" ]; then
    echo "FAIL: Group Policy on the installed machine ($(grep -c . <<<"$NOTOK") failed, $OKCOUNT passed, $MIN):"
    printf '%s\n' "$NOTOK"; exit 1
fi
[ "$OKCOUNT" -gt 0 ] || { echo "FAIL: Group Policy on the installed machine (no checks reported)"; exit 1; }
echo "PASS: Group Policy on the installed machine ($OKCOUNT checks, $MIN)"
