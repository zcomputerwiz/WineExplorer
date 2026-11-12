#!/bin/bash
# WineExplorer Desktop Environment - system tier load measurement
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
# Measures system tier performance across two concurrent user sessions (A and B)
# with and without session 0 holding the machine registry.
# Workloads:
#   1. cmd /c exit: N program starts per session (half sequential, half parallel)
#   2. reg query: N sequential queries of HKLM\Software\Microsoft\Windows NT\CurrentVersion
# Measures wall-clock times (off vs on) and session 0 vs user servers CPU time.
# Run as root in the wexbuild WSL environment with BUILD set to a WoW64 build tree.
set -u

BUILD=${BUILD:-/root/WE-prefetch/build/wow64}
W=${WORK:-/root/st-perf}
SOCK=/tmp/wex-perf-session0.sock
PERF_N=${PERF_N:-${1:-40}}
HALF=$(( PERF_N / 2 ))

cd "$BUILD" || exit 2
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all
unset DISPLAY WEX_SESSION0_SOCKET WEX_SESSION0_DEV

CLK_TCK=$(getconf CLK_TCK 2>/dev/null || echo 100)
[ -n "$CLK_TCK" ] && [ "$CLK_TCK" -gt 0 ] 2>/dev/null || CLK_TCK=100

status=0
S0_PID=""
PID_A=""
PID_B=""
PING_PID_A=""
PING_PID_B=""

cmd_seq_off_ms=0
cmd_par_off_ms=0
cmd_seq_on_ms=0
cmd_par_on_ms=0
reg_query_off_ms=0
reg_query_on_ms=0
session0_cpu_ms=0
user_servers_cpu_ms=0

stop_user_servers() {
    if [ -n "$PING_PID_A" ]; then
        kill "$PING_PID_A" 2>/dev/null || true
        wait "$PING_PID_A" 2>/dev/null || true
        PING_PID_A=""
    fi
    if [ -n "$PING_PID_B" ]; then
        kill "$PING_PID_B" 2>/dev/null || true
        wait "$PING_PID_B" 2>/dev/null || true
        PING_PID_B=""
    fi
    WINEPREFIX="$W/a" ./server/wineserver -k 2>/dev/null || true
    WINEPREFIX="$W/a" ./server/wineserver -w 2>/dev/null || true
    WINEPREFIX="$W/b" ./server/wineserver -k 2>/dev/null || true
    WINEPREFIX="$W/b" ./server/wineserver -w 2>/dev/null || true
}

kill_s0() {
    WINEPREFIX="$W/s0" ./server/wineserver -k 2>/dev/null || true
    WINEPREFIX="$W/s0" ./server/wineserver -w 2>/dev/null || true
    if [ -n "$S0_PID" ]; then
        wait "$S0_PID" 2>/dev/null || true
        S0_PID=""
    fi
}

cleanup() {
    stop_user_servers
    kill_s0
    rm -rf "$W" "$SOCK"
}
trap cleanup EXIT

get_server_pid() {
    local pfx="$1"
    for p in /proc/[0-9]*; do
        [ -d "$p" ] || continue
        local comm=""
        if [ -r "$p/comm" ]; then
            comm=$(cat "$p/comm" 2>/dev/null)
        fi
        if [ "$comm" != "wineserver" ]; then
            if [ -r "$p/cmdline" ] && grep -qa "wineserver" "$p/cmdline" 2>/dev/null; then
                comm="wineserver"
            fi
        fi
        [ "$comm" = "wineserver" ] || continue

        if [ -r "$p/environ" ] && tr '\0' '\n' < "$p/environ" 2>/dev/null | grep -Fxq "WINEPREFIX=$pfx"; then
            basename "$p"
            return 0
        fi
    done
    return 1
}

wait_server_pid() {
    local pfx="$1"
    local pid=""
    for i in $(seq 1 50); do
        pid=$(get_server_pid "$pfx")
        if [ -n "$pid" ] && [ -r "/proc/$pid/stat" ]; then
            echo "$pid"
            return 0
        fi
        sleep 0.1
    done
    return 1
}

get_cpu_ticks() {
    local pid="$1"
    if [ -n "$pid" ] && [ -r "/proc/$pid/stat" ]; then
        awk '{
            sub(/.*\)[ \t]*/, "")
            print $12 + $13
        }' "/proc/$pid/stat" 2>/dev/null || echo 0
    else
        echo 0
    fi
}

start_s0() {
    rm -f "$SOCK"
    WINEPREFIX="$W/s0" WEX_SESSION0_SOCKET="$SOCK" WEX_SESSION0_DEV=1 ./server/wineserver --session0 -f >"$W/s0.log" 2>&1 &
    S0_PID=$!
    for i in $(seq 1 50); do
        [ -S "$SOCK" ] && return 0
        sleep 0.1
    done
    echo "ERROR: session 0 did not start" >&2
    cat "$W/s0.log" >&2
    exit 1
}

start_user_servers() {
    (WINEPREFIX="$W/a" timeout 600 ./wine cmd /c "ping -n 590 127.0.0.1 >nul" >/dev/null 2>&1) &
    PING_PID_A=$!
    (WINEPREFIX="$W/b" timeout 600 ./wine cmd /c "ping -n 590 127.0.0.1 >nul" >/dev/null 2>&1) &
    PING_PID_B=$!
    PID_A=$(wait_server_pid "$W/a")
    PID_B=$(wait_server_pid "$W/b")
    if [ -z "$PID_A" ] || [ -z "$PID_B" ]; then
        echo "ERROR: could not locate user server PID for prefix a or b" >&2
        return 1
    fi
    return 0
}

run_cmd_seq() {
    local pfx="$1"
    local count="$2"
    for i in $(seq 1 "$count"); do
        WINEPREFIX="$pfx" timeout 60 ./wine cmd /c exit >/dev/null 2>&1 || return 1
    done
    return 0
}

run_cmd_par() {
    local count="$1"
    local pids=()
    for i in $(seq 1 "$count"); do
        WINEPREFIX="$W/a" timeout 60 ./wine cmd /c exit >/dev/null 2>&1 &
        pids+=($!)
        WINEPREFIX="$W/b" timeout 60 ./wine cmd /c exit >/dev/null 2>&1 &
        pids+=($!)
    done
    local rc=0
    for pid in "${pids[@]}"; do
        wait "$pid" || rc=1
    done
    return $rc
}

run_reg_seq() {
    local pfx="$1"
    local count="$2"
    for i in $(seq 1 "$count"); do
        WINEPREFIX="$pfx" timeout 60 ./wine reg query 'HKLM\Software\Microsoft\Windows NT\CurrentVersion' >/dev/null 2>&1 || return 1
    done
    return 0
}

calc_ratio() {
    local off="$1"
    local on="$2"
    awk -v off="$off" -v on="$on" 'BEGIN {
        if (off > 0) printf "%.2f", on / off;
        else printf "0.00";
    }'
}

print_perf_line() {
    local name="$1"
    local off_ms="$2"
    local on_ms="$3"
    local ratio
    ratio=$(calc_ratio "$off_ms" "$on_ms")
    echo "perf $name off_ms=$off_ms on_ms=$on_ms ratio=$ratio"
}

# Prefix setup
rm -rf "$W"; mkdir -p "$W"
for p in s0 a b; do
    WINEPREFIX="$W/$p" timeout 240 ./wine wineboot -i >/dev/null 2>&1
    WINEPREFIX="$W/$p" ./server/wineserver -w
done

# --------------------------------------------------------------------------
# 1. Baseline: cmd /c exit with session 0 OFF
# --------------------------------------------------------------------------
unset WEX_SESSION0_SOCKET
start_user_servers || status=1

# Sequential phase (half sequential in A and B concurrently)
t0=$(date +%s%N)
run_cmd_seq "$W/a" "$HALF" &
pid_a=$!
run_cmd_seq "$W/b" "$HALF" &
pid_b=$!
wait "$pid_a" || status=1
wait "$pid_b" || status=1
t1=$(date +%s%N)
cmd_seq_off_ms=$(( (t1 - t0) / 1000000 ))

# Parallel phase (half in parallel per session, both sessions at once)
t0=$(date +%s%N)
run_cmd_par "$HALF" || status=1
t1=$(date +%s%N)
cmd_par_off_ms=$(( (t1 - t0) / 1000000 ))

stop_user_servers
sleep 1

# --------------------------------------------------------------------------
# 2. Phase 2: cmd /c exit with session 0 ON & 3. CPU time measurement
# --------------------------------------------------------------------------
start_s0
export WEX_SESSION0_SOCKET="$SOCK"
start_user_servers || status=1

s0_ticks_before=$(get_cpu_ticks "$S0_PID")
a_ticks_before=$(get_cpu_ticks "$PID_A")
b_ticks_before=$(get_cpu_ticks "$PID_B")

# Sequential phase
t0=$(date +%s%N)
run_cmd_seq "$W/a" "$HALF" &
pid_a=$!
run_cmd_seq "$W/b" "$HALF" &
pid_b=$!
wait "$pid_a" || status=1
wait "$pid_b" || status=1
t1=$(date +%s%N)
cmd_seq_on_ms=$(( (t1 - t0) / 1000000 ))

# Parallel phase
t0=$(date +%s%N)
run_cmd_par "$HALF" || status=1
t1=$(date +%s%N)
cmd_par_on_ms=$(( (t1 - t0) / 1000000 ))

s0_ticks_after=$(get_cpu_ticks "$S0_PID")
a_ticks_after=$(get_cpu_ticks "$PID_A")
b_ticks_after=$(get_cpu_ticks "$PID_B")

s0_ticks_delta=$(( s0_ticks_after - s0_ticks_before ))
a_ticks_delta=$(( a_ticks_after - a_ticks_before ))
b_ticks_delta=$(( b_ticks_after - b_ticks_before ))
user_ticks_delta=$(( a_ticks_delta + b_ticks_delta ))

[ "$s0_ticks_delta" -lt 0 ] && s0_ticks_delta=0
[ "$user_ticks_delta" -lt 0 ] && user_ticks_delta=0

session0_cpu_ms=$(( (s0_ticks_delta * 1000) / CLK_TCK ))
user_servers_cpu_ms=$(( (user_ticks_delta * 1000) / CLK_TCK ))

stop_user_servers
sleep 1

# --------------------------------------------------------------------------
# 4. Second workload: reg query sequential, off and on
# --------------------------------------------------------------------------
# reg query OFF
unset WEX_SESSION0_SOCKET
start_user_servers || status=1

t0=$(date +%s%N)
run_reg_seq "$W/a" "$PERF_N" &
pid_a=$!
run_reg_seq "$W/b" "$PERF_N" &
pid_b=$!
wait "$pid_a" || status=1
wait "$pid_b" || status=1
t1=$(date +%s%N)
reg_query_off_ms=$(( (t1 - t0) / 1000000 ))

stop_user_servers
sleep 1

# reg query ON
export WEX_SESSION0_SOCKET="$SOCK"
start_user_servers || status=1

t0=$(date +%s%N)
run_reg_seq "$W/a" "$PERF_N" &
pid_a=$!
run_reg_seq "$W/b" "$PERF_N" &
pid_b=$!
wait "$pid_a" || status=1
wait "$pid_b" || status=1
t1=$(date +%s%N)
reg_query_on_ms=$(( (t1 - t0) / 1000000 ))

stop_user_servers
sleep 1

# --------------------------------------------------------------------------
# Results & Cleanup
# --------------------------------------------------------------------------
kill_s0

print_perf_line "cmd_seq" "$cmd_seq_off_ms" "$cmd_seq_on_ms"
print_perf_line "cmd_par" "$cmd_par_off_ms" "$cmd_par_on_ms"
print_perf_line "reg_query" "$reg_query_off_ms" "$reg_query_on_ms"
echo "perf session0_cpu_ms=$session0_cpu_ms user_servers_cpu_ms=$user_servers_cpu_ms"

exit $status
