#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# test_accounts_service.sh - the Windows-side accounts service (src/programs/wexaccounts) on its own: a lab build of the service
# (-DWEXACCOUNTS_LAB: the helper is a stand-in, the map is a file) in a plain prefix, and scripts/accounts_service_probe.c as its client.
# Checks what the service keeps: the pipe's syntax and limits, the order of changes, the wait for the account map, the number
# of connections. Needs no session 0, no helper and no root account other than the user of the run (wexgpo: root).
# After a build of the worktree (wex-run.sh build). Prints the probe's ok/FAIL lines and "accounts_service=PASS" or "=FAIL".
set -u
T=${WEX_TREE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
B=${WEX_BUILD:-/root/WE-prefetch/build/wow64}
W=${WEX_WORK:-/root/accsvc-work}
export WINEPREFIX=$W/pfx WINEDEBUG=-all
mkdir -p "$W"
cc=x86_64-w64-mingw32-gcc
$cc -O2 -Wall -municode -DWEXACCOUNTS_LAB -I"$T/src/include" -o "$W/wexaccounts_lab.exe" "$T/src/programs/wexaccounts/main.c" -ladvapi32 || exit 1
$cc -O2 -Wall -municode -I"$T/src/include" -o "$W/accounts_service_probe.exe" "$T/scripts/accounts_service_probe.c" || exit 1
[ -d "$WINEPREFIX" ] || "$B/wine" wineboot -i >/dev/null 2>&1
# the prefix has the real wexaccounts service (wine.inf), which would own the pipe names first: it stays off here
"$B/wine" reg add 'HKLM\System\CurrentControlSet\Services\wexaccounts' /v Start /t REG_DWORD /d 4 /f >/dev/null 2>&1
"$B/server/wineserver" -k 2>/dev/null; sleep 2
MAP='Z:\root\accsvc-work\accounts.map'
[ "$W" = /root/accsvc-work ] || MAP="Z:${W//\//\\}\\accounts.map"
pkill -f wexaccounts_lab.exe 2>/dev/null; "$B/server/wineserver" -k 2>/dev/null; sleep 2   # nothing of an earlier run may hold the pipe
"$B/wine" "$W/wexaccounts_lab.exe" --console "$MAP" > "$W/service.log" 2>&1 &
SVC=$!
sleep 6
"$B/wine" "$W/accounts_service_probe.exe" '\\.\pipe\WexSession0\accounts' "$MAP" 2>&1 | tr -d '\r' | tee "$W/probe.log"
rc=${PIPESTATUS[0]}
kill "$SVC" 2>/dev/null; pkill -f wexaccounts_lab.exe 2>/dev/null; "$B/server/wineserver" -k 2>/dev/null
exit $rc
