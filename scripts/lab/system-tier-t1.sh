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
    # up to 60 s: with chain VMs and a build on the machine, the server has taken more than 5 s to make its socket
    for i in $(seq 1 600); do [ -S "$SOCK" ] && return 0; sleep 0.1; done
    echo "FAIL session 0 did not start within 60 s"; cat "$W/s0.log"
    WINEPREFIX=$W/s0 ./server/wineserver -k 2>/dev/null; exit 1
}
s0() { WINEPREFIX=$W/s0 timeout 60 ./wine "$@" 2>/dev/null; }
# the pid of this run's server of a prefix (another run's session 0 may be on the machine too)
prefix_server_pid() {
    local p
    for p in $(pgrep -x wineserver); do
        tr '\0' '\n' < "/proc/$p/environ" 2>/dev/null | grep -qx "WINEPREFIX=$1" && { echo "$p"; return 0; }
    done
    return 1
}
# waits up to 5 s for a prefix's server to end
server_gone() { local i; for i in $(seq 1 50); do prefix_server_pid "$1" > /dev/null || return 0; sleep 0.1; done; return 1; }
# ends every process still running in this run's prefixes ($W/*, the other uid's prefix included). A server's end does
# not end all its clients: session 0's services.exe was left behind by each run, as root with a deleted prefix
# (14 of them on one machine, 2026-11-27). Runs at the end and on any exit.
end_prefix_procs() {
    local p left=""
    for p in /proc/[0-9]*; do
        p=${p#/proc/}
        [ "$p" = "$$" ] && continue
        tr '\0' '\n' < "/proc/$p/environ" 2>/dev/null | grep -q "^WINEPREFIX=$W/" && left="$left $p"
    done
    [ -n "$left" ] || return 0
    echo "ending processes left in the prefixes:$left"
    kill $left 2>/dev/null; sleep 2
    for p in $left; do kill -0 "$p" 2>/dev/null && kill -9 "$p" 2>/dev/null; done
    return 0
}
trap end_prefix_procs EXIT
# stops session 0 as systemd does (wex-session0.service): SIGTERM saves the hives at once and ends the server. Not
# wineserver -k: its SIGINT shuts the server down first and saves at the end, and after about 10 s -k sends SIGKILL,
# which under load (chain VMs on the machine) came before the save (b1126a: s0_saved_key_dacl, dacl_survives_restart)
stop_s0() {
    local p i
    p="$(prefix_server_pid "$W/s0")" || return 0
    kill -TERM "$p"
    for i in $(seq 1 600); do kill -0 "$p" 2>/dev/null || return 0; sleep 0.1; done
    echo "FAIL session 0 did not end within 60 s of SIGTERM"; return 1
}
start_s0
check s0_socket_mode "$(stat -c %a "$SOCK")" 666

# session A: its server uses session 0; session B as well, running at the same time
export WEX_SESSION0_SOCKET=$SOCK
A() { WINEPREFIX=$W/a timeout 60 ./wine "$@" 2>/dev/null; }
B() { WINEPREFIX=$W/b timeout 60 ./wine "$@" 2>/dev/null; }
(WINEPREFIX=$W/a timeout 300 ./wine cmd /c "ping -n 290 127.0.0.1 >nul" >/dev/null 2>&1 &)
(WINEPREFIX=$W/b timeout 300 ./wine cmd /c "ping -n 290 127.0.0.1 >nul" >/dev/null 2>&1 &)
# both sessions' servers up (up to 15 s), instead of a fixed wait
for i in $(seq 1 150); do prefix_server_pid "$W/a" > /dev/null && prefix_server_pid "$W/b" > /dev/null && break; sleep 0.1; done

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

# R6 (docs/SESSION0_PROCESS_IDS.md): the process and thread ids of A's, B's and session 0's servers come from
# blocks that session 0 leases to each (4096 indices, ids are indices * 4: an id's block is id / 16384), so they
# never meet; session 0's own are in block 0, and session 0 records the blocks of A's server
id_blocks() { tr -d '\r' | grep -aE '^(pid|tid)=' | cut -d= -f2 | awk '{ print int($1 / 16384) }' | sort -un | tr '\n' ' '; }
A_BLOCKS="$(A "$W/p64.exe" pids | id_blocks)"
B_BLOCKS="$(B "$W/p64.exe" pids | id_blocks)"
S0_BLOCKS="$(s0 "$W/p64.exe" pids | id_blocks)"
check a_and_b_have_id_blocks "$([ -n "$A_BLOCKS" ] && [ -n "$B_BLOCKS" ] && echo yes)" yes
check ids_of_a_b_and_session0_never_meet "$(printf '%s\n' $A_BLOCKS $B_BLOCKS $S0_BLOCKS | sort | uniq -d | wc -l)" 0
check session0_ids_in_block_0 "$S0_BLOCKS" "0 "
check session0_records_a_blocks "$(for b in $A_BLOCKS; do grep -ac "^$b " "$W/s0/session0.blocks"; done | sort -u | tr -d '\n')" 1
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
# R3: a session 0 key's handle duplicated into another process, pushed by the program or pulled by the other one;
# the other process opens the key again with its own token, in the program's registry view
check xdup64_push "$(A "$W/p64.exe" xdup 'Software\WexTier' where push | field xget)" session0
check xdup64_pull "$(A "$W/p64.exe" xdup 'Software\WexTier' where pull | field xget)" session0
check xdup32_push "$(A "$W/p32.exe" xdup 'Software\WexTier' where push | field xget)" session0-32bit-view
check xdup32_pull "$(A "$W/p32.exe" xdup 'Software\WexTier' where pull | field xget)" session0-32bit-view
# T6.1: session 0 gives each logon (each user's server) a session id of its own
SA="$(A "$W/p64.exe" whoami | field session)"; SB="$(B "$W/p64.exe" whoami | field session)"
check sessions_differ "$([ -n "$SA" ] && [ -n "$SB" ] && [ "$SA" != "$SB" ] && [ "$SA" -ge 1 ] && [ "$SB" -ge 1 ] && echo yes)" yes
# T6.2: each session's server keeps its link and answers session 0's order with its own session id
for s in "$SA" "$SB"; do
    check "session_${s}_answers_orders" "$(grep -a -c "^session0: session $s (.*) answers on its link: session id $s\$" "$W/s0.log")" 1
done
# and session 0 drops a session's link when its server ends
WINEPREFIX=$W/b ./server/wineserver -k
for i in $(seq 1 50); do grep -aq "^session0: session $SB (server pid [0-9]*) ended\$" "$W/s0.log" && break; sleep 0.1; done
check s0_sees_session_end "$(grep -a -c "^session0: session $SB (server pid [0-9]*) ended\$" "$W/s0.log")" 1
NAME_OUT="$(A "$W/p64.exe" name 'Software\WexTier')"   # one probe run, both of its fields checked
check name64 "$(field name <<<"$NAME_OUT")" '\REGISTRY\Machine\Software\WexTier'
check sd_present "$(field sd <<<"$NAME_OUT" | awk '{print ($1 > 0) ? "yes" : "no"}')" yes

# a key's own permissions are kept by session 0 across a restart (T2a); set here, checked after the restart below
A reg add "$K\\Perm" /f >/dev/null
DACL='D:(A;;KA;;;BA)(A;;KR;;;WD)'
check setdacl "$(A "$W/p64.exe" setdacl 'Software\WexTier\Perm' "$DACL" | field setdacl)" 0
PERM_BEFORE="$(A "$W/p64.exe" getdacl 'Software\WexTier\Perm' | field dacl)"

# a program keeps its key handle while session 0 restarts: the key is reopened on the new channel
rm -f "$W/marker"
# the marker's Windows path follows $W (WORK), not a fixed /root/st-t1
MARKER_WIN="Z:$(printf '%s' "$W/marker" | tr '/' '\\')"
(WINEPREFIX=$W/a timeout 120 ./wine "$W/p64.exe" hold 'Software\WexTier' where "$MARKER_WIN" >"$W/hold.out" 2>/dev/null &)
for i in $(seq 1 100); do grep -q '^first=' "$W/hold.out" 2>/dev/null && break; sleep 0.2; done
stop_s0 || fails=$((fails+1))   # it saves the hives as it ends
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
# R6: A's server, linked again, holds its blocks of ids still; a new program of A gets an id from them
A_SELF_BLOCK="$(A "$W/p64.exe" pids | field self | awk '{ print int($1 / 16384) }')"
check a_keeps_its_id_blocks_after_restart "$(case " $A_BLOCKS" in *" $A_SELF_BLOCK "*) echo yes ;; *) echo "no ($A_SELF_BLOCK; $A_BLOCKS)" ;; esac)" yes
check session0_records_a_blocks_after_restart "$(for b in $A_BLOCKS; do grep -ac "^$b " "$W/s0/session0.blocks"; done | sort -u | tr -d '\n')" 1
# empty messages that carry descriptors do not leak them in session 0: 200 on new links, 40 on session links
S0PID="$(prefix_server_pid "$W/s0")"   # this run's session 0, not another run's on the machine
FD_BEFORE="$(ls "/proc/$S0PID/fd" | wc -l)"
# (the protocol version is the build's own, so that the 40 links are session links, which the count checks)
S0_VERSION="$(awk '$2 == "WEX_SESSION0_VERSION" { print $3 }' "$SRC/wine/include/wine/wex_session0.h")"
LINKED="$(python3 -I - "$SOCK" "$S0_VERSION" <<'PY'
import array, socket, struct, sys
fds = array.array('i', [sys.stdin.fileno()] * 4)
anc = [(socket.SOL_SOCKET, socket.SCM_RIGHTS, fds.tobytes())]
linked = 0
for i in range(240):
    c = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    c.connect(sys.argv[1])
    if i >= 200:   # a session link first: SESSION_ID, its answer and the PING order
        c.send(struct.pack('<5I', 0x30535857, int(sys.argv[2]), 0, 4, 0))
        c.settimeout(2)
        try:
            if c.recv(64) and c.recv(64):
                linked += 1
        except OSError:
            pass
    try:
        c.sendmsg([b''], anc)
        c.settimeout(2)
        c.recv(64)
    except OSError:
        pass
    c.close()
print(linked)
PY
)"
sleep 1
FD_AFTER="$(ls "/proc/$S0PID/fd" | wc -l)"
check empty_messages_session_links_made "$LINKED" 40
check empty_messages_leak_no_fds "$([ "$FD_AFTER" -le $((FD_BEFORE + 8)) ] && echo yes || echo "no: $FD_BEFORE -> $FD_AFTER")" yes

# T5.2: a session's program reaches session 0's service manager through \pipe\WexSession0\svcctl (64- and 32-bit)
s0 sc query PlugPlay >/dev/null 2>&1   # session 0's own services.exe runs
S0_WHO="$(s0 "$W/p64.exe" whoami)"; BIND64="$(A "$W/p64.exe" rpcbind 'WexSession0\svcctl')"   # one run each
check s0_programs_in_session_0 "$(field session <<<"$S0_WHO")" 0
check s0_programs_run_as_localsystem "$(field user <<<"$S0_WHO")" S-1-5-18
check pipe64_bridged "$(field pipe <<<"$BIND64")" open
check bind64_through_bridge "$(field bind <<<"$BIND64" | cut -d' ' -f1)" 12
check bind32_through_bridge "$(A "$W/p32.exe" rpcbind 'WexSession0\svcctl' | field bind | cut -d' ' -f1)" 12
check bridge_refuses_unlisted_pipe "$(A "$W/p64.exe" rpcbind 'WexSession0\lsarpc' | field pipe)" error5
check session_cannot_serve_session0_pipe "$(A "$W/p64.exe" pipecreate 'WexSession0\svcctl' | field create)" error5

# T5.4: the sessions' service managers forward the services that are not a session's own (here wuauserv) to session
# 0's, and keep their own (PlugPlay). Their servers start now, with session 0's service manager up.
state() { tr -d '\r' | awk '/STATE/ { print $3; exit }'; }
WINEPREFIX=$W/a ./server/wineserver -k 2>/dev/null; WINEPREFIX=$W/b ./server/wineserver -k 2>/dev/null
server_gone "$W/a"; server_gone "$W/b"
s0 sc stop wuauserv >/dev/null 2>&1
for i in $(seq 1 20); do [ "$(s0 sc query wuauserv | state)" = 1 ] && break; sleep 0.5; done
check s0_wuauserv_stopped "$(s0 sc query wuauserv | state)" 1
A sc start wuauserv >/dev/null 2>&1
for i in $(seq 1 20); do [ "$(s0 sc query wuauserv | state)" = 4 ] && break; sleep 0.5; done
check started_from_a_runs_in_session0 "$(s0 sc query wuauserv | state)" 4
check b_sees_session0_state "$(B sc query wuauserv | state)" 4
check a_service_process_not_in_a "$(A tasklist 2>/dev/null | grep -ci wuauserv)" 0
A sc stop wuauserv >/dev/null 2>&1
for i in $(seq 1 20); do [ "$(s0 sc query wuauserv | state)" = 1 ] && break; sleep 0.5; done
check stopped_from_a_in_session0 "$(s0 sc query wuauserv | state)" 1
# NotifyServiceStatusChange on a service of session 0, relayed by the session's service manager: the current state at
# once (SERVICE_NOTIFY_STOPPED 1), and a start made in session 0 (SERVICE_NOTIFY_RUNNING 8, state 4)
check notify_session0_current_state "$(A "$W/p64.exe" svcnotify wuauserv 1 5000 | field fired)" 1
rm -f "$W/notify.out"
(A "$W/p64.exe" svcnotify wuauserv 8 20000 >"$W/notify.out" &)
for i in $(seq 1 40); do grep -q '^notify=' "$W/notify.out" 2>/dev/null && break; sleep 0.5; done
check notify_session0_registered "$(field notify < "$W/notify.out")" registered
s0 sc start wuauserv >/dev/null 2>&1
for i in $(seq 1 50); do grep -q '^fired=' "$W/notify.out" 2>/dev/null && break; sleep 0.5; done
check notify_session0_change "$(field fired < "$W/notify.out")" 4
# R6 step 2: A reads the process id of session 0's service as session 0 does (it was 0 before ids were unique), and
# the id is in session 0's block (block 0: ids below 16384)
S0_SVC_PID="$(s0 "$W/p64.exe" svcstate wuauserv | field pid)"
A_SVC_PID="$(A "$W/p64.exe" svcstate wuauserv | field pid)"
check a_sees_session0_service_pid "$A_SVC_PID" "$S0_SVC_PID"
check session0_service_pid_in_block_0 "$(awk '{ print ($1 > 0 && $1 < 16384) ? "yes" : "no: " $1 }' <<<"$S0_SVC_PID")" yes
s0 sc stop wuauserv >/dev/null 2>&1
for i in $(seq 1 20); do [ "$(s0 sc query wuauserv | state)" = 1 ] && break; sleep 0.5; done
check a_keeps_own_plugplay "$(A sc query PlugPlay | state)" 4
A sc create WexT5Probe binPath= 'C:\windows\system32\wuauserv.exe' >/dev/null 2>&1
check created_from_a_in_session0 "$(s0 sc query WexT5Probe | state)" 1
check b_sees_service_created_by_a "$(B sc query WexT5Probe | state)" 1
A sc delete WexT5Probe >/dev/null 2>&1
check deleted_from_a_in_session0 "$(s0 sc query WexT5Probe | state)" ""
# R1b: a service whose own key says WexSessionService=1 is every session's: A starts its own copy (session 0's stays
# stopped) once A's service manager has loaded the value (a restart of A's server)
A sc create WexT5Session binPath= 'C:\windows\system32\wuauserv.exe' >/dev/null 2>&1
s0 reg add 'HKLM\System\CurrentControlSet\Services\WexT5Session' /v WexSessionService /t REG_DWORD /d 1 /f >/dev/null 2>&1
WINEPREFIX=$W/a ./server/wineserver -k 2>/dev/null; sleep 1
A sc start WexT5Session >/dev/null 2>&1
for i in $(seq 1 20); do [ "$(A sc query WexT5Session | state)" = 4 ] && break; sleep 0.5; done
check session_service_value_runs_in_a "$(A sc query WexT5Session | state)" 4
check session_service_value_not_in_session0 "$(s0 sc query WexT5Session | state)" 1
check session_service_value_b_has_own_copy "$(B sc query WexT5Session | state)" 1
A sc stop WexT5Session >/dev/null 2>&1
for i in $(seq 1 20); do [ "$(A sc query WexT5Session | state)" = 1 ] && break; sleep 0.5; done
s0 sc delete WexT5Session >/dev/null 2>&1
check a_lists_session0_service "$(A "$W/p64.exe" enumsvc wuauserv | field count)" 1
check a_lists_own_plugplay_once "$(A "$W/p64.exe" enumsvc PlugPlay | field count)" 1
check a32_lists_session0_service "$(A "$W/p32.exe" enumsvc wuauserv | field count)" 1

# R9 (E1 broker in session 0): \pipe\WexSession0\wexelevate.r9 reaches session 0's wexelevate.r9 from a session with
# a session link, and the server there reads the session id of the client's session
SA_NOW="$(A "$W/p64.exe" whoami | field session)"
rm -f "$W/serve.out"
(s0 "$W/p64.exe" pipeserve wexelevate.r9 >"$W/serve.out" &)
for i in $(seq 1 60); do grep -q '^listening' "$W/serve.out" 2>/dev/null && break; sleep 0.5; done
SAY="$(A "$W/p64.exe" pipesay 'WexSession0\wexelevate.r9' hello)"   # one run, both fields
check r9_pipe_from_session "$(field say <<<"$SAY")" ok
for i in $(seq 1 40); do grep -q '^message=' "$W/serve.out" 2>/dev/null && break; sleep 0.5; done
check r9_client_session_is_the_session "$(field clientsession < "$W/serve.out")" "$SA_NOW"
# R6 step 2: the server in session 0 reads the opener's own process id (ids are unique across servers)
check r9_client_pid_is_the_opener "$(field clientpid < "$W/serve.out")" "$(field self <<<"$SAY")"
check r9_message_crossed "$(field message < "$W/serve.out")" hello

# B17: a service's ncalrpc endpoint (\pipe\lrpc\NAME) in session 0 is reached from a session that has no such pipe;
# a name made from a process id is not (it means another process there), and a session's own endpoint comes first
serve_s0() { rm -f "$W/$2"; (s0 "$W/p64.exe" pipeserve "$1" >"$W/$2" &)
             for i in $(seq 1 60); do grep -q '^listening' "$W/$2" 2>/dev/null && break; sleep 0.5; done; }
serve_s0 'lrpc\WexT1Endpoint' lrpc.out
# a wait finds session 0's endpoint; for one that exists nowhere it fails with error 2 at once, as on Windows (an RPC
# client's connect loop spun for ever when the wait succeeded and the open failed: Office's setup)
check lrpc_wait_finds_session0_endpoint "$(A "$W/p64.exe" pipewait 'lrpc\WexT1Endpoint' | field wait)" ok
MISSING="$(A "$W/p64.exe" pipewait 'lrpc\WexT1Missing')"   # one run, both fields
check lrpc_wait_missing_endpoint "$(field wait <<<"$MISSING")" error2
check lrpc_wait_missing_is_immediate "$(field wait_ms <<<"$MISSING" | awk '{print ($1 < 1500) ? "yes" : "no"}')" yes
check lrpc_endpoint_from_session "$(A "$W/p64.exe" pipesay 'lrpc\WexT1Endpoint' hello | field say)" ok
for i in $(seq 1 40); do grep -q '^message=' "$W/lrpc.out" 2>/dev/null && break; sleep 0.5; done
check lrpc_message_crossed "$(field message < "$W/lrpc.out")" hello
serve_s0 'lrpc\LRPC00000abc.00000001' lrpcpid.out
check lrpc_pid_name_not_bridged "$(A "$W/p64.exe" pipesay 'lrpc\LRPC00000abc.00000001' hello | field say)" error2
s0 "$W/p64.exe" pipesay 'lrpc\LRPC00000abc.00000001' bye >/dev/null 2>&1   # let session 0's server end
check lrpc_session_creates_own_endpoint "$(A "$W/p64.exe" pipecreate 'lrpc\WexT1Local' | field create)" ok
serve_s0 'lrpc\WexT1Own' own-s0.out
rm -f "$W/own-a.out"; (A "$W/p64.exe" pipeserve 'lrpc\WexT1Own' >"$W/own-a.out" &)
for i in $(seq 1 60); do grep -q '^listening' "$W/own-a.out" 2>/dev/null && break; sleep 0.5; done
check lrpc_own_endpoint_first "$(A "$W/p64.exe" pipesay 'lrpc\WexT1Own' mine | field say)" ok
for i in $(seq 1 40); do grep -q '^message=' "$W/own-a.out" 2>/dev/null && break; sleep 0.5; done
check lrpc_own_endpoint_got_it "$(field message < "$W/own-a.out")" mine
s0 "$W/p64.exe" pipesay 'lrpc\WexT1Own' bye >/dev/null 2>&1
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

# B20: a link in a writable directory to a file the user may not write (the machine root's fonts, linked into a
# prefix): CREATE_ALWAYS replaces the link with a file of the user's own and leaves the target alone; a read-only
# open still reads through a link
D20="$W/home/.wine/drive_c/b20"
runuser -u $OTHER -- mkdir -p "$D20"
printf original > "$W/b20-target"; chmod 0644 "$W/b20-target"
ln -s "$W/b20-target" "$D20/font.ttf"; ln -s "$W/b20-target" "$D20/other.ttf"
cp "$W/p64.exe" "$W/home/p64.exe"; chown $OTHER "$W/home/p64.exe"
check b20_overwrite_through_link "$(U timeout 60 ./wine "$W/home/p64.exe" overwrite 'C:\b20\font.ttf' replaced | field overwrite)" ok
check b20_link_replaced_by_file "$( [ -f "$D20/font.ttf" ] && [ ! -L "$D20/font.ttf" ] && cat "$D20/font.ttf")" replaced
check b20_target_untouched "$(cat "$W/b20-target")" original
check b20_read_follows_link "$(U timeout 60 ./wine "$W/home/p64.exe" readfile 'C:\b20\other.ttf' | field read)" original
check b20_other_link_kept "$( [ -L "$D20/other.ttf" ] && echo link)" link
U ./server/wineserver -w
U ./server/wineserver -k; sleep 1  # the next server must start with the session 0 setting
U env WEX_SESSION0_SOCKET=$SOCK timeout 60 ./wine reg query 'HKLM\Software\Microsoft\Windows NT\CurrentVersion' /v CurrentBuild >/dev/null 2>&1
check other_uid_refused "$?" 1
U ./server/wineserver -k; sleep 1

WINEPREFIX=$W/s0 ./server/wineserver -k; sleep 1
end_prefix_procs
check no_process_left_in_the_prefixes "$(end_prefix_procs | grep -c .)" 0
userdel $OTHER 2>/dev/null
grep -a -i 'error\|fatal' "$W/s0.log" | head -5
rm -rf "$W" "$SOCK"
echo "FAILS=$fails"
