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
# a 32-bit program's own view: HKLM\Software is session 0's Wow6432Node, as in the program's own server
s0 reg add 'HKLM\Software\Wow6432Node\WexTier' /v where /d session0-32bit-view /f >/dev/null
check a_32bit_view_from_session0 "$(A "$R32" query "$K" /v where | value where)" session0-32bit-view
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
    # the change goes to the key the probe's view sees: a 32-bit probe watches HKLM\Software\Wow6432Node\WexTier
    if [ "$4" = yes ] && [ "$1" = 32 ]; then A reg add 'HKLM\Software\Wow6432Node\WexTier' /v notify$1 /d x /f >/dev/null
    elif [ "$4" = yes ]; then A reg add "$K" /v notify$1 /d x /f >/dev/null; fi
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
check dup32 "$(A "$W/p32.exe" dup 'Software\WexTier' where | field dup)" session0-32bit-view
# T6.1: session 0 gives each logon (each user's server) a session id of its own
SA="$(A "$W/p64.exe" whoami | field session)"; SB="$(B "$W/p64.exe" whoami | field session)"
check sessions_differ "$([ -n "$SA" ] && [ -n "$SB" ] && [ "$SA" != "$SB" ] && [ "$SA" -ge 1 ] && [ "$SB" -ge 1 ] && echo yes)" yes
# T6.2: each session's server keeps its link and answers session 0's order with its own session id
for s in "$SA" "$SB"; do
    check "session_${s}_answers_orders" "$(grep -a -c "^session0: session $s (.*) answers on its link: session id $s\$" "$W/s0.log")" 1
done
# and session 0 drops a session's link when its server ends
WINEPREFIX=$W/b ./server/wineserver -k; sleep 1
check s0_sees_session_end "$(grep -a -c "^session0: session $SB (server pid [0-9]*) ended\$" "$W/s0.log")" 1
check name64 "$(A "$W/p64.exe" name 'Software\WexTier' | field name)" '\REGISTRY\Machine\Software\WexTier'
check sd_present "$(A "$W/p64.exe" name 'Software\WexTier' | field sd | awk '{print ($1 > 0) ? "yes" : "no"}')" yes

# a key's own permissions are kept by session 0 across a restart (T2a); set here, checked after the restart below
A reg add "$K\\Perm" /f >/dev/null
DACL='D:(A;;KA;;;BA)(A;;KR;;;WD)'
check setdacl "$(A "$W/p64.exe" setdacl 'Software\WexTier\Perm' "$DACL" | field setdacl)" 0
PERM_BEFORE="$(A "$W/p64.exe" getdacl 'Software\WexTier\Perm' | field dacl)"

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
# T6.2: A's server (kept by the holding program) linked to the new session 0 again under its own session id
for i in $(seq 1 30); do grep -aq ' linked again$' "$W/s0.log" && break; sleep 0.5; done
check a_linked_again "$(grep -a -c '^session0: session [0-9]* (server pid [0-9]*, uid 0) linked again$' "$W/s0.log" | awk '{print ($1 > 0) ? "yes" : "no"}')" yes
check s0_keeps_next_id "$([ -s "$W/s0/session0.ids" ] && echo yes)" yes
# empty messages that carry descriptors do not leak them in session 0: 200 on new links, 40 on session links
S0PID="$(pgrep -f -- 'wineserver --session0' | head -1)"
FD_BEFORE="$(ls "/proc/$S0PID/fd" | wc -l)"
python3 -I - "$SOCK" <<'PY'
import array, socket, struct, sys
fds = array.array('i', [sys.stdin.fileno()] * 4)
anc = [(socket.SOL_SOCKET, socket.SCM_RIGHTS, fds.tobytes())]
for i in range(240):
    c = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    c.connect(sys.argv[1])
    if i >= 200:   # a session link first: SESSION_ID, its answer and the PING order
        c.send(struct.pack('<5I', 0x30535857, 5, 0, 4, 0))   # protocol version 5
        c.settimeout(2); c.recv(64); c.recv(64)
    c.sendmsg([b''], anc)
    c.settimeout(2)
    try:
        c.recv(64)
    except OSError:
        pass
    c.close()
PY
sleep 1
FD_AFTER="$(ls "/proc/$S0PID/fd" | wc -l)"
check empty_messages_leak_no_fds "$([ "$FD_AFTER" -le $((FD_BEFORE + 8)) ] && echo yes || echo "no: $FD_BEFORE -> $FD_AFTER")" yes

# T5.2: a session's program reaches session 0's service manager through \pipe\WexSession0\svcctl (64- and 32-bit)
s0 sc query PlugPlay >/dev/null 2>&1   # session 0's own services.exe runs
check s0_programs_in_session_0 "$(s0 "$W/p64.exe" whoami | field session)" 0
check s0_programs_run_as_localsystem "$(s0 "$W/p64.exe" whoami | field user)" S-1-5-18
check pipe64_bridged "$(A "$W/p64.exe" rpcbind 'WexSession0\svcctl' | field pipe)" open
check bind64_through_bridge "$(A "$W/p64.exe" rpcbind 'WexSession0\svcctl' | field bind | cut -d' ' -f1)" 12
check bind32_through_bridge "$(A "$W/p32.exe" rpcbind 'WexSession0\svcctl' | field bind | cut -d' ' -f1)" 12
check bridge_refuses_unlisted_pipe "$(A "$W/p64.exe" rpcbind 'WexSession0\lsarpc' | field pipe)" error5
check session_cannot_serve_session0_pipe "$(A "$W/p64.exe" pipecreate 'WexSession0\svcctl' | field create)" error5

# T5.4: the sessions' service managers forward the services that are not a session's own (here wuauserv) to session
# 0's, and keep their own (PlugPlay). Their servers start now, with session 0's service manager up.
state() { tr -d '\r' | awk '/STATE/ { print $3; exit }'; }
WINEPREFIX=$W/a ./server/wineserver -k 2>/dev/null; WINEPREFIX=$W/b ./server/wineserver -k 2>/dev/null; sleep 1
s0 sc stop wuauserv >/dev/null 2>&1; sleep 1
check s0_wuauserv_stopped "$(s0 sc query wuauserv | state)" 1
A sc start wuauserv >/dev/null 2>&1
for i in $(seq 1 20); do [ "$(s0 sc query wuauserv | state)" = 4 ] && break; sleep 0.5; done
check started_from_a_runs_in_session0 "$(s0 sc query wuauserv | state)" 4
check b_sees_session0_state "$(B sc query wuauserv | state)" 4
check a_service_process_not_in_a "$(A tasklist 2>/dev/null | grep -ci wuauserv)" 0
A sc stop wuauserv >/dev/null 2>&1
for i in $(seq 1 20); do [ "$(s0 sc query wuauserv | state)" = 1 ] && break; sleep 0.5; done
check stopped_from_a_in_session0 "$(s0 sc query wuauserv | state)" 1
check a_keeps_own_plugplay "$(A sc query PlugPlay | state)" 4
A sc create WexT5Probe binPath= 'C:\windows\system32\wuauserv.exe' >/dev/null 2>&1
check created_from_a_in_session0 "$(s0 sc query WexT5Probe | state)" 1
check b_sees_service_created_by_a "$(B sc query WexT5Probe | state)" 1
A sc delete WexT5Probe >/dev/null 2>&1
check deleted_from_a_in_session0 "$(s0 sc query WexT5Probe | state)" ""
check a_lists_session0_service "$(A "$W/p64.exe" enumsvc wuauserv | field count)" 1
check a_lists_own_plugplay_once "$(A "$W/p64.exe" enumsvc PlugPlay | field count)" 1
check a32_lists_session0_service "$(A "$W/p32.exe" enumsvc wuauserv | field count)" 1
check s0_has_held_subkey "$(s0 reg query "$K\\Held" >/dev/null 2>&1; echo $?)" 0
check s0_saved_key_dacl "$(grep -a -c '^#sd=' "$W/s0/system.reg" | awk '{print ($1 > 0) ? "yes" : "no"}')" yes
check dacl_survives_restart "$(A "$W/p64.exe" getdacl 'Software\WexTier\Perm' | field dacl)" "$PERM_BEFORE"
check dacl_is_the_set_one "$(echo "$PERM_BEFORE" | grep -c 'KR;;;WD')" 1

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
