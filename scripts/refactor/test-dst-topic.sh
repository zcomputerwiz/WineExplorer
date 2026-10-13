#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Change only the isolated wineserver's time() and this process tree's TZ.
set -euo pipefail
root="${WEX_REFACTOR_ROOT:-$HOME/wex-refactor}"
build="${WEX_TEST_BUILD:?Set WEX_TEST_BUILD}"
fixture="$(mktemp -d /tmp/wex-dst.XXXXXXXX)"
export WEX_SERVER_BINARY="$WINESERVER"
export WEX_CLOCK_LIBRARY="$root/fake-server-time.so"
export WINESERVER="$root/server-clock.sh"
export WEX_SERVER_CLOCK="$fixture/clock"
export WINETEST_DST_FIXTURE="Z:$fixture"
export TZ='WEXST5WEXDT,M3.2.0/2,M11.1.0/2'
cleanup()
{
    "$WINESERVER" -k || true
    "$WINESERVER" -w
    rm -rf -- "$fixture"
}
trap cleanup EXIT
result=0
for start in 0 1; do
    if [ "$start" = 0 ]; then epoch=1768478400; else epoch=1784116800; fi
    printf '%s\n' "$epoch" > "$fixture/clock"
    export WINETEST_DST_START="$start"
    printf 'Server DST fixture: initial summer %s\n' "$start"
    rm -f "$build/dlls/ntdll/tests/x86_64-windows/time.ok"
    "$@" || result=$?
    "$WINESERVER" -k || true
    "$WINESERVER" -w
done
exit "$result"
