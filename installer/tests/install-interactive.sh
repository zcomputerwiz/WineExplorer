#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# End-to-end interactive install of the ISO in a QEMU/KVM VM (scripts/vmtest.py; WSL2 or Linux, run as root for
# qemu-nbd).  Blank disk -> text setup (partition editor, quick format, copy; restart with the CD still in) ->
# GUI setup -> OOBE with two users (one with a password, one without) -> logon of the passwordless user.
#
#   install-interactive.sh ISO PASSWORD_FILE [VM_NAME]
#
# PASSWORD_FILE holds the test password used for the Administrator and the first user.  Screenshots of every step
# go to /tmp/vmtest/VM_NAME/steps/.
set -u
ISO=$1; PWFILE=$2; VM=${3:-wex-install}
[[ "$VM" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid test VM name"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
V="python3 $HERE/../../scripts/vmtest.py"
STATE=${VMTEST_DIR:-/tmp/vmtest}/$VM
STEPS=$STATE/steps
P=$(tr -d '\r\n' < "$PWFILE")
fail() { echo "FAIL: $*"; $V shot "$VM" "$STEPS/fail.png" >/dev/null; exit 1; }
step() { N=$((N + 1)); echo "--- $*"; $V shot "$VM" "$STEPS/$(printf %02d "$N")-${1// /_}.png" >/dev/null; }
N=0

$V stop "$VM" 2>/dev/null; rm -rf "$STATE"; mkdir -p "$STEPS"
$V start "$VM" --iso "$ISO" || exit 1

# Text-mode setup
$V wait "$VM" "Welcome to Setup" --timeout 180 || fail "no text setup"
step welcome; $V key "$VM" ret
$V wait "$VM" "Licensing Agreement" --timeout 30 || fail "no license"
step license; $V key "$VM" f8
$V wait "$VM" "Unpartitioned space" --timeout 30 || fail "blank disk not listed"
step partitions; $V key "$VM" c
$V wait "$VM" "create a new partition" --timeout 20 || fail "no create screen"
$V key "$VM" backspace backspace backspace backspace backspace backspace; $V type "$VM" 15000; $V key "$VM" ret
$V wait "$VM" "15000 MB" --timeout 20 || fail "partition not created"
step created; $V key "$VM" ret
$V wait "$VM" "must now be formatted" --timeout 20 || fail "no format screen"
step format; $V key "$VM" ret
$V wait "$VM" "has completed successfully" --timeout 900 || fail "copy did not finish"
step copied
# Leave the CD in: the restart must eject it or the CD must continue Setup from the disk
$V wait "$VM" "Customize" --timeout 600 || fail "GUI setup did not start after the restart"

# GUI setup
step regional; $V key "$VM" ret
$V wait "$VM" "Organization" --timeout 30 || fail "no name page"; $V type "$VM" "Wine User"; $V key "$VM" ret
$V wait "$VM" "Computer name" --timeout 30 || fail "no computer page"
$V key "$VM" tab; $V type "$VM" "$P"; $V key "$VM" tab; $V type "$VM" "$P"
step computer; $V key "$VM" ret
$V wait "$VM" "Time zone" --timeout 30 || fail "no time page"; $V key "$VM" ret
$V wait "$VM" "Networking software" --timeout 30 || fail "no network page"; $V key "$VM" ret
$V wait "$VM" "WORKGROUP" --timeout 30 || fail "no workgroup page"
step workgroup; $V key "$VM" ret

# OOBE
$V wait "$VM" "Thank you for installing" --timeout 900 || fail "OOBE did not start"
step oobe; $V key "$VM" ret
$V wait "$VM" "Automatic Updates" --timeout 30 || fail "no updates page"; $V key "$VM" ret
$V wait "$VM" "connectivity" --timeout 60 || fail "no connectivity page"; $V key "$VM" ret
$V wait "$VM" "Who will use" --timeout 30 || fail "no users page"
$V type "$VM" "Alice"; $V key "$VM" tab; $V type "$VM" "Bob Smith"
step users; $V key "$VM" ret
$V wait "$VM" "Setting password" --timeout 30 || fail "no password page"
$V type "$VM" "$P"; $V key "$VM" tab; $V type "$VM" "$P"; $V key "$VM" ret
$V wait "$VM" "Congratulations" --timeout 30 || fail "no finish page"
step finish; $V key "$VM" ret          # Enter must press Finish
$V wait "$VM" "Bob Smith" --timeout 300 || fail "no logon screen with both users"
step logon

# The installed custom Welcome screen must authenticate through LightDM/PAM.
$V wait "$VM" "To begin" --timeout 30 || fail "custom Welcome screen missing"
$V clicktext "$VM" "Alice" || fail "cannot click Alice"
$V wait "$VM" "password" --timeout 30 || fail "no password prompt for Alice"
$V type "$VM" "incorrect-test-password"; $V key "$VM" ret
$V wait "$VM" "Did you forget" --timeout 30 || fail "wrong password was not rejected"
step rejected-password
$V type "$VM" "$P"; $V key "$VM" ret
$V wait "$VM" "My Documents" --timeout 240 || fail "correct Alice password did not log on"
step alice-desktop

# Win+L must return to the Welcome screen and allow Bob to log on.
$V key "$VM" meta_l-l
$V wait "$VM" "Bob Smith" --timeout 60 || fail "lock did not return to Welcome screen"
step switch-user

# First logon of the passwordless user: desktop from the template prefix, with icons
$V clicktext "$VM" "Bob Smith" || fail "cannot click Bob Smith"
T0=$(date +%s)
$V wait "$VM" "My Documents" --timeout 240 || fail "no desktop for Bob Smith"
$V wait "$VM" "Recycle Bin" --timeout 30 || fail "desktop icons missing"
step desktop
ELAPSED=$(( $(date +%s) - T0 ))

# Run 32-bit (WoW64) and 64-bit program checks on the installed desktop
"$HERE/check-wow64.sh" "$VM" "$PWFILE" "$STEPS" || fail "WoW64 program checks failed"

# Check the flushed installed disk, including both users' actual session logs.
$V shutdown "$VM" || fail "guest did not shut down cleanly"
NBD=; CONNECTED=false; MOUNTED=false
cleanup() {
    if $MOUNTED; then umount "$STEPS/check-root"; fi
    if $CONNECTED; then qemu-nbd --disconnect "$NBD" >/dev/null; fi
}
trap cleanup EXIT
modprobe nbd max_part=16 || fail "cannot load NBD"
for entry in /sys/block/nbd*; do
    if [ ! -e "$entry/pid" ]; then NBD=/dev/${entry##*/}; break; fi
done
[ -n "$NBD" ] || fail "no unused NBD device"
qemu-nbd --read-only --connect "$NBD" "$STATE/disk.qcow2" || fail "cannot attach test disk"
CONNECTED=true
udevadm settle
for attempt in $(seq 1 30); do
    [ -b "${NBD}p1" ] && break
    sleep 1
done
mkdir -p "$STEPS/check-root"
mount -o ro,noload "${NBD}p1" "$STEPS/check-root" || fail "cannot mount installed root"
MOUNTED=true
python3 "$HERE/check-interactive-accounts.py" "$STEPS/check-root" "$PWFILE" || fail "installed account checks failed"
echo "PASS (Bob desktop $ELAPSED s after logon); screenshots in $STEPS; VM stopped"
