#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Safe Mode and the Advanced Options menu on an installed disk (docs/SAFE_MODE.md, docs/SAFE_MODE_WORKORDER.md A1).
#
# Boots a copy-on-write overlay of an installed test disk once per phase, reaching the menu through Vintner Boot's
# Advanced Options variant (installer/vintner/advanced.conf) in Vintner's menu, which Shift held on BIOS and F8 held
# on UEFI open (Vintner Boot's docs/menu-key.md); each start reports its variant on the serial console. Then it
# checks each start:
#   safe         Safe Mode: the OS list shows the mode, the Welcome screen instead of automatic logon, only the
#                Safe Mode units run, the Explorer session has WEXMODE=safe, Wine's 32-bit and 64-bit Notepad open;
#                the Welcome screen and the desktop at 640x480 (work order C3), as in every Safe Mode, and a normal
#                start afterwards not
#   safenet      Safe Mode with Networking: the Welcome screen, NetworkManager runs, no timers, WEXMODE=safenet
#   safecmd      Safe Mode with Command Prompt: logging on starts the recovery console and no wineserver
#   nowine       Safe Mode with Command Prompt with Wine missing: the GTK greeter takes over, and logging on still
#                starts the recovery console (the test hides /usr/local/bin/wine for this boot only)
#   bootlog      Enable Boot Logging: a normal start with automatic logon, and ntbtlog.txt gains driver lines
#   startnormal  Start WineExplorer Normally: a normal start with automatic logon, timers and networking, which the
#                start-up check does not count
#   oschoices    Return to OS Choices Menu (work order D3): a restart into the loader's own menu (vintner next-boot
#                menu); Enter there starts normally
#   normal       no key: automatic logon, the boot-mode generator wrote nothing, and the start-up check counted it
#   lastgood     Last Known Good Configuration (work order D3) from a generation that started successfully: with a
#                control set saved by a start that counted (a normal start first if there is none), the test adds a
#                key to the machine registry's control set and a Software key on the stopped disk; after the Last
#                Known Good start (no restart) the first is gone, the second is still there, and the replaced control
#                set is the failed copy
#   vfailed      "did not start successfully": with the logon screen held back 90 seconds for this phase, a reset
#                before the logon screen, then the next ordinary start shows the screen with its countdown; an arrow
#                key stops it, and Safe Mode with Command Prompt restarts into the variant with no menu; the next
#                start shows the screen again, its countdown starts normally and counts, and the start after shows none
#   lastgoodgen  last in the list: a generation 2 that never starts successfully (published from a changed variant),
#                then Last Known Good from its Advanced Options variant restarts into generation 1 (vintner next-boot
#                previous-good) and puts the registry's control set back
#   accounts     the Administrator account and hidden accounts (work order C4, C7; needs this branch's Wine, so
#                WEX_TEST_WINE): the Safe Mode Welcome screen shows Administrator and not the test
#                account hidden through SpecialAccounts\UserList, and Administrator logs on; then, in a normal start,
#                the logon screen shows neither
#   waylandsafe  Safe Mode in the Wayland session, chosen on the Welcome screen (work order A3): the message box, the
#                desktop, WEXMODE=safe, and the session runs under wexcomp. Only when named.
#   waylandcmd   Safe Mode with Command Prompt in the Wayland session: the recovery console runs in wexcomp (through
#                XWayland), with no wineserver. Only when named.
#                Choosing a session makes LightDM save it as the account's own, so these two phases leave the test
#                account's AccountsService file and .dmrc out of the record.
# Provisioning also gives the Administrator account a random test password ($OUT/admin-password) and adds the
# account wexhidden ("Hidden Test", no password) with a value of 0 under SpecialAccounts\UserList in the logon
# prefix's registry.
# Before and after every boot it records checksums of every file and link under /etc and /var/lib/AccountsService
# and of each account's .dmrc. A boot that changes any of them fails. Every Safe Mode start must add driver lines
# to ntbtlog.txt (work order C6). Every boot also reports the guest's disk use:
# a warning from 80%, a failure from 95%.
#
#   safe-mode.sh SOURCE_VM TEST_VM [PHASE...]          (default: safe to lastgoodgen, in the order above)
#
# SOURCE_VM is an installed test disk under $VMTEST_DIR, for example from install-unattended.sh, and is never
# written. TEST_VM is replaced by a new overlay of it, which is kept for inspection; an existing TEST_VM disk that
# is not an overlay of SOURCE_VM is refused.
#   WEX_TEST_FIRMWARE=uefi        boot with OVMF (default bios, SeaBIOS): the firmware SOURCE_VM was installed with
#   WEX_SAFEMODE_FILES            how the overlay gets this branch's Safe Mode; either way one provisioning boot then
#                                 sets up the test accounts:
#                   installed     (default) SOURCE_VM already has this branch's packages (an ISO install)
#                   branch        copies this checkout's Safe Mode files onto the overlay where the package installs
#                                 them, and runs the package postinst's Safe Mode part in the guest. Files that Vintner
#                                 Boot reads only when it publishes a generation (installer/vintner/) stay as
#                                 installed.
#   WEX_TEST_WINE                 with branch: a .tar.gz of this branch's Wine (make install prefix=/usr/local into
#                                 a staging directory, packed from there), unpacked over / in the provisioning boot
#                                 before the Safe Mode files are set up. It tests Wine-side changes (logonui.exe,
#                                 wexhost) without packaging, which downloads third-party assets. With installed the
#                                 packages carry this branch's Wine.
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
# lastgoodgen last: it leaves generation 2 on boot storage, which later starts would boot
[ ${#PHASES[@]} -gt 0 ] \
    || PHASES=(safe safenet safecmd nowine bootlog startnormal oschoices normal lastgood vfailed lastgoodgen)
for name in "$SOURCE" "$TEST"; do
    [[ "$name" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid test VM name: $name"; exit 2; }
done
[ "$SOURCE" != "$TEST" ] || { echo "TEST_VM must differ from SOURCE_VM"; exit 2; }
for phase in "${PHASES[@]}"; do
    case "$phase" in
        safe|safenet|safecmd|nowine|bootlog|startnormal|oschoices|normal|lastgood|lastgoodgen|vfailed|accounts|waylandsafe|waylandcmd) ;;
        *) echo "Unknown phase: $phase"; exit 2 ;;
    esac
done
FIRMWARE=${WEX_TEST_FIRMWARE:-bios}
case "$FIRMWARE" in
    bios) START_ARGS=() ;;
    uefi) START_ARGS=(--efi) ;;
    *) echo "Invalid firmware: $FIRMWARE"; exit 2 ;;
esac
FILES=${WEX_SAFEMODE_FILES:-installed}
case "$FILES" in branch|installed) ;; *) echo "Invalid WEX_SAFEMODE_FILES: $FILES"; exit 2 ;; esac
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
MACHINE_REG=/var/lib/wineexplorer/machine/system.reg
LASTGOOD_DIR=/var/lib/wineexplorer/lastgood
NBD=

# Where the wineexplorer-session package installs the Safe Mode files (installer/package-wine.sh; the unit test
# scripts/test_safemode.py checks that this list matches the package). Mode, source, destination.
SAFE_MODE_FILES=(
    "755 scripts/wex-bootmode.sh usr/local/bin/wex-bootmode"
    "755 scripts/wex-safe-terminal.sh usr/local/bin/wex-safe-terminal.sh"
    "755 scripts/wex-greeter.sh usr/local/bin/wex-greeter.sh"
    "755 scripts/wine-explorer-session.sh usr/local/bin/wine-explorer-session.sh"
    "755 scripts/wine-explorer-session-common.sh usr/local/bin/wine-explorer-session-common.sh"
    "755 scripts/wine-explorer-wayland-session.sh usr/local/bin/wine-explorer-wayland-session.sh"
    "755 scripts/wex-safemode-pam-check.sh usr/local/sbin/wex-safemode-pam-check.sh"
    "755 scripts/wex-advanced-options.py usr/local/sbin/wex-advanced-options"
    "755 scripts/wex-bootlog.sh usr/local/sbin/wex-bootlog"
    "755 scripts/wex-startup-check.sh usr/local/sbin/wex-startup-check"
    "755 scripts/wex-lastgood.sh usr/local/sbin/wex-lastgood"
    "755 installer/systemd/wex-bootmode-generator usr/lib/systemd/system-generators/wex-bootmode-generator"
    "644 installer/systemd/wex-advanced-options.target lib/systemd/system/wex-advanced-options.target"
    "644 installer/systemd/wex-advanced-options.service lib/systemd/system/wex-advanced-options.service"
    "644 installer/systemd/wex-safemode.target lib/systemd/system/wex-safemode.target"
    "644 installer/systemd/wex-safenet.target lib/systemd/system/wex-safenet.target"
    "644 installer/systemd/wex-bootlog.service lib/systemd/system/wex-bootlog.service"
    "644 installer/systemd/wex-startup-check.service lib/systemd/system/wex-startup-check.service"
    "644 installer/systemd/wex-bootmode-record.service lib/systemd/system/wex-bootmode-record.service"
    "644 installer/systemd/wex-failure-check.service lib/systemd/system/wex-failure-check.service"
    "644 installer/systemd/wex-failure-screen.service lib/systemd/system/wex-failure-screen.service"
    "644 installer/systemd/wex-plymouth-quit.service lib/systemd/system/wex-plymouth-quit.service"
    "644 installer/systemd/wexprefetchd.tmpfiles usr/lib/tmpfiles.d/wexprefetchd.conf"
    "644 installer/systemd/wexprefetchd.service lib/systemd/system/wexprefetchd.service"
)

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
    driver_lines "$MNT$NTBTLOG" > "$1.ntbtlog" 2>/dev/null || echo 0 > "$1.ntbtlog"
    # FILE.lastgood: 1 when the menu offers Last Known Good (work order D3), which moves the items below it
    if [ -x "$MNT/usr/local/sbin/wex-lastgood" ] && [ -s "$MNT$LASTGOOD_DIR/controlset.reg" ]; then
        echo 1
    else
        echo 0
    fi > "$1.lastgood"
    detach
    return $rc
}
lkg() { cat "$OUT/before-$PHASE.txt.lastgood" 2>/dev/null || echo 0; }

driver_lines() {   # driver_lines FILE: ntbtlog.txt's "Loaded driver" lines; UTF-16 with a BOM (C6), plain text before
    if [ "$(od -An -N2 -tx1 "$1" 2>/dev/null | tr -d ' \n')" = fffe ]; then
        iconv -f UTF-16 -t UTF-8 "$1" | grep -c 'Loaded driver'
    else
        grep -c 'Loaded driver' "$1"
    fi
}

# ---- one boot per phase; fail() ends the phase (each phase runs in a subshell) ----

fail() {
    echo "FAIL ($PHASE): $*"
    "${V[@]}" shot "$TEST" "$OUT/$PHASE-fail.png" >/dev/null 2>&1
    "${V[@]}" text "$TEST" 2>/dev/null | head -n 30
    # The VM is then stopped, not shut down: Alt+SysRq+W first logs the tasks that are blocked and where (a menu that
    # never showed: plymouthd, 2026-11-11), and Alt+SysRq+S writes this boot's journal to the disk, where journalctl
    # --directory can read it afterwards (the overlay allows every SysRq function, 90-wex-test-sysrq.conf)
    if running "$STATE/disk.qcow2"; then
        "${V[@]}" key "$TEST" alt-sysrq-w >/dev/null 2>&1 && sleep 2
        "${V[@]}" key "$TEST" alt-sysrq-s >/dev/null 2>&1 && sleep 3
    fi
    exit 1
}
gsh() { "${V[@]}" sh "$TEST" "$USER_NAME" "$PWFILE" "$1" --timeout "${2:-60}"; }
start_vm() { "${V[@]}" start "$TEST" "${START_ARGS[@]}" >/dev/null || fail "QEMU did not start"; }
shot() { "${V[@]}" shot "$TEST" "$OUT/$PHASE-$1.png" >/dev/null; }
screen_sum() { "${V[@]}" shot "$TEST" >/dev/null 2>&1; md5sum < "$STATE/screen.ppm"; }
wait_text() { "${V[@]}" wait "$TEST" "$1" --timeout "$2" >/dev/null; }

open_menu() {
    # Vintner Boot's menu shows while Shift is held on BIOS (its GRUB's keystatus --shift, then no timeout) and after a
    # key press on UEFI (systemd-boot). Each generation's variants follow it, and the menu highlights the generation,
    # so the Advanced Options variant is one item down. Its start counts nothing (Vintner Boot's docs/variants.md).
    # GRUB checks Shift within 2 seconds of power-on, so the VM starts paused and the key is held from its first
    # instruction (vmtest hold --cont); systemd-boot comes later on OVMF.
    local try
    for try in 1 2; do
        "${V[@]}" start "$TEST" "${START_ARGS[@]}" --paused >/dev/null || fail "QEMU did not start"
        if [ "$FIRMWARE" = bios ]; then
            "${V[@]}" hold "$TEST" shift 6 --cont >/dev/null 2>&1
        else
            "${V[@]}" hold "$TEST" f8 25 --cont >/dev/null 2>&1   # 15 seconds once missed systemd-boot
        fi
        if wait_text "Advanced Options" 30; then
            shot loader
            move_down 1
            shot loader-variant
            "${V[@]}" key "$TEST" ret >/dev/null
            # The menu shows within seconds. In a few starts the screen stayed black for about three minutes
            # (2026-11-10/11), with nothing in the journal meanwhile: then log every task's state and kernel stack
            # (SysRq T) while it lasts, and wait on.
            if ! wait_text "Advanced Options Menu" 30; then
                echo "  WARNING: no menu after 30 seconds; tasks logged (SysRq T, $(date +%T))"
                shot slow-menu
                "${V[@]}" key "$TEST" alt-sysrq-t >/dev/null 2>&1
            fi
            if ! wait_text "Advanced Options Menu" 150; then
                # The menu redraws the lines a key changes: if Down shows them, it runs but its screen was not
                # visible (seen once on BIOS, 2026-11-10)
                shot no-menu
                "${V[@]}" key "$TEST" down >/dev/null
                if wait_text "Safe Mode" 15; then
                    shot after-down
                    fail "the menu ran, but showed nothing until a key was pressed"
                fi
                fail "the Advanced Options variant did not start the menu"
            fi
            sleep 3
            shot menu
            VIA_MENU=1
            [ "$(highlight_row)" = "$ROW_NORMALLY" ] || fail "the menu did not open on Start WineExplorer Normally"
            return 0
        fi
        [ "$try" = 2 ] && fail "no Vintner Boot menu with the Advanced Options variant"
        shot no-loader-menu
        echo "  (Vintner Boot's menu did not show; starting again)"
        "${V[@]}" stop "$TEST" >/dev/null 2>&1
        wait_off 60 || fail "the VM did not stop"
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

# The Advanced Options menu's rows (scripts/wex-advanced-options.py, laid out as XP's, work order C5). The menu opens
# on Start Normally and wraps round, so pressing Down until the highlight bar is on a row gets there whatever presses
# were lost (one was, on OVMF, 2026-11-11). The bar's row comes from a screenshot (highlight-row.py).
ROW_SAFE=4 ROW_SAFENET=5 ROW_SAFECMD=6 ROW_BOOTLOG=8 ROW_LASTGOOD=10 ROW_NORMALLY=15 ROW_OSCHOICES=17

highlight_row() { "${V[@]}" shot "$TEST" >/dev/null 2>&1 && python3 "$HERE/highlight-row.py" "$STATE/screen.ppm"; }

select_row() {   # select_row ROW: Down until the highlight bar is on ROW
    local _ wait row before
    row=$(highlight_row)
    for _ in $(seq 1 16); do
        [ "$row" = "$1" ] && return 0
        before=$row
        "${V[@]}" key "$TEST" down >/dev/null
        for wait in 1 2 3 4; do   # until the bar moves; a press it does not show is made again
            sleep 0.5
            row=$(highlight_row)
            [ "$row" != "$before" ] && break
        done
    done
    fail "the highlight did not reach row $1 (last on row ${row:-none})"
}

menu_choice() {   # menu_choice ROW LABEL: choose the item on ROW, check the OS list shows LABEL, start
    local try before screen
    open_menu
    select_row "$1"
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
                   # (also the variant: the Advanced Options one after the menu, else none)
    local line miss disk variant
    line=$(gsh "$STATE_CMD" | grep '^mode=' | tail -n 1)
    echo "  $line"
    [ -n "$line" ] || fail "no answer on the serial console"
    if [ -n "${VIA_MENU:-}" ]; then variant=variant=advanced; else variant=variant=none; fi
    miss=$(kv_check "$line" "$@" "$variant") || fail "expected $miss: $line"
    # a full test disk makes every later result unreliable (VMs have run out of space before)
    disk=${line##*disk=}
    disk=${disk%% *}
    case "$disk" in ''|*[!0-9]*) fail "no disk use in: $line" ;; esac
    if [ "$disk" -ge 95 ]; then fail "the guest's root file system is ${disk}% full"; fi
    if [ "$disk" -ge 80 ]; then echo "  WARNING: the guest's root file system is ${disk}% full"; fi
}

expect_screen() {   # expect_screen WxH|!WxH: the VM's screen size, from a screenshot's PPM header. Safe Mode runs at
                    # 640x480 (work order C3) on images whose greeter sets it; older images are reported, not failed.
    local size
    if [ "$1" = 640x480 ] && ! gsh 'grep -q -- "--mode 640x480" /usr/local/bin/wex-greeter.sh && echo c3=yes' | grep -q '^c3=yes'; then
        echo "  (this image's greeter does not set 640x480 in Safe Mode)"
        return 0
    fi
    "${V[@]}" shot "$TEST" >/dev/null 2>&1 || fail "no screenshot"
    size=$(head -n 2 "$STATE/screen.ppm" | tail -n 1 | tr ' ' x)
    echo "  screen ${size:-unknown}"
    case "$1" in
        !*) [ "$size" != "${1#!}" ] || fail "the screen is ${1#!}" ;;
        *) [ "$size" = "$1" ] || fail "the screen is ${size:-unknown}, expected $1" ;;
    esac
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

expect_check() {   # the start-up check (scripts/wex-startup-check.sh) finished without error this boot. Its verdict
                   # is in the system journal, which the test user cannot read; whether Vintner Boot counted the
                   # start shows at the next one (vfailed, lastgoodgen).
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
    menu_choice "$ROW_SAFE" "Safe Mode"
    logon_screen
    expect_state mode=safe default=wex-safemode.target nm=inactive timers=0 cron=inactive prefetch!=active
    expect_logonui_process
    expect_screen 640x480
    logon
    safe_desktop 400
    expect_screen 640x480
    expect_session_mode safe
    expect_safeboot 1
    programs_32_64
    shutdown_vm
}

phase_safenet() {
    menu_choice "$ROW_SAFENET" "Safe Mode with Networking"
    logon_screen
    expect_state mode=safenet default=wex-safenet.target nm=active timers=0 cron=inactive prefetch!=active
    logon
    safe_desktop 400
    expect_screen 640x480
    expect_session_mode safenet
    expect_safeboot 2
    shutdown_vm
}

phase_safecmd() {
    menu_choice "$ROW_SAFECMD" "Safe Mode with Command Prompt"
    logon_screen
    expect_state mode=safecmd default=wex-safemode.target nm=inactive timers=0 cron=inactive prefetch!=active
    expect_screen 640x480
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
    # XP's shape (work order C6): 80 columns and at least 30 rows (36 at 640x480). The size of the shell's terminal;
    # images whose console predates the shape are reported, not failed.
    out=$(gsh 'grep -q -- -rightbar /usr/local/bin/wex-safe-terminal.sh && echo shape=yes; for p in $(pgrep -u "$USER" -f "bash --rcfile"); do t=$(readlink /proc/$p/fd/0); case "$t" in /dev/pts/*) echo "console=$(stty -F "$t" size | tr " " x)"; break ;; esac; done' \
        | grep -E '^(shape|console)=' | tr '\n' ' ')
    echo "  $out"
    case "$out" in
        *"shape=yes"*)
            case "$out" in
                *"console="[3-9][0-9]x80" "*|*"console="[1-9][0-9][0-9]x80" "*) ;;
                *) fail "the recovery console is not 80 columns by 30 rows or more: $out" ;;
            esac ;;
        *) echo "  (this image's recovery console predates its XP shape)" ;;
    esac
}

wayland_session() {   # on the Welcome screen: choose "WineExplorer (Wayland)" in logonui.exe's session list
    # The list's small text: OCR reads "WineExplorer" but not always "(X11)", and it is the only such word there
    "${V[@]}" clicktext "$TEST" "WineExplorer" --timeout 15 >/dev/null 2>&1 \
        || fail "no session list on the Welcome screen"
    "${V[@]}" key "$TEST" down ret >/dev/null
    wait_text "Wayland" 10 || fail "the Wayland session could not be chosen"
    shot session
}

expect_compositor() {   # the logged-on session runs under wexcomp, the Wayland session's compositor
    local out
    out=$(gsh 'echo compositors=$(pgrep -c -u '"$USER_NAME"' -x wexcomp)' | grep '^compositors=')
    echo "  $out"
    case "$out" in "compositors="[1-9]*) ;; *) fail "the Wayland session's compositor is not running: $out" ;; esac
}

phase_waylandsafe() {   # work order A3
    menu_choice "$ROW_SAFE" "Safe Mode"
    logon_screen
    wayland_session
    logon
    safe_desktop 400
    expect_session_mode safe
    expect_compositor
    shutdown_vm
}

phase_waylandcmd() {   # work order A3: the recovery console replaces the shell inside the compositor
    menu_choice "$ROW_SAFECMD" "Safe Mode with Command Prompt"
    logon_screen
    wayland_session
    logon
    recovery_console
    expect_compositor
    shutdown_vm
}

phase_nowine() {   # /usr/local/bin/wine is hidden for this boot (the main loop does it)
    menu_choice "$ROW_SAFECMD" "Safe Mode with Command Prompt"
    logon_screen_gtk
    expect_state mode=safecmd default=wex-safemode.target nm=inactive timers=0 cron=inactive prefetch!=active
    logon_gtk
    recovery_console
    shutdown_vm
}

phase_bootlog() {
    local before after='' _
    before=$(cat "$OUT/before-$PHASE.txt.ntbtlog")
    menu_choice "$ROW_BOOTLOG" "Enable Boot Logging"
    desktop 600
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    for _ in $(seq 1 12); do
        after=$(gsh "iconv -f UTF-16 -t UTF-8 $NTBTLOG | grep -c 'Loaded driver'" | grep -x '[0-9]*' | tail -n 1)
        [ -n "$after" ] && [ "$after" -gt "$before" ] && break
        sleep 5
    done
    echo "  ntbtlog.txt driver lines: $before before, ${after:-none} after"
    [ -n "$after" ] && [ "$after" -gt "$before" ] || fail "ntbtlog.txt gained no driver lines"
    expect_session_mode normal
    shutdown_vm
}

phase_startnormal() {
    open_menu   # which opens on Start Normally
    select_row "$ROW_NORMALLY"
    shot highlight
    "${V[@]}" key "$TEST" ret >/dev/null
    desktop 600
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    expect_check
    expect_session_mode normal
    shutdown_vm
}

phase_oschoices() {   # work order D3: Return to OS Choices Menu
    open_menu
    select_row "$ROW_OSCHOICES"
    shot highlight
    "${V[@]}" key "$TEST" ret >/dev/null
    # vintner next-boot menu, then a restart: the loader's own menu shows for 30 seconds with no key held. Its
    # recovery entries tell it from the Advanced Options menu, which also says "Advanced Options".
    wait_text "Recovery Console" 180 || fail "no boot loader menu after Return to OS Choices Menu"
    shot loader-menu
    "${V[@]}" key "$TEST" ret >/dev/null   # the generation, highlighted first
    VIA_MENU=   # this start is the generation's own, not the variant's
    desktop 600
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    expect_check
    expect_session_mode normal
    shutdown_vm
}

phase_normal() {
    local out
    start_vm
    desktop 600
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    expect_screen '!640x480'   # each start's X server begins at its own mode: Safe Mode's 640x480 is not kept
    out=$(gsh 'echo early=$(ls -A /run/systemd/generator.early 2>/dev/null | grep -c -E "^(default|timers)\.target$")' | grep '^early=')
    [ "$out" = early=0 ] || fail "the boot-mode generator wrote units in a normal start ($out)"
    expect_check
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

# The two changes lastgood_change makes: a key in the control set, as a driver's settings added after the saved
# copy, and a Software key, as a program installed meanwhile. (Wine writes no block for a key without values, such as
# System\ControlSet001\Control, so a value cannot be added to one reliably.) A pattern for grep, and the Software key
# as Wine writes it, one per phase: Last Known Good keeps it, so lastgood's would still be there in lastgoodgen.
LKG_CONTROL_KEY='^\[System\\\\ControlSet001\\\\Control\\\\WexLkgTest\]'
lkg_software_key() { printf '%s' "[Software\\\\WexLkgSoftware_$PHASE]"; }

lastgood_change() {   # on the stopped disk, after the saved copy: the two keys above
    local reg=$MNT$MACHINE_REG rc
    attach rw || return 1
    if [ ! -s "$MNT$LASTGOOD_DIR/controlset.reg" ]; then
        echo "no control set was saved"
        detach
        return 1
    fi
    # (the Software key through the environment: awk -v would turn its double backslash into one)
    cp -a "$reg" "$reg.test-tmp" && LKG_SOFTWARE="$(lkg_software_key)" awk '
        { print }
        END {
            print ""
            print "[System\\\\ControlSet001\\\\Control\\\\WexLkgTest] 1794264452"
            print "\"Marker\"=\"changed\""
            print ""
            print ENVIRON["LKG_SOFTWARE"] " 1794264452"
            print "\"Kept\"=\"yes\""
        }
    ' "$reg" > "$reg.test-tmp" && mv -f "$reg.test-tmp" "$reg"
    rc=$?
    echo "  changed on the disk: control=$(grep -c "$LKG_CONTROL_KEY" "$reg") software=$(grep -cF "$(lkg_software_key)" "$reg")"
    detach
    return $rc
}

lastgood_result() {   # on the stopped disk: control=, software=, failed= and saved= counts of the two keys
    local reg=$MNT$MACHINE_REG out
    attach ro || return 1
    out="control=$(grep -c "$LKG_CONTROL_KEY" "$reg")"
    out+=" software=$(grep -cF "$(lkg_software_key)" "$reg")"
    out+=" failed=$(grep -c "$LKG_CONTROL_KEY" "$MNT$LASTGOOD_DIR/failed-controlset.reg" 2>/dev/null)"
    out+=" saved=$(grep -c "$LKG_CONTROL_KEY" "$MNT$LASTGOOD_DIR/controlset.reg" 2>/dev/null)"
    detach
    echo "$out"
}

phase_lastgood() {   # work order D3
    local out
    if [ "$(lkg)" != 1 ]; then
        echo "  no control set saved yet: a normal start first"
        start_vm
        desktop 600
        expect_check
        shutdown_vm
        wait_off 60 || fail "the VM did not power off"
    fi
    lastgood_change || fail "cannot change the machine registry on the disk"
    [ "$(attach ro && grep -c "$LKG_CONTROL_KEY" "$MNT$MACHINE_REG"; detach)" = 1 ] \
        || fail "the test key is not in the machine registry's control set"
    # The generation has started successfully, so Last Known Good puts the control set back and this start goes on
    menu_choice "$ROW_LASTGOOD" "Last Known Good"
    desktop 600
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    expect_check   # a start through the menu does not count, so it saves nothing either
    expect_session_mode normal
    programs_32_64
    shutdown_vm
    wait_off 60 || fail "the VM did not power off"
    out=$(lastgood_result) || fail "cannot read the machine registry on the disk"
    echo "  after Last Known Good: $out"
    [ "$out" = "control=0 software=1 failed=1 saved=0" ] \
        || fail "expected the control set back, the Software key kept and the change in the failed copy: $out"
}

phase_lastgoodgen() {   # work order D3 with Vintner Boot: from a generation that has not started successfully,
                        # Last Known Good also restarts into the previous good one (vintner next-boot previous-good)
    local out status
    if [ "$(lkg)" != 1 ]; then
        echo "  no control set saved yet: a normal start first"
        start_vm
        desktop 600
        expect_check
        shutdown_vm
        wait_off 60 || fail "the VM did not power off"
    fi
    # Generation 2: a changed variant changes the publisher's inputs, and a unit publishes at the next start (generation
    # 1's, which counts as usual) and powers off. Generation 2 is then started only through its variant, which counts
    # nothing, so it never starts successfully. The publish waits for vintner-bless and tries again a few times: it
    # mounts boot storage read-write, which fails while something else has it mounted read-only.
    attach rw || fail "cannot mount the overlay"
    cp -a "$MNT/etc/vintner/variants.d/advanced.conf" "$MNT/root/wex-test-advanced.conf"
    sed -i 's/^tokens=.*/& wextest=1/' "$MNT/etc/vintner/variants.d/advanced.conf"
    printf '%s\n' '#!/bin/sh' 'rc=1' 'for try in 1 2 3 4 5; do' \
        '    vintner publish --kernel "$(uname -r)" >> /root/wex-test-publish.log 2>&1 && { rc=0; break; }' \
        '    sleep 5' 'done' 'echo "publish=$rc" >> /root/wex-test-publish.log' 'systemctl --no-block poweroff' \
        > "$MNT/root/wex-test-publish.sh"
    printf '%s\n' '[Unit]' 'Description=Test: publish a generation' 'After=multi-user.target vintner-bless.service' \
        '[Service]' 'Type=oneshot' 'ExecStart=/bin/sh /root/wex-test-publish.sh' '[Install]' 'WantedBy=multi-user.target' \
        > "$MNT/etc/systemd/system/wex-test-publish.service"
    ln -sf /etc/systemd/system/wex-test-publish.service "$MNT/etc/systemd/system/multi-user.target.wants/"
    detach
    start_vm
    wait_off 900 || fail "the start that publishes generation 2 did not power off"
    # Put the disk back as it was, then add a unit that records `vintner status --json` at every start from now on
    attach rw || fail "cannot mount the overlay"
    cp "$MNT/root/wex-test-publish.log" "$OUT/lastgoodgen-publish.log" 2>/dev/null
    out=$(grep '^publish=' "$MNT/root/wex-test-publish.log" 2>/dev/null)
    mv -f "$MNT/root/wex-test-advanced.conf" "$MNT/etc/vintner/variants.d/advanced.conf"
    rm -f "$MNT/etc/systemd/system/multi-user.target.wants/wex-test-publish.service" \
        "$MNT/etc/systemd/system/wex-test-publish.service" "$MNT/root/wex-test-publish.sh" "$MNT/root/wex-test-publish.log"
    if [ "$out" = publish=0 ]; then
        printf '%s\n' '[Unit]' 'Description=Test: record vintner status' 'After=multi-user.target vintner-bless.service' \
            '[Service]' 'Type=oneshot' 'ExecStart=/bin/sh -c "vintner status --json > /root/wex-test-status.json"' \
            '[Install]' 'WantedBy=multi-user.target' > "$MNT/etc/systemd/system/wex-test-status.service"
        ln -sf /etc/systemd/system/wex-test-status.service "$MNT/etc/systemd/system/multi-user.target.wants/"
    fi
    detach
    [ "$out" = publish=0 ] || fail "vintner publish failed (see $OUT/lastgoodgen-publish.log)"
    lastgood_change || fail "cannot change the machine registry on the disk"
    # The loader highlights generation 2, and its Advanced Options variant is one down. Last Known Good is offered
    # (generation 1), and once chosen the machine restarts into generation 1 with no menu.
    menu_choice "$ROW_LASTGOOD" "Last Known Good"
    VIA_MENU=   # the start that follows is generation 1's own
    desktop 900
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    expect_check
    shutdown_vm
    wait_off 60 || fail "the VM did not power off"
    attach rw || fail "cannot mount the overlay"
    cp "$MNT/root/wex-test-status.json" "$OUT/lastgoodgen-status.json" 2>/dev/null
    rm -f "$MNT/etc/systemd/system/multi-user.target.wants/wex-test-status.service" \
        "$MNT/etc/systemd/system/wex-test-status.service" "$MNT/root/wex-test-status.json"
    detach
    status=$(python3 -c 'import json, sys
s = json.load(open(sys.argv[1]))
b = s.get("booted") or {}
states = {g["generation"]: g["state"] for g in s.get("generations", [])}
print("booted=%s:%s newest_good=%s gen2=%s" % (b.get("kind"), b.get("generation"), s.get("newest_good"), states.get(2)))' \
        "$OUT/lastgoodgen-status.json" 2>&1)
    echo "  after Last Known Good: $status"
    case "$status" in
        "booted=generation:1 newest_good=1 gen2="*) [ "${status##*gen2=}" != GOOD ] || fail "generation 2 became GOOD: $status" ;;
        *) fail "expected the start after Last Known Good to be generation 1's: $status" ;;
    esac
    out=$(lastgood_result) || fail "cannot read the machine registry on the disk"
    echo "  registry: $out"
    [ "$out" = "control=0 software=1 failed=1 saved=0" ] \
        || fail "expected the control set back, the Software key kept and the change in the failed copy: $out"
}

phase_vfailed() {   # "did not start successfully" (wex-failure-check.service, work order D1); the main loop holds
                    # the logon screen back for this phase (delay_logon)
    local try
    # Reset once Linux runs, before the logon screen and so before boot-complete.target, where Vintner would count the
    # start good: the loader has counted a try that never finished, as the XP capture was made (a reset during the
    # boot splash). An automatic logon follows Linux within seconds, too soon to reset reliably without the hold.
    start_vm
    sleep 15
    wait_serial 150 || fail "the serial console did not answer"
    "${V[@]}" reset "$TEST" >/dev/null || fail "the Reset button did not work"
    echo "  reset before the logon screen"
    failure_screen
    # Up stops the countdown and highlights Last Known Good, or, while nothing can be gone back to, skips it, grayed,
    # to Safe Mode with Command Prompt. That choice restarts into the Advanced Options variant, which goes straight
    # to Safe Mode with Command Prompt with no menu.
    for try in 1 2 3; do
        "${V[@]}" key "$TEST" up >/dev/null
        sleep 1.5
        [ -z "$(countdown)" ] && break
        [ "$try" = 3 ] && fail "Up did not stop the countdown"
    done
    if [ "$(lkg)" = 1 ]; then
        "${V[@]}" key "$TEST" up >/dev/null
        sleep 1
    fi
    shot safecmd-highlighted
    "${V[@]}" key "$TEST" ret >/dev/null
    ! wait_text "Advanced Options Menu" 60 || fail "the variant showed the menu instead of starting the mode chosen"
    VIA_MENU=1   # the variant's start
    logon_screen
    expect_state mode=safecmd default=wex-safemode.target nm=inactive timers=0 cron=inactive prefetch!=active
    shutdown_vm
    wait_off 60 || fail "the VM did not power off"
    # The start that showed the screen did not finish either, so the next ordinary start shows it again; left alone,
    # its countdown starts normally, and that start counts
    VIA_MENU=
    start_vm
    failure_screen
    desktop 600
    expect_state mode=normal default=graphical.target nm=active timers!=0 cron=active prefetch!=inactive
    expect_check
    shutdown_vm
    wait_off 60 || fail "the VM did not power off"
    # ... so the start after it shows no screen (the logon screen is still 90 seconds late)
    start_vm
    ! wait_text "did not start successfully" 80 || fail "the failure screen came back after a start that counted"
    desktop 600
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
    menu_choice "$ROW_SAFE" "Safe Mode"
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
# every SysRq function, so that fail() can log blocked tasks (Debian's default, 438, allows sync but no task dumps)
printf '%s\n' '# installer/tests/safe-mode.sh' 'kernel.sysrq = 1' > "$MNT/etc/sysctl.d/90-wex-test-sysrq.conf" \
    || die "cannot allow SysRq"
if [ -n "$PROBE_DIR" ]; then
    install -m 755 "$PROBE_DIR/safeboot-probe32.exe" "$PROBE_DIR/safeboot-probe64.exe" "$MNT$PROBES/" \
        || die "cannot install the Safe Mode probes"
fi
ADMIN_PWFILE=$OUT/admin-password
STEP=
if [ "$FILES" = branch ]; then
    for entry in "${SAFE_MODE_FILES[@]}"; do
        read -r mode src dst <<< "$entry"
        install -D -m "$mode" "$REPO/$src" "$MNT/$dst" || die "cannot install $src"
    done
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
# Vintner Boot counted this start as a try; powered off before boot-complete.target, it would stay a failed start and
# bring the failure screen one start early (vfailed), on the generation's last try. Count it as the start it was.
/usr/sbin/vintner bless; echo "bless=$?"
sync
systemctl --no-block poweroff
EOF
} > "$MNT/root/wex-test-provision.sh"
chmod 755 "$MNT/root/wex-test-provision.sh"
printf '%s\n' '[Unit]' 'Description=Safe Mode test provisioning (installer/tests/safe-mode.sh)' 'After=local-fs.target' \
    'Before=display-manager.service' '' '[Service]' 'Type=oneshot' 'ExecStart=/root/wex-test-provision.sh' \
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
[ ! -e "$MNT/boot/grub/grub.cfg" ] || { detach; die "a Vintner Boot install has a GRUB configuration"; }
[ -f "$MNT/etc/vintner/variants.d/advanced.conf" ] \
    || { detach; die "no Advanced Options variant (/etc/vintner/variants.d/advanced.conf)"; }
provisioned 'bless=0' "Vintner Boot did not count the provisioning start"
provisioned 'adminpw=0' "the Administrator account is missing or got no password"
# the logon screen reads the logon prefix's own hive, or the machine hive once the system role serves it (C7)
grep -qE '^userlist=/var/lib/(lightdm/[.]wine|wineexplorer/machine)/system[.]reg$' "$OUT/provision.log" \
    || { detach; die "neither the logon prefix's nor the machine's registry got the SpecialAccounts\\UserList value (see $OUT/provision.log)"; }
grep -q 'wex-safemode-pam-check.sh autologin' "$MNT/etc/pam.d/lightdm-autologin" \
    || { detach; die "/etc/pam.d/lightdm-autologin does not consult the boot mode"; }
[ -L "$MNT/etc/systemd/system/multi-user.target.wants/wex-startup-check.service" ] \
    || { detach; die "wex-startup-check.service is not enabled"; }
detach

# ---- the phases ----

LOGON_DELAY=usr/lib/systemd/system/lightdm.service.d/wex-test-delay.conf
delay_logon() {   # delay_logon on|off: LightDM starts 90 seconds late for the vfailed phase (outside the records)
    attach rw || return 1
    if [ "$1" = on ]; then
        mkdir -p "$(dirname "$MNT/$LOGON_DELAY")" &&
            printf '%s\n' '# installer/tests/safe-mode.sh, vfailed phase only' '[Service]' 'ExecStartPre=/bin/sleep 90' \
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

overall=0
summary=()
for PHASE in "${PHASES[@]}"; do
    snapshot "$OUT/before-$PHASE.txt" || die "cannot record the configuration before $PHASE"
    if [ "$PHASE" = nowine ]; then hide_wine on || die "cannot hide /usr/local/bin/wine"; fi
    if [ "$PHASE" = vfailed ]; then delay_logon on || die "cannot hold the logon screen back"; fi
    echo "=== $PHASE ($(date +%T))"
    ( "phase_$PHASE" ) 2>&1 | tee "$OUT/$PHASE.log"
    rc=${PIPESTATUS[0]}
    if [ "$rc" -ne 0 ]; then
        "${V[@]}" stop "$TEST" >/dev/null 2>&1 || true
    fi
    wait_off 60 || die "$TEST did not stop after $PHASE"
    if [ "$PHASE" = nowine ]; then hide_wine off || die "cannot put /usr/local/bin/wine back"; fi
    if [ "$PHASE" = vfailed ]; then delay_logon off || die "cannot remove the logon delay"; fi
    snapshot "$OUT/after-$PHASE.txt" || die "cannot record the configuration after $PHASE"
    compare=("$OUT/before-$PHASE.txt" "$OUT/after-$PHASE.txt")
    case "$PHASE" in
        wayland*)   # LightDM saves the session chosen on the Welcome screen as the account's own
            for f in "${compare[@]}"; do
                grep -vE "  (var/lib/AccountsService/users/$USER_NAME|home/$USER_NAME/\.dmrc)\$" "$f" > "$f.compared"
            done
            compare=("${compare[0]}.compared" "${compare[1]}.compared") ;;
    esac
    case "$PHASE" in
        safe|safenet|safecmd|nowine|waylandsafe|waylandcmd)   # XP logs every Safe Mode start (work order C6)
            logged=$(( $(cat "$OUT/after-$PHASE.txt.ntbtlog") - $(cat "$OUT/before-$PHASE.txt.ntbtlog") ))
            echo "  ntbtlog.txt: $logged new driver lines"
            if [ "$rc" -eq 0 ] && [ "$logged" -le 0 ]; then
                echo "FAIL ($PHASE): the Safe Mode start wrote no boot log"
                rc=1
            fi ;;
    esac
    if ! diff -u "${compare[@]}" > "$OUT/$PHASE-changes.diff"; then
        echo "FAIL ($PHASE): the boot changed files under /etc, /var/lib/AccountsService or a .dmrc:"
        grep '^[-+][^-+]' "$OUT/$PHASE-changes.diff" | head -n 20
        rc=1
    fi
    if [ "$rc" -eq 0 ]; then summary+=("PASS $PHASE"); else summary+=("FAIL $PHASE"); overall=1; fi
done

# The Welcome screen ran (any Safe Mode phase): its prefix is now lightdm's ~/.wine, moved from ~/.wine-logon if
# the image had one (docs/SAFE_MODE_WORKORDER.md C7)
case " ${PHASES[*]} " in
    *" safe "*|*" safenet "*|*" safecmd "*|*" vfailed "*|*" waylandsafe "*|*" waylandcmd "*)
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
