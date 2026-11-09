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
#   WEX_SAFEMODE_FILES=installed  SOURCE_VM already has this branch's wineexplorer-session package (an ISO install).
#                                 The default, branch, copies this checkout's Safe Mode files onto the overlay where
#                                 the package installs them, then runs the package postinst's Safe Mode part and
#                                 update-grub in the guest during one provisioning boot.
#   WEX_TEST_USER                 the account that logs on (default wineuser); it also logs on on the serial console
#   WEX_TEST_PASSWORD_FILE        its password (default: empty)
#   WEX_TEST_TILE                 its name on the Welcome screen (default: the first tile, chosen with Enter)
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
        safe|safenet|safecmd|nowine|bootlog|startnormal|normal|failed) ;;
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
case "$FILES" in branch|installed) ;; *) echo "Invalid WEX_SAFEMODE_FILES: $FILES"; exit 2 ;; esac

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

open_menu() {
    start_vm
    # During GRUB's hidden timeout F8, the menu entry's hotkey, boots the Advanced Options entry. Keys sent while the
    # firmware runs are buffered, so press it repeatedly from the start.
    local _
    for _ in $(seq 1 40); do "${V[@]}" key "$TEST" f8 >/dev/null 2>&1; sleep 0.25; done
    wait_text "Advanced Options Menu" 180 || fail "no Advanced Options menu after F8"
    sleep 3   # the menu discards the F8 presses that opened it
    shot menu
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

logon() {   # a tile, then the password box if the account has a password
    if [ -n "${WEX_TEST_TILE:-}" ]; then
        "${V[@]}" clicktext "$TEST" "$WEX_TEST_TILE" >/dev/null || fail "no tile '$WEX_TEST_TILE' on the Welcome screen"
    else
        "${V[@]}" key "$TEST" ret >/dev/null   # Enter starts the logon of the first tile
    fi
    if [ -s "$PWFILE" ]; then
        wait_text "password" 30 || fail "no password box on the Welcome screen"
        "${V[@]}" type "$TEST" "$(cat "$PWFILE")"
        "${V[@]}" key "$TEST" ret >/dev/null
    fi
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
STATE_CMD='printf "mode=%s default=%s nm=%s timers=%s cron=%s prefetch=%s disk=%s\n" "$(wex-bootmode)" "$(systemctl get-default)" "$(systemctl is-active NetworkManager.service)" "$(systemctl list-units --type=timer --state=active --no-legend | wc -l)" "$(systemctl is-active cron.service)" "$(if [ -e /lib/systemd/system/wexprefetchd.socket ]; then systemctl is-active wexprefetchd.socket; else echo absent; fi)" "$(df --output=pcent / | tail -n 1 | tr -d " %")"'

expect_state() {   # expect_state key=value|key!=value...: the guest's mode and units, read on the serial console
    local line kv disk
    line=$(gsh "$STATE_CMD" | grep '^mode=' | tail -n 1)
    echo "  $line"
    [ -n "$line" ] || fail "no answer on the serial console"
    for kv in "$@"; do
        case "$kv" in
            *!=*) case " $line " in *" ${kv%%!=*}=${kv#*!=} "*) fail "expected $kv: $line" ;; esac ;;
            *) case " $line " in *" $kv "*) ;; *) fail "expected $kv: $line" ;; esac ;;
        esac
    done
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

shutdown_vm() { "${V[@]}" shutdown "$TEST" --timeout 180 >/dev/null || fail "no clean shutdown"; }

phase_safe() {
    menu_choice 0 "Safe Mode"
    logon_screen
    expect_state mode=safe default=wex-safemode.target nm=inactive timers=0 cron=inactive prefetch!=active
    expect_logonui_process
    logon
    desktop 400
    expect_session_mode safe
    programs_32_64
    shutdown_vm
}

phase_safenet() {
    menu_choice 1 "Safe Mode with Networking"
    logon_screen
    expect_state mode=safenet default=wex-safenet.target nm=active timers=0 cron=inactive prefetch!=active
    logon
    desktop 400
    expect_session_mode safenet
    shutdown_vm
}

phase_safecmd() {
    menu_choice 2 "Safe Mode with Command Prompt"
    logon_screen
    expect_state mode=safecmd default=wex-safemode.target nm=inactive timers=0 cron=inactive prefetch!=active
    expect_pending 0
    logon
    recovery_console
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
    cat > "$MNT/root/wex-test-provision.sh" <<'EOF'
#!/bin/sh
# installer/tests/safe-mode.sh: runs once, removes itself and powers off
exec > /root/wex-test-provision.log 2>&1
sh -x /root/wex-test-safemode-postinst.sh; echo "postinst=$?"
update-grub; echo "update-grub=$?"
rm -f /etc/systemd/system/multi-user.target.wants/wex-test-provision.service /etc/systemd/system/wex-test-provision.service
sync
systemctl --no-block poweroff
EOF
    chmod 755 "$MNT/root/wex-test-provision.sh"
    cat > "$MNT/etc/systemd/system/wex-test-provision.service" <<'EOF'
[Unit]
Description=Safe Mode test provisioning (installer/tests/safe-mode.sh)
After=local-fs.target
Before=display-manager.service

[Service]
Type=oneshot
ExecStart=/root/wex-test-provision.sh
EOF
    mkdir -p "$MNT/etc/systemd/system/multi-user.target.wants"
    ln -sf /etc/systemd/system/wex-test-provision.service \
        "$MNT/etc/systemd/system/multi-user.target.wants/wex-test-provision.service"
fi
detach

if [ "$FILES" = branch ]; then
    echo "=== provisioning boot ($(date +%T))"
    "${V[@]}" start "$TEST" "${START_ARGS[@]}" >/dev/null || die "QEMU did not start"
    wait_off 900 || die "the provisioning boot did not power off"
fi
attach ro || die "cannot mount the overlay"
if [ "$FILES" = branch ]; then
    cp "$MNT/root/wex-test-provision.log" "$OUT/provision.log" 2>/dev/null
    grep -qx 'postinst=0' "$OUT/provision.log" || { detach; die "the postinst's Safe Mode part failed (see $OUT/provision.log)"; }
    grep -qx 'update-grub=0' "$OUT/provision.log" || { detach; die "update-grub failed (see $OUT/provision.log)"; }
fi
grep -q 'wexmode=menu' "$MNT/boot/grub/grub.cfg" && grep -q -- '--hotkey=f8' "$MNT/boot/grub/grub.cfg" \
    || { detach; die "grub.cfg has no Advanced Options entry with the F8 hotkey"; }
grep -q 'wex-safemode-pam-check.sh autologin' "$MNT/etc/pam.d/lightdm-autologin" \
    || { detach; die "/etc/pam.d/lightdm-autologin does not consult the boot mode"; }
grep -q 'save_env wex_boot_pending' "$MNT/boot/grub/grub.cfg" \
    || { detach; die "grub.cfg does not record unfinished starts (wex_boot_pending)"; }
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
            if [ -f "$MNT/var/lib/lightdm/.wine/system.reg" ] || [ -L "$MNT/var/lib/lightdm/.wine/system.reg" ]; then
                if [ -e "$MNT/var/lib/lightdm/.wine-logon" ]; then
                    summary+=("FAIL logon prefix (~/.wine-logon is still there)"); overall=1
                else
                    summary+=("PASS logon prefix (/var/lib/lightdm/.wine)")
                fi
            else
                summary+=("FAIL logon prefix (no /var/lib/lightdm/.wine/system.reg)"); overall=1
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
