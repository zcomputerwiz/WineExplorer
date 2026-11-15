# shellcheck shell=bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Sourced by the VM tests (safe-mode.sh, install-unattended.sh): connect a qcow2 disk to the first unused NBD device.
#   nbd_connect ro|rw DISK    sets NBD (/dev/nbdN) and returns 0, or prints why not and returns 1
# Two tests running at once (installer/tests/run-chain.sh runs BIOS and UEFI, and several stages, side by side) could
# both see the same device unused and both connect to it: the pick and the connect are made under one lock, held
# until qemu-nbd has the device (its /sys/block/nbdN/pid then exists, and the next test skips it).
# WEX_NBD_FIRST (default 0) and WEX_NBD_LAST (default 7) bound the devices used: the Vintner Boot session's own runs
# on this machine have used nbd8 and up.

nbd_connect() {
    local mode=$1 disk=$2 lock n dev
    NBD=
    modprobe nbd max_part=16 || return 1
    exec {lock}>/run/lock/wex-test-nbd.lock || return 1
    if ! flock -w 300 "$lock"; then
        exec {lock}>&-
        echo "the NBD lock /run/lock/wex-test-nbd.lock was held for 5 minutes"
        return 1
    fi
    for n in $(seq "${WEX_NBD_FIRST:-0}" "${WEX_NBD_LAST:-7}"); do
        dev=/dev/nbd$n
        [ -b "$dev" ] || continue
        [ -e "/sys/block/nbd$n/pid" ] && continue
        if [ "$mode" = ro ]; then
            qemu-nbd --read-only --connect "$dev" "$disk" || continue
        else
            qemu-nbd --connect "$dev" "$disk" || continue
        fi
        NBD=$dev
        break
    done
    flock -u "$lock"
    exec {lock}>&-
    [ -n "$NBD" ] || { echo "no unused NBD device in nbd${WEX_NBD_FIRST:-0}-${WEX_NBD_LAST:-7}"; return 1; }
}
