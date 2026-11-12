#!/bin/bash
# ST-04: prototype of machine-registry calls against a second server. "nobuild" skips sync and build.
D=/mnt/d/DeepThought/Projects/WineExplorer/unsloth-tmp
B=/root/WE-prefetch/build/wow64
if pgrep -f 'sysrun_server|test_e1s6|test_pam_worker' >/dev/null || pgrep -x make >/dev/null; then echo BUSY; exit 3; fi
if [ "${1:-}" != nobuild ]; then
    bash $D/s9-sync.sh > /tmp/st04-sync.log 2>&1; bash $D/s11-fixlinks.sh >> /tmp/st04-sync.log 2>&1
    bash $D/s9-configure.sh >> /tmp/st04-sync.log 2>&1
    make -C $B -j16 > /tmp/st04-make.log 2>&1; echo "MAIN_EXIT=$?"
    grep -nE 'error:|warning:.*unix/(server|registry)\.c' /tmp/st04-make.log | head -10
fi
cd $B
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all
unset DISPLAY WEX_REGISTRY_SERVER
A=/root/st04-user B0=/root/st04-session0
rm -rf $A $B0
fails=0
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got [$2] want [$3]"; fails=$((fails+1)); fi; }
value() { tr -d '\r' | grep -a "$1" | awk '{print $NF}'; }
K='HKLM\Software\WexTier'
R32='C:\windows\syswow64\reg.exe'

# "session 0": a second prefix with its own, persistent server
WINEPREFIX=$B0 timeout 180 ./wine wineboot -i >/dev/null 2>&1
WINEPREFIX=$B0 ./wine reg add "$K" /v where /d session0 /f >/dev/null 2>&1
WINEPREFIX=$B0 ./server/wineserver -w
WINEPREFIX=$B0 ./server/wineserver -p
WINEPREFIX=$B0 ./wine cmd /c exit >/dev/null 2>&1
SOCK=$(ls -d /tmp/.wine-0/server-*-$(printf '%x' $(stat -c %i $B0))/socket 2>/dev/null)
echo "session 0 socket: $SOCK"
[ -S "$SOCK" ] || { echo "FAIL no session 0 socket"; exit 1; }

# the user's session
export WINEPREFIX=$A
timeout 180 ./wine wineboot -i >/dev/null 2>&1
./wine reg add "$K" /v where /d local /f >/dev/null 2>&1
(timeout 120 ./wine cmd /c "ping -n 100 127.0.0.1 >nul" >/dev/null 2>&1 &)
sleep 3

check off_reads_local64 "$(timeout 60 ./wine reg query "$K" /v where 2>/dev/null | value where)" local
check off_reads_local32 "$(timeout 60 ./wine "$R32" query "$K" /v where /reg:64 2>/dev/null | value where)" local

export WEX_REGISTRY_SERVER=$SOCK
check on_reads_session0_64 "$(timeout 60 ./wine reg query "$K" /v where 2>/dev/null | value where)" session0
check on_reads_session0_32 "$(timeout 60 ./wine "$R32" query "$K" /v where /reg:64 2>/dev/null | value where)" session0
timeout 60 ./wine reg add "$K" /v from64 /d written64 /f >/dev/null 2>&1; check on_write64_exit $? 0
timeout 60 ./wine "$R32" add "$K\\Sub" /v from32 /d written32 /f /reg:64 >/dev/null 2>&1; check on_write32_exit $? 0
timeout 60 ./wine reg add 'HKCU\Software\WexTier' /v user /d stayslocal /f >/dev/null 2>&1; check on_hkcu_write_exit $? 0
check on_enum_subkey "$(timeout 60 ./wine reg query "$K" 2>/dev/null | tr -d '\r' | grep -c 'WexTier.Sub$')" 1
timeout 60 ./wine reg delete "$K" /v from64 /f >/dev/null 2>&1; check on_delete_value_exit $? 0
check on_deleted_gone "$(timeout 60 ./wine reg query "$K" /v from64 2>/dev/null | value from64)" ""
# a program started by a program inherits the switch through the environment
check on_child_reads_session0 "$(timeout 60 ./wine cmd /c "reg query $K /v where" 2>/dev/null | value where)" session0
unset WEX_REGISTRY_SERVER

# what each server holds afterwards
check session0_has_write32 "$(WINEPREFIX=$B0 timeout 60 ./wine reg query "$K\\Sub" /v from32 2>/dev/null | value from32)" written32
check session0_no_hkcu "$(WINEPREFIX=$B0 timeout 60 ./wine reg query 'HKCU\Software\WexTier' /v user 2>/dev/null | value user)" ""
check user_still_local "$(timeout 60 ./wine reg query "$K" /v where 2>/dev/null | value where)" local
check user_has_no_write32 "$(timeout 60 ./wine reg query "$K\\Sub" /v from32 2>/dev/null | value from32)" ""
check user_has_hkcu "$(timeout 60 ./wine reg query 'HKCU\Software\WexTier' /v user 2>/dev/null | value user)" stayslocal

# rough cost: 200 reg.exe reads of one machine value, switch off and on
t0=$(date +%s%N); for i in $(seq 1 20); do timeout 60 ./wine reg query "$K" /v where >/dev/null 2>&1; done; t1=$(date +%s%N)
export WEX_REGISTRY_SERVER=$SOCK
for i in $(seq 1 20); do timeout 60 ./wine reg query "$K" /v where >/dev/null 2>&1; done; t2=$(date +%s%N)
unset WEX_REGISTRY_SERVER
echo "20 reg.exe runs: off $(( (t1 - t0) / 1000000 )) ms, on $(( (t2 - t1) / 1000000 )) ms"

./server/wineserver -k; WINEPREFIX=$B0 ./server/wineserver -k; sleep 1
rm -rf $A $B0
echo "FAILS=$fails"
echo DONE
