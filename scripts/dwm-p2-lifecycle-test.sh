#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# DWM Phase P2 (Broker Lifecycle) automated test suite
# Target: WSL distro wexbuild (root), /root/WE-prefetch/build/wow64
set -u

cd /root/WE-prefetch/build/wow64 || { echo "FAIL: missing build directory"; exit 1; }
export WINEPREFIX=/root/dwm-pfx6 WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=err+explorer
FAILURES=0
DWM_PAT='C:.windows.system32.[d]wm.exe'
EXPL_PAT='C:.windows.system32.[e]xplorer.exe /desktop='
LOG_TEST4=/root/p2-test4-expl.log

cleanup() { pkill -9 -f "$DWM_PAT" 2>/dev/null; pkill -9 -f "$EXPL_PAT" 2>/dev/null; sleep 1; }
wait_dwm() {
    for _ in {1..30}; do
        [ "$(pgrep -f "$DWM_PAT" | wc -l)" -eq "${1:-1}" ] && return 0
        sleep 0.2
    done
    return 1
}

# Initial clean prefix setup
pkill -9 -f '[w]ineserver' 2>/dev/null; cleanup
rm -rf "$WINEPREFIX"
timeout 120 ./wine wineboot -i >/dev/null 2>&1
for n in t1a t1b a b test3 test4 test5; do ./wine reg add "HKCU\\Software\\Wine\\Explorer\\Desktops\\$n" /v EnableShell /t REG_DWORD /d 1 /f >/dev/null 2>&1; done

# (1) DwmService=0 or absent -> no dwm.exe process
(timeout 60 ./wine explorer.exe /desktop=t1a,800x600 >/dev/null 2>&1 &)
sleep 3; pids_absent=$(pgrep -f "$DWM_PAT"); cleanup
./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmService /t REG_DWORD /d 0 /f >/dev/null 2>&1
(timeout 60 ./wine explorer.exe /desktop=t1b,800x600 >/dev/null 2>&1 &)
sleep 3; pids_zero=$(pgrep -f "$DWM_PAT"); cleanup
if [ -z "$pids_absent" ] && [ -z "$pids_zero" ]; then
    echo "PASS: (1) DwmService=0 or absent -> no dwm.exe process"
else
    echo "FAIL: (1) DwmService=0 or absent -> dwm.exe process found"; FAILURES=$((FAILURES + 1))
fi

# (2) two desktops -> exactly two dwm.exe with different /parent values
./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmService /t REG_DWORD /d 1 /f >/dev/null 2>&1
(timeout 60 ./wine explorer.exe /desktop=a,800x600 >/dev/null 2>&1 &)
(timeout 60 ./wine explorer.exe /desktop=b,800x600 >/dev/null 2>&1 &)
wait_dwm 2
dwm_count=$(pgrep -f "$DWM_PAT" | wc -l)
parents=$(ps -eo args | grep -E "$DWM_PAT" | grep -o '/parent=[0-9]*' | sort -u)
parent_count=$(echo "$parents" | grep -c '/parent=' || true)
if [ "$dwm_count" -eq 2 ] && [ "$parent_count" -eq 2 ]; then
    echo "PASS: (2) two desktops -> exactly two dwm.exe with different /parent values ($parents)"
else
    echo "FAIL: (2) two desktops -> expected 2 dwm with distinct parents (count=$dwm_count, distinct=$parent_count)"
    FAILURES=$((FAILURES + 1))
fi
cleanup

# (3) kill -9 dwm.exe -> a new dwm.exe within 3s
(timeout 60 ./wine explorer.exe /desktop=test3,800x600 >/dev/null 2>&1 &)
wait_dwm 1
old_pid=$(pgrep -f "$DWM_PAT" | head -1)
if [ -z "$old_pid" ]; then
    echo "FAIL: (3) dwm.exe failed to start"; FAILURES=$((FAILURES + 1))
else
    kill -9 "$old_pid" 2>/dev/null
    new_pid=""
    for _ in {1..15}; do
        p=$(pgrep -f "$DWM_PAT" | head -1)
        if [ -n "$p" ] && [ "$p" != "$old_pid" ]; then new_pid="$p"; break; fi
        sleep 0.2
    done
    if [ -n "$new_pid" ]; then
        echo "PASS: (3) kill -9 dwm.exe -> a new dwm.exe within 3s (old=$old_pid, new=$new_pid)"
    else
        echo "FAIL: (3) kill -9 dwm.exe -> no new dwm.exe within 3s"; FAILURES=$((FAILURES + 1))
    fi
fi
cleanup

# (4) kill dwm.exe 4 times within 10s -> supervisor gives up
rm -f "$LOG_TEST4"
(timeout 60 ./wine explorer.exe /desktop=test4,800x600 > "$LOG_TEST4" 2>&1 &)
wait_dwm 1
expl_pid=$(pgrep -f "$EXPL_PAT" | head -1)
for _ in {1..4}; do
    kpid=""
    for _ in {1..15}; do
        p=$(pgrep -f "$DWM_PAT" | head -1)
        if [ -n "$p" ]; then kpid="$p"; break; fi
        sleep 0.2
    done
    [ -n "$kpid" ] && kill -9 "$kpid" 2>/dev/null
    sleep 0.5
done
sleep 4
t4_dwm_count=$(pgrep -f "$DWM_PAT" | wc -l)
t4_expl_alive=0; [ -n "$expl_pid" ] && kill -0 "$expl_pid" 2>/dev/null && t4_expl_alive=1
t4_logged=0; grep -q "keeps failing" "$LOG_TEST4" 2>/dev/null && t4_logged=1
if [ "$t4_dwm_count" -eq 0 ] && [ "$t4_expl_alive" -eq 1 ] && [ "$t4_logged" -eq 1 ]; then
    echo "PASS: (4) kill dwm.exe 4 times within 10s -> supervisor gives up (dwm=$t4_dwm_count, expl_alive=$t4_expl_alive, logged=$t4_logged)"
else
    echo "FAIL: (4) supervisor crash loop handling (dwm=$t4_dwm_count, expl_alive=$t4_expl_alive, logged=$t4_logged)"
    FAILURES=$((FAILURES + 1))
fi
cleanup

# (5) kill -9 explorer for a desktop -> its dwm.exe exits within 3s (no orphan)
(timeout 60 ./wine explorer.exe /desktop=test5,800x600 >/dev/null 2>&1 &)
wait_dwm 1
expl_pid=$(pgrep -f "$EXPL_PAT" | head -1)
dwm_pid=$(pgrep -f "$DWM_PAT" | head -1)
if [ -z "$expl_pid" ] || [ -z "$dwm_pid" ]; then
    echo "FAIL: (5) explorer or dwm.exe failed to start"; FAILURES=$((FAILURES + 1))
else
    kill -9 "$expl_pid" 2>/dev/null
    dwm_exited=0
    for _ in {1..15}; do
        if ! kill -0 "$dwm_pid" 2>/dev/null; then dwm_exited=1; break; fi
        sleep 0.2
    done
    if [ "$dwm_exited" -eq 1 ]; then
        echo "PASS: (5) kill -9 explorer -> its dwm.exe exits within 3s (no orphan)"
    else
        echo "FAIL: (5) kill -9 explorer -> dwm.exe ($dwm_pid) did not exit within 3s"; FAILURES=$((FAILURES + 1))
    fi
fi
cleanup

./server/wineserver -k 2>/dev/null; pkill -9 -x wineserver; rm -rf "$WINEPREFIX"
echo "=== DWM Phase P2 Results: $FAILURES failure(s) ==="
exit "$FAILURES"
