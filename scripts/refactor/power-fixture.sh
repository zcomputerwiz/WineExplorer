#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Substitute only this process tree's power_supply view; never change real sysfs.
set -euo pipefail
if [ "${1:-}" != --inside ]; then
    exec unshare -Urnm -- bash "$0" --inside "$@"
fi
shift
mode="${1:?Usage: power-fixture.sh battery|none command [args...]}"
shift
case "$mode" in battery|none) ;; *) exit 2 ;; esac
fixture="$(mktemp -d /tmp/wex-power.XXXXXXXX)"
trap 'umount /sys/class/power_supply; rm -rf -- "$fixture"' EXIT
mkdir -p "$fixture/AC"
printf '%s\n' Mains > "$fixture/AC/type"
printf '%s\n' 1 > "$fixture/AC/online"
if [ "$mode" = battery ]; then
    mkdir "$fixture/BAT0"
    printf '%s\n' Battery > "$fixture/BAT0/type"
    printf '%s\n' Charging > "$fixture/BAT0/status"
    printf '%s\n' 12000000 > "$fixture/BAT0/voltage_now"
    printf '%s\n' 4000000 > "$fixture/BAT0/charge_full"
    printf '%s\n' 2000000 > "$fixture/BAT0/charge_now"
    printf '%s\n' 0 > "$fixture/BAT0/current_now"
fi
mount --bind "$fixture" /sys/class/power_supply
printf 'Power fixture: %s\n' "$mode"
"$@"
