#!/bin/bash
# WineExplorer Desktop Environment - system tier T1 test in development mode
#
# Copyright 2026 WineExplorer Desktop Environment Project
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Session 0 (wineserver --session0 with WEX_SESSION0_DEV=1) and two user sessions A and B whose servers use it
# (WEX_SESSION0_SOCKET), all as root in plain prefixes; then a session of another uid, which session 0 refuses.
# Run as root in a build environment (docs/TOOLING.md), with BUILD set to a WoW64 build tree.
set -u
SRC=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD:-/root/WE-prefetch/build/wow64}
W=${WORK:-/root/st-t1}
SOCK=/tmp/wex-st1-session0.sock
OTHER=wexst1
cd "$BUILD" || exit 2
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all
unset DISPLAY WEX_SESSION0_SOCKET WEX_SESSION0_DEV
fails=0
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got [$2] want [$3]"; fails=$((fails+1)); fi; }
value() { tr -d '\r' | grep -a "$1" | awk '{print $NF}'; }
field() { tr -d '\r' | grep -a "^$1=" | head -1 | cut -d= -f2-; }
K='HKLM\Software\WexTier'
R32='C:\windows\syswow64\reg.exe'

rm -rf "$W"; mkdir -p "$W"
x86_64-w64-mingw32-gcc -Wall -O2 -municode -o "$W/p64.exe" "$SRC/scripts/system-tier-probe.c" -ladvapi32 -lntdll || exit 1
i686-w64-mingw32-gcc -Wall -O2 -municode -o "$W/p32.exe" "$SRC/scripts/system-tier-probe.c" -ladvapi32 -lntdll || exit 1
for p in s0 a b; do WINEPREFIX=$W/$p timeout 240 ./wine wineboot -i >/dev/null 2>&1; WINEPREFIX=$W/$p ./server/wineserver -w; done
WINEPREFIX=$W/a ./wine reg add "$K" /v where /d local-a /f >/dev/null 2>&1; WINEPREFIX=$W/a ./server/wineserver -w
WINEPREFIX=$W/s0 ./wine reg add "$K" /v where /d session0 /f >/dev/null 2>&1; WINEPREFIX=$W/s0 ./server/wineserver -w

start_s0() {
    rm -f "$SOCK"
    (WINEPREFIX=$W/s0 WEX_SESSION0_SOCKET=$SOCK WEX_SESSION0_DEV=1 ./server/wineserver --session0 -f >"$W/s0.log" 2>&1 &)
    for i in $(seq 1 50); do [ -S "$SOCK" ] && return 0; sleep 0.1; done
    echo "FAIL session 0 did not start"; cat "$W/s0.log"; exit 1
}
s0() { WINEPREFIX=$W/s0 timeout 60 ./wine "$@" 2>/dev/null; }
start_s0
check s0_socket_mode "$(stat -c %a "$SOCK")" 666

# session A: its server uses session 0; session B as well, running at the same time
export WEX_SESSION0_SOCKET=$SOCK
A() { WINEPREFIX=$W/a timeout 60 ./wine "$@" 2>/dev/null; }
B() { WINEPREFIX=$W/b timeout 60 ./wine "$@" 2>/dev/null; }
(WINEPREFIX=$W/a timeout 300 ./wine cmd /c "ping -n 290 127.0.0.1 >nul" >/dev/null 2>&1 &)
(WINEPREFIX=$W/b timeout 300 ./wine cmd /c "ping -n 290 127.0.0.1 >nul" >/dev/null 2>&1 &)
sleep 3

check a_reads_session0_64 "$(A reg query "$K" /v where | value where)" session0
check a_reads_session0_32 "$(A "$R32" query "$K" /v where /reg:64 | value where)" session0
A reg add "$K" /v froma /d written-in-a /f >/dev/null; check a_write_exit $? 0
check b_sees_a_write_at_once "$(B reg query "$K" /v froma | value froma)" written-in-a
check s0_has_a_write "$(s0 reg query "$K" /v froma | value froma)" written-in-a
B "$R32" add "$K\\FromB" /v b32 /d written-in-b32 /f /reg:64 >/dev/null; check b_write32_exit $? 0
check a_enum_sees_b_subkey "$(A reg query "$K" | tr -d '\r' | grep -c 'WexTier.FromB$')" 1
A reg delete "$K" /v froma /f >/dev/null; check a_delete_exit $? 0
check b_sees_delete "$(B reg query "$K" /v froma | value froma)" ""

# change notification: a program in B waiting on a machine key is woken by a write in A (64-bit and 32-bit);
# without a change it times out
notify_case() {  # name, probe, wait ms, change (yes/no)
    rm -f "$W/notify.out"
    (WINEPREFIX=$W/b timeout 60 ./wine "$W/$2" notify 'Software\WexTier' "$3" >"$W/notify.out" 2>/dev/null &)
    for i in $(seq 1 100); do grep -q '^armed' "$W/notify.out" 2>/dev/null && break; sleep 0.2; done
    [ "$4" = yes ] && A reg add "$K" /v notify$1 /d x /f >/dev/null
    for i in $(seq 1 100); do grep -q '^notify=' "$W/notify.out" 2>/dev/null && break; sleep 0.2; done
    field notify < "$W/notify.out"
}
check notify64_changed "$(notify_case 64 p64.exe 15000 yes)" changed
check notify32_changed "$(notify_case 32 p32.exe 15000 yes)" changed
check notify_quiet_times_out "$(notify_case quiet p64.exe 1500 no)" timeout

# per-session keys stay in each session's server; HKCU stays in the session
A reg add 'HKLM\Software\Wine\Drives' /v st1 /d drive-a /f >/dev/null; check a_session_key_write_exit $? 0
check a_reads_own_session_key "$(A reg query 'HKLM\Software\Wine\Drives' /v st1 | value st1)" drive-a
check b_no_session_key_of_a "$(B reg query 'HKLM\Software\Wine\Drives' /v st1 | value st1)" ""
check s0_no_session_key_of_a "$(s0 reg query 'HKLM\Software\Wine\Drives' /v st1 | value st1)" ""
A reg add 'HKCU\Software\WexTier' /v user /d a-user /f >/dev/null
check s0_no_hkcu_of_a "$(s0 reg query 'HKCU\Software\WexTier' /v user | value user)" ""
check a_reads_own_hkcu "$(A reg query 'HKCU\Software\WexTier' /v user | value user)" a-user

# handles: duplicate, object name, security descriptor (64-bit and 32-bit)
check dup64 "$(A "$W/p64.exe" dup 'Software\WexTier' where | field dup)" session0
check dup32 "$(A "$W/p32.exe" dup 'Software\WexTier' where | field dup)" session0
check name64 "$(A "$W/p64.exe" name 'Software\WexTier' | field name)" '\REGISTRY\Machine\Software\WexTier'
check sd_present "$(A "$W/p64.exe" name 'Software\WexTier' | field sd | awk '{print ($1 > 0) ? "yes" : "no"}')" yes

# a program keeps its key handle while session 0 restarts: the key is reopened on the new channel
rm -f "$W/marker"
(WINEPREFIX=$W/a timeout 120 ./wine "$W/p64.exe" hold 'Software\WexTier' where 'Z:\root\st-t1\marker' >"$W/hold.out" 2>/dev/null &)
for i in $(seq 1 100); do grep -q '^first=' "$W/hold.out" 2>/dev/null && break; sleep 0.2; done
WINEPREFIX=$W/s0 ./server/wineserver -k; sleep 1
check s0_saved_b_write "$(grep -a -c '"b32"="written-in-b32"' "$W/s0/system.reg")" 1
start_s0
s0 reg add "$K" /v where /d session0-restarted /f >/dev/null
touch "$W/marker"
for i in $(seq 1 100); do grep -q '^sub=' "$W/hold.out" 2>/dev/null && break; sleep 0.2; done
check hold_first "$(field first < "$W/hold.out")" session0
check hold_second_after_restart "$(field second < "$W/hold.out")" session0-restarted
check hold_create_subkey "$(field sub < "$W/hold.out")" 0
check s0_has_held_subkey "$(s0 reg query "$K\\Held" >/dev/null 2>&1; echo $?)" 0

# a server that does not use session 0 keeps its own machine registry
unset WEX_SESSION0_SOCKET
WINEPREFIX=$W/a ./server/wineserver -k; sleep 1
check off_reads_local "$(A reg query "$K" /v where | value where)" local-a
WINEPREFIX=$W/a ./server/wineserver -k; WINEPREFIX=$W/b ./server/wineserver -k; sleep 1

# a session of another uid: session 0 (development trust: its own uid only) refuses its server
id -u $OTHER >/dev/null 2>&1 || useradd -M -d "$W/home" -s /usr/sbin/nologin $OTHER
mkdir -p "$W/home"; chown $OTHER "$W/home"
U() { runuser -u $OTHER -- env HOME="$W/home" WINEPREFIX="$W/home/.wine" WINEDLLOVERRIDES="$WINEDLLOVERRIDES" WINEDEBUG=-all "$@"; }
U timeout 240 ./wine wineboot -i >/dev/null 2>&1; U ./server/wineserver -w
check other_off_reads "$(U timeout 60 ./wine reg query 'HKLM\Software\Microsoft\Windows NT\CurrentVersion' /v CurrentBuild 2>/dev/null | value CurrentBuild | grep -c .)" 1
U ./server/wineserver -k; sleep 1  # the next server must start with the session 0 setting
U env WEX_SESSION0_SOCKET=$SOCK timeout 60 ./wine reg query 'HKLM\Software\Microsoft\Windows NT\CurrentVersion' /v CurrentBuild >/dev/null 2>&1
check other_uid_refused "$?" 1
U ./server/wineserver -k; sleep 1

WINEPREFIX=$W/s0 ./server/wineserver -k; sleep 1
userdel $OTHER 2>/dev/null
grep -a -i 'error\|fatal' "$W/s0.log" | head -5
rm -rf "$W" "$SOCK"
echo "FAILS=$fails"
