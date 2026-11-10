#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Safe Mode and the Advanced Options menu on an installed disk (docs/SAFE_MODE.md, docs/SAFE_MODE_WORKORDER.md A1).
#
# Boots a copy-on-write overlay of an installed test disk once per phase, pressing F8 during GRUB's hidden timeout,
# and checks each start:
#   safe         Safe Mode: the OS list shows the mode, the Welcome screen instead of automatic logon, only the
#                Safe Mode units run, the Explorer session has WEXMODE=safe, Wine's 32-bit and 64-bit Notepad open
#   safenet      Safe Mode with Networking: the Welcome screen, NetworkManager runs, no timers, WEXMODE=safenet
#   safecmd      Safe Mode with Command Prompt: logging on starts the recovery console and no wineserver
#   nowine       Safe Mode with Command Prompt with Wine missing: the GTK greeter takes over, and logging on still
#                starts the recovery console (the test hides /usr/local/bin/wine for this boot only)
#   bootlog      Enable Boot Logging: a normal start with automatic logon, and ntbtlog.txt gains driver lines
#   startnormal  Start WineExplorer Normally: a normal start with automatic logon, timers and networking; it does
#                not count, so GRUB's wex_boot_pending is back at 0
#   normal       no F8: automatic logon, the boot-mode generator wrote nothing, and the start-up check cleared
#                wex_boot_pending
#   failed       "did not start successfully": with the logon screen held back 90 seconds for this phase, the Reset
#                button once Linux runs (GRUB has recorded the start), then the failure screen with its countdown;
#                an arrow key stops it, and Safe Mode with Command Prompt from there does not count (the screen comes
#                back at the next start); left alone, the countdown starts normally, which counts
#   accounts     the Administrator account and hidden accounts (work order C4, C7; needs this branch's Wine, so
#                WEX_TEST_WINE): the Safe Mode Welcome screen shows Administrator and not the test
#                account hidden through SpecialAccounts\UserList, and Administrator logs on; then, in a normal start,
#                the logon screen shows neither
# Provisioning also gives the Administrator account a random test password ($OUT/admin-password) and adds the
# account wexhidden ("Hidden Test", no password) with a value of 0 under SpecialAccounts\UserList in the logon
# prefix's registry.
# Before and after every boot it records checksums of every file and link under /etc and /var/lib/AccountsService
# and of each account's .dmrc. A boot that changes any of them fails. Every boot also reports the guest's disk use:
# a warning from 80%, a failure from 95%. Before each phase the overlay's wex_boot_pending is cleared, so a phase
# that failed does not leave the next one on the failure screen.
#
#   safe-mode.sh SOURCE_VM TEST_VM [PHASE...]          (default: all phases, in the order above)
#
# SOURCE_VM is an installed test disk under $VMTEST_DIR, for example from install-unattended.sh, and is never
# written. TEST_VM is replaced by a new overlay of it, which is kept for inspection; an existing TEST_VM disk that
# is not an overlay of SOURCE_VM is refused.
#   WEX_TEST_FIRMWARE=uefi        boot with OVMF (default bios, SeaBIOS): the firmware SOURCE_VM was installed with
#   WEX_TEST_LOADER=vintner       SOURCE_VM starts through Vintner Boot (an ISO built with --vintner, so also
#                                 WEX_SAFEMODE_FILES=installed): the menu is reached through the Advanced Options
#                                 variant (installer/vintner/advanced.conf) in Vintner's menu, Shift held on BIOS and
#                                 Space pressed on UEFI, instead of F8; each start reports its variant on the serial
#                                 console. GRUB's start-up record does not exist there, so the failed phase and the
#                                 wex_boot_pending checks are left out. Default grub.
#   WEX_SAFEMODE_FILES            how the overlay gets this branch's Safe Mode; every mode then runs update-grub and
#                                 sets up the test accounts in one provisioning boot:
#                   branch        (default) copies this checkout's Safe Mode files onto the overlay where the package
#                                 installs them, and runs the package postinst's Safe Mode part in the guest
#                   installed     SOURCE_VM already has this branch's packages (an ISO install)
#                   upgrade       SOURCE_VM was installed from an ISO with the Safe Mode before docs/SAFE_MODE.md (built
#                                 before commit 14286423). The provisioning boot starts that Safe Mode with Command
#                                 Prompt (grubenv next_entry) and, while it runs, installs the packages in
#                                 WEX_UPGRADE_DEBS with dpkg; then the test checks that nothing of the old Safe Mode
#                                 is left (docs/SAFE_MODE_WORKORDER.md A2). It first adds the locked standard account
#                                 wexstd ("Standard Test") with no AccountsService file, which the old Safe Mode hides.
#   WEX_UPGRADE_DEBS              with upgrade: a directory with this branch's wineexplorer, wineexplorer-session and
#                                 wexcomp packages (installer/package-wine.sh, installer/package-wexcomp.sh)
#   WEX_TEST_WINE                 with branch: a .tar.gz of this branch's Wine (make install prefix=/usr/local into
#                                 a staging directory, packed from there), unpacked over / in the provisioning boot
#                                 before the Safe Mode files are set up. It tests Wine-side changes (logonui.exe,
#                                 wexhost) without packaging, which downloads third-party assets. With installed and
#                                 upgrade the packages carry this branch's Wine.
#   WEX_TEST_PROBES               a directory with safeboot-probe32.exe and safeboot-probe64.exe (installer/tests/
#                                 safeboot-probe.c, built with MinGW). Then safe, safenet and normal also check what
#                                 Windows programs see (work order B1-B3): SM_CLEANBOOT, SafeBoot\Option, which
#                                 services run, and that an unlisted service is refused. Needs a Wine with phase B.
#   WEX_TEST_SAFE_BOX=required    a Safe Mode logon must show the message box (work order C2); by default (optional)
#                                 the box is answered when it appears, for images whose Explorer has none
#   WEX_TEST_USER                 the account that logs on (default wineuser); it also logs on on the serial console
#   WEX_TEST_PASSWORD_FILE        its password (default: empty)
#   WEX_TEST_TILE                 its name on the Welcome screen (default: WEX_TEST_USER), clicked; Enter (the first
#                                 tile) when OCR cannot find it
# Requires root, QEMU/KVM, qemu-nbd, Tesseract and ImageMagick (scripts/vmtest.py). Every VM ends stopped.
# Screenshots, logs and the configuration records go to $VMTEST_DIR/TEST_VM/safe-mode/.
set -uo pipefail

usage="usage: safe-mode.sh SOURCE_VM TEST_VM [PHASE...]"
SOURCE=${1:?$usage}
TEST=${2:?$usage}
shift 2
PHASES=("$@")
[ ${#PHASES[@]} -gt 0 ] || PHASES=(safe safenet safecmd nowine bootlog startnormal normal failed)
for name in "$SOURCE" "$TEST"; do
    [[ "$name" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid test VM name: $name"; exit 2; }
done
[ "$SOURCE" != "$TEST" ] || { echo "TEST_VM must differ from SOURCE_VM"; exit 2; }
for phase in "${PHASES[@]}"; do
    case "$phase" in
        safe|safenet|safecmd|nowine|bootlog|startnormal|normal|failed|accounts) ;;
        *) echo "Unknown phase: $phase"; exit 2 ;;
    esac
done
FIRMWARE=${WEX_TEST_FIRMWARE:-bios}
case "$FIRMWARE" in
    bios) START_ARGS=() ;;
    uefi) START_ARGS=(--efi) ;;
    *) echo "Invalid firmware: $FIRMWARE"; exit 2 ;;
esac
FILES=${WEX_SAFEMODE_FILES:-branch}
case "$FILES" in branch|installed|upgrade) ;; *) echo "Invalid WEX_SAFEMODE_FILES: $FILES"; exit 2 ;; esac
LOADER=${WEX_TEST_LOADER:-grub}
case "$LOADER" in
    grub) ;;
    vintner)
        [ "$FILES" = installed ] || { echo "WEX_TEST_LOADER=vintner needs WEX_SAFEMODE_FILES=installed"; exit 2; }
        if [ $# -eq 0 ]; then
            PHASES=(safe safenet safecmd nowine bootlog startnormal normal)
        else
            case " ${PHASES[*]} " in *" failed "*) echo "The failed phase is GRUB's (WEX_TEST_LOADER=grub)"; exit 2 ;; esac
        fi
        ;;
    *) echo "Invalid WEX_TEST_LOADER: $LOADER"; exit 2 ;;
esac
WINE_TAR=${WEX_TEST_WINE:-}
if [ -n "$WINE_TAR" ]; then
    [ "$FILES" = branch ] || { echo "WEX_TEST_WINE needs WEX_SAFEMODE_FILES=branch"; exit 2; }
    [ -f "$WINE_TAR" ] || { echo "No Wine tarball: $WINE_TAR"; exit 2; }
fi
SAFE_BOX=${WEX_TEST_SAFE_BOX:-optional}
case "$SAFE_BOX" in required|optional) ;; *) echo "Invalid WEX_TEST_SAFE_BOX: $SAFE_BOX"; exit 2 ;; esac
PROBE_DIR=${WEX_TEST_PROBES:-}
if [ -n "$PROBE_DIR" ]; then
    for bits in 32 64; do
        [ -f "$PROBE_DIR/safeboot-probe$bits.exe" ] || { echo "No $PROBE_DIR/safeboot-probe$bits.exe"; exit 2; }
    done
fi
UPGRADE_DEBS=${WEX_UPGRADE_DEBS:-}
if [ "$FILES" = upgrade ]; then
    for pkg in wineexplorer_ wineexplorer-session_ wexcomp_; do
        compgen -G "$UPGRADE_DEBS/$pkg*.deb" >/dev/null \
            || { echo "WEX_UPGRADE_DEBS has no $pkg*.deb: ${UPGRADE_DEBS:-unset}"; exit 2; }
    done
fi
case " ${PHASES[*]} " in
    *" accounts "*)
        [ "$FILES" != branch ] || [ -n "$WINE_TAR" ] \
            || { echo "The accounts phase needs this branch's Wine: WEX_TEST_WINE"; exit 2; } ;;
esac

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
export VMTEST_DIR=${VMTEST_DIR:-/tmp/vmtest}
V=(python3 "$REPO/scripts/vmtest.py")
SRC_DISK=$VMTEST_DIR/$SOURCE/disk.qcow2
STATE=$VMTEST_DIR/$TEST
OUT=$STATE/safe-mode
MNT=$STATE/root
USER_NAME=${WEX_TEST_USER:-wineuser}
HELPER=/usr/local/libexec/wex-tests/session-run.py
PROBES=/usr/local/libexec/wex-tests
NTBTLOG=/var/lib/wineexplorer/machine/drive_c/windows/ntbtlog.txt
NBD=

# Where the wineexplorer-session package installs the Safe Mode files (installer/package-wine.sh; the unit test
# scripts/test_safemode.py checks that this list matches the package). Mode, source, destination.
SAFE_MODE_FILES=(
    "755 scripts/wex-bootmode.sh usr/local/bin/wex-bootmode"
    "755 scripts/wex-safe-terminal.sh usr/local/bin/wex-safe-terminal.sh"
    "755 scripts/wex-greeter.sh usr/local/bin/wex-greeter.sh"
    "755 scripts/wine-explorer-session.sh usr/local/bin/wine-explorer-session.sh"
    "755 scripts/wine-explorer-session-common.sh usr/local/bin/wine-explorer-session-common.sh"
    "755 scripts/wex-safemode-pam-check.sh usr/local/sbin/wex-safemode-pam-check.sh"
    "755 scripts/wex-advanced-options.py usr/local/sbin/wex-advanced-options"
    "755 scripts/wex-bootlog.sh usr/local/sbin/wex-bootlog"
    "755 scripts/wex-startup-check.sh usr/local/sbin/wex-startup-check"
    "755 installer/systemd/wex-bootmode-generator usr/lib/systemd/system-generators/wex-bootmode-generator"
    "644 installer/systemd/wex-advanced-options.target lib/systemd/system/wex-advanced-options.target"
    "644 installer/systemd/wex-advanced-options.service lib/systemd/system/wex-advanced-options.service"
    "644 installer/systemd/wex-safemode.target lib/systemd/system/wex-safemode.target"
    "644 installer/systemd/wex-safenet.target lib/systemd/system/wex-safenet.target"
    "644 installer/systemd/wex-bootlog.service lib/systemd/system/wex-bootlog.service"
    "644 installer/systemd/wex-startup-check.service lib/systemd/system/wex-startup-check.service"
    "644 installer/systemd/wex-bootmode-record.service lib/systemd/system/wex-bootmode-record.service"
    "644 installer/systemd/wexprefetchd.tmpfiles usr/lib/tmpfiles.d/wexprefetchd.conf"
    "644 installer/systemd/wexprefetchd.service lib/systemd/system/wexprefetchd.service"
    "755 installer/live-build/config/includes.chroot/etc/grub.d/11_wineexplorer_advanced etc/grub.d/11_wineexplorer_advanced"
)
# What the implementation before docs/SAFE_MODE.md installed; dpkg removes these when the package is upgraded.
OLD_FILES=(etc/grub.d/11_wineexplorer_safemode usr/local/sbin/wex-safemode-setup.sh
           lib/systemd/system/wex-safemode.service usr/share/xsessions/wex-safe-terminal.desktop)

die() { echo "FAIL: $*"; exit 1; }
running() { pgrep -f "file=$1," >/dev/null; }
wait_off() {
    local _
    for _ in $(seq 1 "$1"); do
        running "$STATE/disk.qcow2" || return 0
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
    [ -n "$NBD" ] || { echo "no unused NBD device"; return 1; }
    if [ "$1" = ro ]; then
        qemu-nbd --read-only --connect "$NBD" "$STATE/disk.qcow2" || { NBD=; return 1; }
    else
        qemu-nbd --connect "$NBD" "$STATE/disk.qcow2" || { NBD=; return 1; }
    fi
    udevadm settle 2>/dev/null || true
    for _ in $(seq 1 30); do
        for part in "$NBD"p*; do
            [ -b "$part" ] && [ "$(blkid -o value -s TYPE "$part" 2>/dev/null)" = ext4 ] && rootdev=$part
        done
        [ -n "$rootdev" ] && break
        sleep 1
    done
    [ -n "$rootdev" ] || { echo "no ext4 root file system on $NBD"; detach; return 1; }
    mkdir -p "$MNT"
    if [ "$1" = ro ]; then
        mount -o ro,noload "$rootdev" "$MNT" || { detach; return 1; }
    else
        mount "$rootdev" "$MNT" || { detach; return 1; }
    fi
}

detach() {
    if mountpoint -q "$MNT"; then umount "$MNT"; fi
    if [ -n "$NBD" ]; then qemu-nbd --disconnect "$NBD" >/dev/null 2>&1; fi
    NBD=
}

cleanup() {
    detach
    "${V[@]}" stop "$TEST" >/dev/null 2>&1 || true
}
trap cleanup EXIT

snapshot() {   # snapshot FILE: the configuration record, and FILE.ntbtlog with ntbtlog.txt's driver line count
    attach ro || return 1
    (
        cd "$MNT" || exit 1
        find etc var/lib/AccountsService home -xdev \( -path 'home/*/*' -a ! -name .dmrc \) -prune -o -type f -print0 \
            | sort -z | xargs -0 -r sha256sum
        find etc var/lib/AccountsService -xdev -type l -printf '%p -> %l\n' | sort
    ) > "$1"
    local rc=$?
    grep -c 'Loaded driver' "$MNT$NTBTLOG" > "$1.ntbtlog" 2>/dev/null || echo 0 > "$1.ntbtlog"
    detach
    return $rc
}

# ---- one boot per phase; fail() ends the phase (each phase runs in a subshell) ----

fail() {
    echo "FAIL ($PHASE): $*"
    "${V[@]}" shot "$TEST" "$OUT/$PHASE-fail.png" >/dev/null 2>&1
    "${V[@]}" text "$TEST" 2>/dev/null | head -n 30
    exit 1
}
gsh() { "${V[@]}" sh "$TEST" "$USER_NAME" "$PWFILE" "$1" --timeout "${2:-60}"; }
start_vm() { "${V[@]}" start "$TEST" "${START_ARGS[@]}" >/dev/null || fail "QEMU did not start"; }
shot() { "${V[@]}" shot "$TEST" "$OUT/$PHASE-$1.png" >/dev/null; }
screen_sum() { "${V[@]}" shot "$TEST" >/dev/null 2>&1; md5sum < "$STATE/screen.ppm"; }
wait_text() { "${V[@]}" wait "$TEST" "$1" --timeout "$2" >/dev/null; }

open_menu_vintner() {
    # Vintner Boot's menu shows while Shift is held on BIOS (its GRUB's keystatus --shift, then no timeout) and after a
    # key press on UEFI (systemd-boot). Each generation's variants follow it, and the menu highlights the generation,
    # so the Advanced Options variant is one item down. Its start counts nothing (Vintner Boot's docs/variants.md).
    local _ try
    for try in 1 2; do
        start_vm
        if [ "$FIRMWARE" = bios ]; then
            "${V[@]}" hold "$TEST" shift 20 >/dev/null 2>&1
        else
            for _ in $(seq 1 60); do "${V[@]}" key "$TEST" spc >/dev/null 2>&1; sleep 0.25; done
        fi
        if wait_text "Advanced Options for" 30; then
            shot loader
            move_down 1
            shot loader-variant
            "${V[@]}" key "$TEST" ret >/dev/null
            wait_text "Advanced Options Menu" 180 || fail "the Advanced Options variant did not start the menu"
            sleep 3
            shot menu
            VIA_MENU=1
            return 0
        fi
        [ "$try" = 2 ] && fail "no Vintner Boot menu with the Advanced Options variant"
        shot no-loader-menu
        echo "  (Vintner Boot's menu did not show; starting again)"
        "${V[@]}" stop "$TEST" >/dev/null 2>&1
        wait_off 60 || fail "the VM did not stop"
    done
}

open_menu() {
    # During GRUB's hidden timeout F8, the menu entry's hotkey, boots the Advanced Options entry. Keys sent while the
    # firmware runs are buffered, so press it repeatedly from the start. Once in a while no press reaches GRUB and the
    # machine starts normally; then stop it, clear the start-up state that start may have left, and try once more.
    local _ try
    if [ "$LOADER" = vintner ]; then open_menu_vintner; return; fi
    for try in 1 2; do
        start_vm
        for _ in $(seq 1 40); do "${V[@]}" key "$TEST" f8 >/dev/null 2>&1; sleep 0.25; done
        if wait_text "Advanced Options Menu" 180; then
            sleep 3   # the menu discards the F8 presses that opened it
            shot menu
            return 0
        fi
        [ "$try" = 2 ] && fail "no Advanced Options menu after F8"
        shot no-menu
        echo "  (F8 was not taken; starting again)"
        "${V[@]}" stop "$TEST" >/dev/null 2>&1
        wait_off 60 || fail "the VM did not stop"
        clear_pending || fail "cannot clear wex_boot_pending"
    done
}

move_down() {   # move_down N: N items down; a key the screen does not show is pressed again
    local _ try before
    for _ in $(seq 1 "$1"); do
        before=$(screen_sum)
        for try in 1 2 3; do
            "${V[@]}" key "$TEST" down >/dev/null
            sleep 0.5
            [ "$(screen_sum)" != "$before" ] && break
            [ "$try" = 3 ] && fail "Down was not taken"
        done
    done
}

menu_choice() {   # menu_choice DOWNS LABEL: choose an item, check the OS list shows LABEL, start
    local try before screen
    open_menu
    move_down "$1"
    for try in 1 2 3; do
        before=$(screen_sum)
        "${V[@]}" key "$TEST" ret >/dev/null
        sleep 1
        [ "$(screen_sum)" != "$before" ] && break
    done
    wait_text "operating system to start" 10 || fail "no OS list after the choice"
    screen=$("${V[@]}" text "$TEST")
    grep -q "$2" <<< "$screen" || fail "the OS list does not show '$2'"
    shot oslist
    "${V[@]}" key "$TEST" ret >/dev/null
}

logon_screen() {   # the Welcome screen (logonui.exe)
    wait_text "To begin" 240 || fail "no Welcome screen (automatic logon in Safe Mode, or the GTK greeter?)"
    shot logon
}

logon() {   # logon [TILE PASSWORD_FILE]: click a tile (by default the test user's), then the password if it has one
    local tile=${1:-${WEX_TEST_TILE:-$USER_NAME}} pwfile=${2:-$PWFILE}
    if ! "${V[@]}" clicktext "$TEST" "$tile" --timeout 15 >/dev/null 2>&1; then
        [ -z "${1:-}" ] || fail "no tile '$tile' on the Welcome screen"
        echo "  (OCR did not find the tile '$tile'; Enter starts the first tile's logon)"
        "${V[@]}" key "$TEST" ret >/dev/null
    fi
    if [ -s "$pwfile" ]; then
        # "Type your password" under the name; the pointer, left on the name, can hide the second word
        wait_text "Type your" 30 || wait_text "password" 5 || fail "no password box on the Welcome screen"
        "${V[@]}" type "$TEST" "$(cat "$pwfile")"
        "${V[@]}" key "$TEST" ret >/dev/null
    fi
}

tiles() {   # the Welcome screen's text (OCR), for checking which tiles it shows
    "${V[@]}" text "$TEST" | tr '\n' ' '
}

logon_screen_gtk() {   # the GTK greeter, the last resort when Wine does not start
    wait_text "Cancel" 240 || fail "no GTK greeter"
    shot logon-gtk
}

logon_gtk() {   # the GTK greeter asks for the user name, then the password
    "${V[@]}" type "$TEST" "$USER_NAME"
    "${V[@]}" key "$TEST" ret >/dev/null
    sleep 4
    if [ -s "$PWFILE" ]; then
        "${V[@]}" type "$TEST" "$(cat "$PWFILE")"
        "${V[@]}" key "$TEST" ret >/dev/null
    else
        # an account without a password is authenticated at once and the greeter waits for Log In; OCR does not
        # always read that button, so fall back to its place on the 1280x800 screen
        "${V[@]}" clicktext "$TEST" "Log In" >/dev/null 2>&1 || "${V[@]}" click "$TEST" 736 440 >/dev/null
    fi
}

# prefetch is "absent" on images made before the prefetch service (docs/PREFETCH_SERVICE.md); disk is the root
# file system's use in percent
STATE_CMD='printf "mode=%s default=%s nm=%s timers=%s cron=%s prefetch=%s disk=%s variant=%s\n" "$(wex-bootmode)" "$(systemctl get-default)" "$(systemctl is-active NetworkManager.service)" "$(systemctl list-units --type=timer --state=active --no-legend | wc -l)" "$(systemctl is-active cron.service)" "$(if [ -e /lib/systemd/system/wexprefetchd.socket ]; then systemctl is-active wexprefetchd.socket; else echo absent; fi)" "$(df --output=pcent / | tail -n 1 | tr -d " %")" "$(grep -o "vintner[.]variant=[a-z0-9_]*" /proc/cmdline | cut -d= -f2 | grep . || echo none)"'

kv_check() {   # kv_check LINE key=value|key!=value...: prints the first expectation LINE does not meet
    local line=$1 kv
    shift
    for kv in "$@"; do
        case "$kv" in
            *!=*) case " $line " in *" ${kv%%!=*}=${kv#*!=} "*) echo "$kv"; return 1 ;; esac ;;
            *) case " $line " in *" $kv "*) ;; *) echo "$kv"; return 1 ;; esac ;;
        esac
    done
}

expect_state() {   # expect_state key=value|key!=value...: the guest's mode and units, read on the serial console
                   # (with Vintner Boot also the variant: the Advanced Options one after the menu, else none)
    local line miss disk variant=()
    line=$(gsh "$STATE_CMD" | grep '^mode=' | tail -n 1)
    echo "  $line"
    [ -n "$line" ] || fail "no answer on the serial console"
    if [ "$LOADER" = vintner ]; then
        if [ -n "${VIA_MENU:-}" ]; then variant=(variant=advanced); else variant=(variant=none); fi
    fi
    miss=$(kv_check "$line" "$@" "${variant[@]}") || fail "expected $miss: $line"
    # a full test disk makes every later result unreliable (VMs have run out of space before)
    disk=${line##*disk=}
    disk=${disk%% *}
    case "$disk" in ''|*[!0-9]*) fail "no disk use in: $line" ;; esac
    if [ "$disk" -ge 95 ]; then fail "the guest's root file system is ${disk}% full"; fi
    if [ "$disk" -ge 80 ]; then echo "  WARNING: the guest's root file system is ${disk}% full"; fi
}

expect_session_mode() {   # the logged-on Explorer session's WEXMODE
    local out
    out=$(gsh "python3 $HELPER --user $USER_NAME --report")
    grep -qx "WEXMODE=$1" <<< "$out" || fail "the Explorer session does not have WEXMODE=$1: $(tr '\n' ' ' <<< "$out")"
    echo "  session WEXMODE=$1"
}

desktop() {   # desktop TIMEOUT
    wait_text "Recycle Bin" "$1" || fail "no Explorer desktop"
    shot desktop
}

safe_desktop() {   # safe_desktop TIMEOUT: after a Safe Mode logon the message box (work order C2), answered with Enter
                   # (Yes, the default), then the desktop. WEX_TEST_SAFE_BOX=required fails when there is no box.
    local end=$((SECONDS + $1)) screen seen=
    while [ "$SECONDS" -lt "$end" ]; do
        screen=$("${V[@]}" text "$TEST" 2>/dev/null | tr '\n' ' ')
        case "$screen" in
            *"running in Safe Mode"*) seen=1; shot box; "${V[@]}" key "$TEST" ret >/dev/null; break ;;   # OCR reads the first line whole
            *"Recycle Bin"*) break ;;
        esac
        sleep 3
    done
    if [ -n "$seen" ]; then
        echo "  Safe Mode message box answered with Yes"
    elif [ "$SAFE_BOX" = required ]; then
        fail "no Safe Mode message box after logon"
    else
        echo "  (no Safe Mode message box)"
    fi
    desktop 120
}

wait_serial() {   # wait_serial TIMEOUT: until the serial console answers; vmtest.py sh waits 15 s for a login prompt
    local end=$((SECONDS + $1))
    while [ "$SECONDS" -lt "$end" ]; do
        gsh 'echo serial=up' 10 2>/dev/null | grep -q '^serial=up' && return 0
        sleep 3
    done
    return 1
}

expect_pending() {   # expect_pending 0|1|unset: GRUB's wex_boot_pending (11_wineexplorer_advanced) in the guest
    local out
    [ "$LOADER" = grub ] || return 0   # Vintner Boot keeps its own counters (docs/VINTNER_BOOT.md 4.3)
    out=$(gsh 'v=$(grub-editenv /boot/grub/grubenv list | sed -n "s/^wex_boot_pending=//p"); echo "pending=${v:-unset}"' \
        | grep '^pending=' | tail -n 1)
    echo "  wex_boot_$out"
    [ "$out" = "pending=$1" ] || fail "expected wex_boot_pending=$1 in grubenv: ${out:-no answer}"
}

expect_check() {   # the start-up check (scripts/wex-startup-check.sh) finished without error this boot. Its verdict
                   # is in the system journal, which the test user cannot read; expect_pending shows whether it counted.
    local out
    out=$(gsh 'systemctl show -p ActiveState -p Result wex-startup-check.service | tr "\n" " "' | grep 'ActiveState=' | tail -n 1)
    echo "  start-up check: $out"
    case " $out " in
        *" ActiveState=active "*) case " $out " in *" Result=success "*) return 0 ;; esac ;;
    esac
    fail "the start-up check did not finish successfully: ${out:-no answer}"
}

# the pattern scripts/wex-startup-check.sh uses for the Welcome screen (scripts/test_safemode.py keeps them equal)
LOGONUI_PATTERN='^([^ ]*[\/])?logonui\.exe( |$)'

expect_logonui_process() {   # on the Welcome screen: the start-up check's pattern finds logonui.exe, and only it
    local out
    out=$(gsh "pgrep -u lightdm -a -f logonui | cut -c1-150; echo matched=\$(pgrep -u lightdm -f '$LOGONUI_PATTERN' | wc -l)")
    sed 's/^/  lightdm: /' <<< "$out" | grep -v '^  lightdm: $'
    grep -qx 'matched=1' <<< "$out" || fail "the start-up check's pattern does not match exactly one logonui.exe process"
}

countdown() {   # the failure screen's countdown, or nothing when its line is gone
    "${V[@]}" text "$TEST" | sed -n 's/.*Seconds until WineExplorer starts: *\([0-9][0-9]*\).*/\1/p' | head -n 1
}

failure_screen() {   # "WineExplorer did not start successfully", with its countdown running
    local first second
    wait_text "did not start successfully" 240 || fail "no failure screen"
    shot failure
    first=$(countdown)
    sleep 3
    second=$(countdown)
    echo "  failure screen; countdown ${first:-?}, three seconds later ${second:-?}"
    [ -n "$first" ] && [ -n "$second" ] || fail "no countdown on the failure screen"
    [ "$second" -lt "$first" ] || fail "the countdown does not count down ($first, then $second)"
}

programs_32_64() {   # the regression check: Wine's 32-bit (syswow64) and 64-bit (system32) Notepad open
    local bits dir
    for bits in 32 64; do
        if [ "$bits" = 32 ]; then dir=syswow64; else dir=system32; fi
        gsh "echo $bits-bit Notepad > /tmp/wex-notepad$bits.txt; python3 $HELPER --user $USER_NAME C:/windows/$dir/notepad.exe Z:/tmp/wex-notepad$bits.txt" >/dev/null \
            || fail "could not start the $bits-bit Notepad"
        wait_text "wex-notepad$bits" 60 || fail "the $bits-bit Notepad did not open"
        shot "notepad$bits"
        echo "  $bits-bit Notepad open"
    done
}

expect_safeboot() {   # expect_safeboot 0|1|2|cmd: what Windows programs see, from the 32-bit and the 64-bit probe.
                      # cmd: Safe Mode with Command Prompt, run from the serial console, as from the recovery console
    local bits out miss raw run want=()
    [ -n "$PROBE_DIR" ] || return 0
    case "$1" in
        0) want=(cleanboot=0 option=none shell=none MountMgr=running spooler=untried) ;;
        1) want=(cleanboot=1 option=1 shell=none EventLog=running PlugPlay=running MountMgr=running NDIS=stopped nsiproxy=stopped spooler=1084) ;;
        2) want=(cleanboot=2 option=2 shell=none EventLog=running PlugPlay=running MountMgr=running NDIS=running nsiproxy=running spooler=1084) ;;
        cmd) want=(cleanboot=1 option=1 shell=1 EventLog=running PlugPlay=running MountMgr=running NDIS=stopped nsiproxy=stopped spooler=1084) ;;
    esac
    for bits in 32 64; do
        if [ "$1" = cmd ]; then
            run="timeout 150 env WINEDEBUG=err-all wine $PROBES/safeboot-probe$bits.exe > /tmp/wex-safeboot$bits.out 2>&1; echo wine-rc=\$?; tail -n 15 /tmp/wex-safeboot$bits.out"
        else
            run="rm -f /tmp/wex-safeboot$bits.log; python3 $HELPER --user $USER_NAME --wait --log-file /tmp/wex-safeboot$bits.log $PROBES/safeboot-probe$bits.exe >/dev/null; grep '^bits=' /tmp/wex-safeboot$bits.log"
        fi
        raw=$(gsh "$run" 180)
        out=$(grep '^bits=' <<< "$raw" | tail -n 1)
        echo "  probe: $out"
        if ! miss=$(kv_check "$out" "bits=$bits" "${want[@]}"); then
            sed 's/^/  | /' <<< "$raw" | tail -n 20
            fail "the $bits-bit probe: expected $miss: ${out:-no answer}"
        fi
    done
}

shutdown_vm() { "${V[@]}" shutdown "$TEST" --timeout 180 >/dev/null || fail "no clean shutdown"; }

phase_safe() {
    menu_choice 0 "Safe Mode"
    logon_screen
    expect_state mode=safe default=wex-safemode.target nm=inactive timers=0 cron=inactive prefetch!=active
    expect_logonui_process
    logon
    safe_desktop 400
    expect_session_mode safe
    expect_safeboot 1
    programs_32_64
    shutdown_vm
}

phase_safenet() {
    menu_choice 1 "Safe Mode with Networking"
    logon_screen
    expect_state mode=safenet default=wex-safenet.target nm=active timers=0 cron=inactive prefetch!=active
    logon
    safe_desktop 400
    expect_session_mode safenet
    expect_safeboot 2
    shutdown_vm
}

phase_safecmd() {
    menu_choice 2 "Safe Mode with Command Prompt"
    logon_screen
    expect_state mode=safecmd default=wex-safemode.target nm=inactive timers=0 cron=inactive prefetch!=active
    expect_pending 0
    logon
    recovery_console
    expect_safeboot cmd   # after the console check, which wants no wineserver
    shutdown_vm
}

recovery_console() {   # after logging on in Safe Mode with Command Prompt: the console, and no wineserver
    local out
    wait_text "Recovery Commands" 120 || fail "no recovery console after logging on"
    shot console
    out=$(gsh 'echo wineservers=$(pgrep -c wineserver) xterms=$(pgrep -c xterm)' | grep '^wineservers=')
    echo "  $out"
    case "$out" in "wineservers=0 xterms="[1-9]*) ;; *) fail "expected the recovery console without Wine: $out" ;; esac
}

phase_nowine() {   # /usr/local/bin/wine is hidden for this boot (the main loop does it)
    menu_choice 2 "Safe Mode with Command Prompt"
    logon_screen_gtk
    expect_state mode=safecmd default=wex-safemode.target nm=inactive timers=0 cron=inactive prefetch!=active
    logon_gtk
    recovery_console
    shutdown_vm
}

phase_bootlog() {
    local before after='' _
    before=$(cat "$OUT/before-$PHASE.txt.ntbtlog")
    menu_choice 3 "Enable Boot Logging"
    desktop 600
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    for _ in $(seq 1 12); do
        after=$(gsh "grep -c 'Loaded driver' $NTBTLOG" | grep -x '[0-9]*' | tail -n 1)
        [ -n "$after" ] && [ "$after" -gt "$before" ] && break
        sleep 5
    done
    echo "  ntbtlog.txt driver lines: $before before, ${after:-none} after"
    [ -n "$after" ] && [ "$after" -gt "$before" ] || fail "ntbtlog.txt gained no driver lines"
    expect_session_mode normal
    shutdown_vm
}

phase_startnormal() {
    open_menu
    move_down 6   # Safe Mode, the two other Safe Modes, Boot Logging, Debugging, Disable restart, Start Normally
    shot highlight
    "${V[@]}" key "$TEST" ret >/dev/null
    desktop 600
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    expect_check
    expect_pending 0   # not counted: the Advanced Options entry wrote the previous value back
    expect_session_mode normal
    shutdown_vm
}

phase_normal() {
    local out
    start_vm
    desktop 600
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    out=$(gsh 'echo early=$(ls -A /run/systemd/generator.early 2>/dev/null | grep -c -E "^(default|timers)\.target$")' | grep '^early=')
    [ "$out" = early=0 ] || fail "the boot-mode generator wrote units in a normal start ($out)"
    expect_check
    expect_pending unset   # counted: the check cleared it
    expect_session_mode normal
    # work order B0: a boot without the menu records its mode where Wine reads it (on images that have the unit)
    out=$(gsh 'echo record=$(cat /run/wineexplorer/bootmode 2>/dev/null || echo none) unit=$(systemctl is-enabled wex-bootmode-record.service 2>/dev/null || echo absent) dir=$(stat -c %U /run/wineexplorer 2>/dev/null)' \
        | grep '^record=' | tail -n 1)
    echo "  $out"
    case "$out" in
        "record=normal unit=enabled dir=root"|"record=none unit=absent dir="*) ;;
        *) fail "the boot mode record: ${out:-no answer}" ;;
    esac
    expect_safeboot 0
    shutdown_vm
}

phase_failed() {   # the main loop holds the logon screen back for this phase (delay_logon)
    local try
    # Once the serial console answers, Linux runs and GRUB has recorded this start, and the logon screen is still
    # held back: the Reset button now ends a start that did not finish, as the XP capture was made (a reset during
    # the boot splash). An automatic logon follows Linux within seconds, too soon to reset reliably without the hold.
    start_vm
    sleep 15
    wait_serial 150 || fail "the serial console did not answer"
    expect_pending 1
    "${V[@]}" reset "$TEST" >/dev/null || fail "the Reset button did not work"
    echo "  reset before the logon screen"
    failure_screen
    # Up stops the countdown, removes its line and, over the grayed Last Known Good, highlights Safe Mode with
    # Command Prompt, which starts at once
    for try in 1 2 3; do
        "${V[@]}" key "$TEST" up >/dev/null
        sleep 1.5
        [ -z "$(countdown)" ] && break
        [ "$try" = 3 ] && fail "Up did not stop the countdown"
    done
    shot stopped
    "${V[@]}" key "$TEST" ret >/dev/null
    logon_screen
    expect_state mode=safecmd default=wex-safemode.target nm=inactive timers=0 cron=inactive prefetch!=active
    expect_pending 1   # a Safe Mode start from the failure screen does not count
    shutdown_vm
    wait_off 60 || fail "the VM did not power off"
    # so the next start shows the screen again; left alone, its countdown starts normally, and that start counts
    start_vm
    failure_screen
    desktop 600
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    expect_check
    expect_pending unset   # Start Normally on the failure screen counts
    expect_session_mode normal
    shutdown_vm
}

shows() { wait_text "$1" 20; }          # a tile is on the screen (wait also tries the white-text threshold)
hides() { ! wait_text "$1" 8; }         # ... and is not

to_logon_screen() {   # from a normal start's desktop to the logon screen (fast user switching)
    if ! gsh 'XDG_SEAT_PATH=/org/freedesktop/DisplayManager/Seat0 dm-tool switch-to-greeter' >/dev/null; then
        "${V[@]}" key "$TEST" meta_l-l >/dev/null   # Windows+L
    fi
    logon_screen
}

phase_accounts() {   # provisioning gave Administrator a test password and hid wexhidden ("Hidden Test")
    local out
    menu_choice 0 "Safe Mode"
    logon_screen
    shows Administrator || fail "no Administrator tile on the Safe Mode Welcome screen"
    hides "Hidden Test" || fail "the Safe Mode Welcome screen shows the account hidden by SpecialAccounts\\UserList"
    shows "$USER_NAME" || fail "the Safe Mode Welcome screen does not show $USER_NAME"
    shot tiles-safe
    echo "  Safe Mode Welcome screen: Administrator and $USER_NAME shown, Hidden Test not"
    logon Administrator "$ADMIN_PWFILE"
    safe_desktop 400
    out=$(gsh 'loginctl list-sessions --no-legend | awk "{ print \$3 }" | sort -u | tr "\n" " "' | tail -n 1)
    echo "  sessions: $out"
    grep -qw administrator <<< "$out" || fail "Administrator did not log on"
    shutdown_vm
    wait_off 60 || fail "the VM did not power off"
    # a normal start logs the test user on automatically; its logon screen leaves out both accounts
    start_vm
    desktop 600
    to_logon_screen
    shows "$USER_NAME" || fail "the logon screen of a normal start does not show $USER_NAME"
    hides Administrator || fail "the logon screen of a normal start shows Administrator"
    hides "Hidden Test" || fail "the logon screen of a normal start shows the hidden account"
    shot tiles-normal
    echo "  normal start's logon screen: $USER_NAME shown, Administrator and Hidden Test not"
    shutdown_vm
}

# ---- the overlay ----

command -v qemu-nbd >/dev/null || die "qemu-nbd is missing"
[ -f "$SRC_DISK" ] || die "no disk $SRC_DISK"
running "$SRC_DISK" && die "$SOURCE is running; stop it first"
if [ -e "$STATE/disk.qcow2" ]; then
    qemu-img info "$STATE/disk.qcow2" 2>/dev/null | grep -qx "backing file: $SRC_DISK" \
        || die "$STATE/disk.qcow2 exists and is not an overlay of $SOURCE; choose another TEST_VM"
    "${V[@]}" stop "$TEST" >/dev/null 2>&1 || true
    wait_off 30 || die "$TEST is still running"
fi
rm -rf -- "$STATE"
mkdir -p "$OUT"
qemu-img create -q -f qcow2 -b "$SRC_DISK" -F qcow2 "$STATE/disk.qcow2" || die "cannot create the overlay"
if [ -n "${WEX_TEST_PASSWORD_FILE:-}" ]; then
    PWFILE=$WEX_TEST_PASSWORD_FILE
else
    PWFILE=$OUT/password
    : > "$PWFILE"
fi
echo "=== $TEST: overlay of $SOURCE, $FIRMWARE, Safe Mode files: $FILES"

attach rw || die "cannot mount the overlay"
install -D -m 755 "$HERE/session-run.py" "$MNT$HELPER" || die "cannot install the test helper"
if [ -n "$PROBE_DIR" ]; then
    install -m 755 "$PROBE_DIR/safeboot-probe32.exe" "$PROBE_DIR/safeboot-probe64.exe" "$MNT$PROBES/" \
        || die "cannot install the Safe Mode probes"
fi
ADMIN_PWFILE=$OUT/admin-password
STEP=
# The provisioning unit runs before the logon screen; with upgrade, inside the old Safe Mode, once it is set up
ORDER='Before=display-manager.service'
if [ "$FILES" = branch ]; then
    for entry in "${SAFE_MODE_FILES[@]}"; do
        read -r mode src dst <<< "$entry"
        install -D -m "$mode" "$REPO/$src" "$MNT/$dst" || die "cannot install $src"
    done
    for old in "${OLD_FILES[@]}"; do rm -f "$MNT/$old"; done
    {
        echo '#!/bin/sh'
        echo 'set -e'
        awk '/^# Safe Mode \(docs\/SAFE_MODE\.md\)/ { p = 1 } /^systemctl daemon-reload/ { p = 0 } p' \
            "$REPO/installer/package-wine.sh"
    } > "$MNT/root/wex-test-safemode-postinst.sh"
    grep -q pam_exec "$MNT/root/wex-test-safemode-postinst.sh" \
        || die "the Safe Mode part of the postinst was not found in installer/package-wine.sh"
    STEP='sh -x /root/wex-test-safemode-postinst.sh; echo "postinst=$?"'
    if [ -n "$WINE_TAR" ]; then
        cp -- "$WINE_TAR" "$MNT/root/wex-test-wine.tar.gz" || die "cannot copy the Wine tarball onto the overlay"
        # then what the Wine package's postinst does: update the machine root, whose HKLM gets wine.inf's changes
        # (the SafeBoot lists); a user's prefix update cannot write the machine's HKLM under the system role
        STEP='tar -xzf /root/wex-test-wine.tar.gz -C / --no-same-owner; echo "wine=$?"; rm -f /root/wex-test-wine.tar.gz
if [ -d /var/lib/wineexplorer/machine/drive_c/windows/system32 ] && [ -x /usr/local/bin/wex-setup-machine.sh ]; then
    /usr/local/bin/wex-setup-machine.sh --update; echo "machine=$?"
fi
'"$STEP"
    fi
elif [ "$FILES" = upgrade ]; then
    [ -e "$MNT/etc/grub.d/11_wineexplorer_safemode" ] \
        || die "$SOURCE has no Safe Mode from before docs/SAFE_MODE.md (/etc/grub.d/11_wineexplorer_safemode)"
    old_entry=$(grep -o "^menuentry '[^']*Safe Mode with Command Prompt[^']*'" "$MNT/boot/grub/grub.cfg" | head -n 1 | cut -d"'" -f2)
    [ -n "$old_entry" ] || die "the grub.cfg of $SOURCE has no Safe Mode with Command Prompt entry"
    grub-editenv "$MNT/boot/grub/grubenv" set "next_entry=$old_entry" || die "cannot choose '$old_entry' for the next start"
    AUTOLOGIN_CONFS=$(find "$MNT/etc/lightdm/lightdm.conf.d" -maxdepth 1 -name '*autologin*' | wc -l)
    install -d -m 700 "$MNT/root/wex-test-debs" && cp -- "$UPGRADE_DEBS"/*.deb "$MNT/root/wex-test-debs/" \
        || die "cannot copy the packages onto the overlay"
    useradd --root "$MNT" --create-home --comment 'Standard Test' wexstd || die "cannot add the account wexstd"
    cat > "$MNT/root/wex-test-upgrade.sh" <<'EOF'
#!/bin/sh
# installer/tests/safe-mode.sh, WEX_SAFEMODE_FILES=upgrade: runs while the old Safe Mode with Command Prompt is up
state() {   # what the old Safe Mode changed: its LightDM drop-in and run-time state, PAM lines, the standard
            # accounts it gave its session or hid, its unit, and the automatic logon drop-ins it moved away
    sessions=0
    hidden=0
    for f in /var/lib/AccountsService/users/*; do
        uid=$(id -u "${f##*/}" 2>/dev/null) || continue
        [ "$uid" -ge 1000 ] && [ "$uid" -lt 60000 ] || continue
        if grep -qEx 'X?Session=wex-safe-terminal' "$f"; then sessions=$((sessions + 1)); fi
        if grep -qx 'SystemAccount=true' "$f"; then hidden=$((hidden + 1)); fi
    done
    echo "mode=$(grep -o 'wexmode=[a-z]*' /proc/cmdline | head -n 1 | cut -d= -f2)" \
        "dropin=$(if [ -e /etc/lightdm/lightdm.conf.d/99-safemode.conf ]; then echo yes; else echo no; fi)" \
        "run=$(if [ -e /run/wex-safemode ]; then echo yes; else echo no; fi)" \
        "pam=$(cat /etc/pam.d/lightdm /etc/pam.d/lightdm-autologin | grep -c wex-safemode-pam-check)" \
        "sessions=$sessions hidden=$hidden" \
        "unit=$(ls /etc/systemd/system/*.wants/wex-safemode.service 2>/dev/null | wc -l)" \
        "autologin=$(ls /etc/lightdm/lightdm.conf.d | grep -c autologin)"
}
echo "old: $(state)"
dpkg -i /root/wex-test-debs/*.deb; echo "dpkg=$?"
rm -rf /root/wex-test-debs
echo "new: $(state)"
# the logon screen is up; stop it before the test accounts and the logon prefix are set up
systemctl stop display-manager.service; echo "logon-stopped=$?"
EOF
    STEP='sh /root/wex-test-upgrade.sh; rm -f /root/wex-test-upgrade.sh'
    ORDER='After=wex-safemode.service display-manager.service'
fi
# a random password for the Administrator account (work order C4), for this overlay only
head -c 64 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 16 > "$ADMIN_PWFILE"
install -m 600 "$ADMIN_PWFILE" "$MNT/root/wex-test-admin-password" || die "cannot copy the Administrator password"
{
    echo '#!/bin/sh'
    echo '# installer/tests/safe-mode.sh: runs once, removes itself and powers off'
    echo 'exec > /root/wex-test-provision.log 2>&1'
    echo "$STEP"
    cat <<'EOF'
if command -v update-grub >/dev/null; then update-grub; echo "update-grub=$?"; else echo "update-grub=none"; fi
# test accounts (work order C4, C7): Administrator with the test password; wexhidden hidden by SpecialAccounts\UserList
if getent passwd administrator >/dev/null; then
    printf 'administrator:%s\n' "$(cat /root/wex-test-admin-password)" | chpasswd; echo "adminpw=$?"
fi
rm -f /root/wex-test-admin-password
if ! getent passwd wexhidden >/dev/null; then
    useradd --create-home --comment 'Hidden Test' wexhidden && passwd -d wexhidden >/dev/null
fi
echo "wexhidden=$?"
mkdir -p /var/lib/AccountsService/users
printf '[User]\nSession=wine-explorer\nXSession=wine-explorer\nSystemAccount=false\n' > /var/lib/AccountsService/users/wexhidden
if [ ! -e /var/lib/lightdm/.wine ] && [ -d /var/lib/lightdm/.wine-logon ]; then
    mv /var/lib/lightdm/.wine-logon /var/lib/lightdm/.wine
fi
# The greeter makes its prefix at its first start; make it now, as wex-greeter.sh does, so its registry can get the
# value (an image without the system role has no logon prefix until then)
if [ ! -e /var/lib/lightdm/.wine/system.reg ]; then
    runuser -u lightdm -- env -u DISPLAY HOME=/var/lib/lightdm WINEPREFIX=/var/lib/lightdm/.wine WINELINKFAKEDLLS=1 \
        WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' WINEDEBUG=-all /usr/local/bin/wine wineboot.exe -u
    runuser -u lightdm -- env HOME=/var/lib/lightdm WINEPREFIX=/var/lib/lightdm/.wine /usr/local/bin/wineserver -w
    echo "logonprefix=$?"
fi
# Wine's text registry format; the logon screen reads the machine hive (system role) or the prefix's own
for hive in /var/lib/wineexplorer/machine/system.reg /var/lib/lightdm/.wine/system.reg; do
    if [ -f "$hive" ] && [ ! -L "$hive" ]; then
        printf '\n%s\n%s\n%s\n' '[Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\SpecialAccounts\\UserList] 1700000000' \
            '#time=1d9a0000000000' '"wexhidden"=dword:00000000' >> "$hive"
        echo "userlist=$hive"
    fi
done
rm -f /etc/systemd/system/multi-user.target.wants/wex-test-provision.service /etc/systemd/system/wex-test-provision.service
# /usr changed (a Wine tarball): record that now, so no later boot changes /etc/.updated
/usr/lib/systemd/systemd-update-done; echo "update-done=$?"
sync
systemctl --no-block poweroff
EOF
} > "$MNT/root/wex-test-provision.sh"
chmod 755 "$MNT/root/wex-test-provision.sh"
printf '%s\n' '[Unit]' 'Description=Safe Mode test provisioning (installer/tests/safe-mode.sh)' 'After=local-fs.target' \
    "$ORDER" '' '[Service]' 'Type=oneshot' 'ExecStart=/root/wex-test-provision.sh' \
    > "$MNT/etc/systemd/system/wex-test-provision.service"
mkdir -p "$MNT/etc/systemd/system/multi-user.target.wants"
ln -sf /etc/systemd/system/wex-test-provision.service \
    "$MNT/etc/systemd/system/multi-user.target.wants/wex-test-provision.service"
detach

echo "=== provisioning boot ($(date +%T))"
"${V[@]}" start "$TEST" "${START_ARGS[@]}" >/dev/null || die "QEMU did not start"
wait_off 1500 || die "the provisioning boot did not power off"
attach ro || die "cannot mount the overlay"
cp "$MNT/root/wex-test-provision.log" "$OUT/provision.log" 2>/dev/null
provisioned() {   # provisioned LINE MESSAGE: the provisioning log has LINE, or the test ends with MESSAGE
    grep -qx "$1" "$OUT/provision.log" || { detach; die "$2 (see $OUT/provision.log)"; }
}
if [ -n "$WINE_TAR" ]; then provisioned 'wine=0' "unpacking the Wine tarball failed"; fi
if [ -n "$WINE_TAR" ] && grep -q '^machine=' "$OUT/provision.log"; then
    provisioned 'machine=0' "updating the machine root after the Wine tarball failed"
fi
if [ "$FILES" = branch ]; then provisioned 'postinst=0' "the postinst's Safe Mode part failed"; fi
if [ "$FILES" = upgrade ]; then
    # the old Safe Mode with Command Prompt was running, with everything it changes, when dpkg installed the packages
    old=$(sed -n 's/^old: //p' "$OUT/provision.log" | tail -n 1)
    new=$(sed -n 's/^new: //p' "$OUT/provision.log" | tail -n 1)
    echo "  before the upgrade: $old"
    echo "  after the upgrade:  $new"
    # (the old unit is wanted by display-manager.service and multi-user.target)
    miss=$(kv_check "$old" mode=safecmd dropin=yes run=yes pam!=0 hidden!=0 unit!=0 autologin=0) \
        || { detach; die "the old Safe Mode with Command Prompt was not running as expected ($miss)"; }
    provisioned 'dpkg=0' "dpkg did not install the packages"
    miss=$(kv_check "$new" dropin=no run=no pam=2 sessions=0 hidden=0 unit=0 "autologin=$AUTOLOGIN_CONFS") \
        || { detach; die "the postinst left part of the old Safe Mode ($miss)"; }
    # and nothing came back when the old Safe Mode's unit stopped at power-off
    for left in "${OLD_FILES[@]}" etc/lightdm/lightdm.conf.d/99-safemode.conf; do
        [ ! -e "$MNT/$left" ] || { detach; die "the upgrade left /$left"; }
    done
    ! compgen -G "$MNT/etc/systemd/system/*.wants/wex-safemode.service" >/dev/null \
        || { detach; die "wex-safemode.service is still enabled"; }
    ! grep -q "^menuentry '[^']*Safe Mode with Command Prompt" "$MNT/boot/grub/grub.cfg" \
        || { detach; die "grub.cfg still has the old Safe Mode with Command Prompt entry"; }
    [ "$(grep -c wex-safemode-pam-check "$MNT/etc/pam.d/lightdm")" = 1 ] \
        && grep -qx 'account requisite pam_exec.so quiet stdout /usr/local/sbin/wex-safemode-pam-check.sh' "$MNT/etc/pam.d/lightdm" \
        || { detach; die "/etc/pam.d/lightdm does not have exactly the new boot mode check"; }
    ! grep -qx 'SystemAccount=true' "$MNT/var/lib/AccountsService/users/wexstd" \
        || { detach; die "the standard account wexstd is still hidden"; }
    echo "  upgrade: nothing of the old Safe Mode is left"
fi
if [ "$LOADER" = grub ]; then
    provisioned 'update-grub=0' "update-grub failed"
else
    provisioned 'update-grub=none' "a Vintner Boot install still has GRUB's update-grub"
    [ -f "$MNT/etc/vintner/variants.d/advanced.conf" ] \
        || { detach; die "no Advanced Options variant (/etc/vintner/variants.d/advanced.conf)"; }
fi
provisioned 'adminpw=0' "the Administrator account is missing or got no password"
# the logon screen reads the logon prefix's own hive, or the machine hive once the system role serves it (C7)
grep -qE '^userlist=/var/lib/(lightdm/[.]wine|wineexplorer/machine)/system[.]reg$' "$OUT/provision.log" \
    || { detach; die "neither the logon prefix's nor the machine's registry got the SpecialAccounts\\UserList value (see $OUT/provision.log)"; }
if [ "$LOADER" = grub ]; then
    grep -q 'wexmode=menu' "$MNT/boot/grub/grub.cfg" && grep -q -- '--hotkey=f8' "$MNT/boot/grub/grub.cfg" \
        || { detach; die "grub.cfg has no Advanced Options entry with the F8 hotkey"; }
    grep -q 'save_env wex_boot_pending' "$MNT/boot/grub/grub.cfg" \
        || { detach; die "grub.cfg does not record unfinished starts (wex_boot_pending)"; }
fi
grep -q 'wex-safemode-pam-check.sh autologin' "$MNT/etc/pam.d/lightdm-autologin" \
    || { detach; die "/etc/pam.d/lightdm-autologin does not consult the boot mode"; }
[ -L "$MNT/etc/systemd/system/multi-user.target.wants/wex-startup-check.service" ] \
    || { detach; die "wex-startup-check.service is not enabled"; }
detach

# ---- the phases ----

LOGON_DELAY=usr/lib/systemd/system/lightdm.service.d/wex-test-delay.conf
delay_logon() {   # delay_logon on|off: LightDM starts 90 seconds late for the failed phase (outside the records)
    attach rw || return 1
    if [ "$1" = on ]; then
        mkdir -p "$(dirname "$MNT/$LOGON_DELAY")" &&
            printf '%s\n' '# installer/tests/safe-mode.sh, failed phase only' '[Service]' 'ExecStartPre=/bin/sleep 90' \
                > "$MNT/$LOGON_DELAY"
    else
        rm -f "$MNT/$LOGON_DELAY"
    fi
    local rc=$?
    detach
    return $rc
}

hide_wine() {   # hide_wine on|off: /usr/local/bin/wine away for the nowine phase, and back (outside the records)
    attach rw || return 1
    if [ "$1" = on ]; then
        mv "$MNT/usr/local/bin/wine" "$MNT/usr/local/bin/wine.wex-test-hidden"
    else
        mv "$MNT/usr/local/bin/wine.wex-test-hidden" "$MNT/usr/local/bin/wine"
    fi
    local rc=$?
    detach
    return $rc
}

clear_pending() {   # every phase starts from a start that counted; say so when the last one did not
    local value
    [ "$LOADER" = grub ] || return 0
    attach rw || return 1
    value=$(grub-editenv "$MNT/boot/grub/grubenv" list | sed -n 's/^wex_boot_pending=//p')
    if [ "$value" = 1 ]; then echo "  (the start before $PHASE did not count; wex_boot_pending cleared)"; fi
    grub-editenv "$MNT/boot/grub/grubenv" unset wex_boot_pending
    local rc=$?
    detach
    return $rc
}

command -v grub-editenv >/dev/null || die "grub-editenv is missing (package grub-common)"
overall=0
summary=()
for PHASE in "${PHASES[@]}"; do
    snapshot "$OUT/before-$PHASE.txt" || die "cannot record the configuration before $PHASE"
    clear_pending || die "cannot clear wex_boot_pending before $PHASE"
    if [ "$PHASE" = nowine ]; then hide_wine on || die "cannot hide /usr/local/bin/wine"; fi
    if [ "$PHASE" = failed ]; then delay_logon on || die "cannot hold the logon screen back"; fi
    echo "=== $PHASE ($(date +%T))"
    ( "phase_$PHASE" ) 2>&1 | tee "$OUT/$PHASE.log"
    rc=${PIPESTATUS[0]}
    if [ "$rc" -ne 0 ]; then
        "${V[@]}" stop "$TEST" >/dev/null 2>&1 || true
    fi
    wait_off 60 || die "$TEST did not stop after $PHASE"
    if [ "$PHASE" = nowine ]; then hide_wine off || die "cannot put /usr/local/bin/wine back"; fi
    if [ "$PHASE" = failed ]; then delay_logon off || die "cannot remove the logon delay"; fi
    snapshot "$OUT/after-$PHASE.txt" || die "cannot record the configuration after $PHASE"
    if ! diff -u "$OUT/before-$PHASE.txt" "$OUT/after-$PHASE.txt" > "$OUT/$PHASE-changes.diff"; then
        echo "FAIL ($PHASE): the boot changed files under /etc, /var/lib/AccountsService or a .dmrc:"
        grep '^[-+][^-+]' "$OUT/$PHASE-changes.diff" | head -n 20
        rc=1
    fi
    if [ "$rc" -eq 0 ]; then summary+=("PASS $PHASE"); else summary+=("FAIL $PHASE"); overall=1; fi
done

# The Welcome screen ran (any Safe Mode phase): its prefix is now lightdm's ~/.wine, moved from ~/.wine-logon if
# the image had one (docs/SAFE_MODE_WORKORDER.md C7)
case " ${PHASES[*]} " in
    *" safe "*|*" safenet "*|*" safecmd "*|*" failed "*)
        if attach ro; then
            # a made prefix has user.reg; under the system role its HKLM is the machine root's (no system.reg)
            if [ -f "$MNT/var/lib/lightdm/.wine/user.reg" ]; then
                if [ -e "$MNT/var/lib/lightdm/.wine-logon" ]; then
                    summary+=("FAIL logon prefix (~/.wine-logon is still there)"); overall=1
                else
                    summary+=("PASS logon prefix (/var/lib/lightdm/.wine)")
                fi
            else
                summary+=("FAIL logon prefix (no /var/lib/lightdm/.wine/user.reg)"); overall=1
            fi
            detach
        else
            summary+=("FAIL logon prefix (cannot mount the overlay)"); overall=1
        fi
        ;;
esac

echo "=== $TEST ($FIRMWARE, Safe Mode files: $FILES)"
printf '%s\n' "${summary[@]}"
if [ "$overall" -eq 0 ]; then
    echo "PASS: Safe Mode test; evidence in $OUT"
else
    echo "FAIL: Safe Mode test; evidence in $OUT"
fi
exit "$overall"
