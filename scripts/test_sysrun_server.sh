#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Server-side probes of the system role (docs/ELEVATION_SYSTEM_ROLE_WORKORDER.md, phase C2, constraints a to d, f).
# Run as root on a Wine build (WINE_BUILD_DIR, default /home/user/build/dev). It creates a throw-away system
# group, two users (wexadm in sudo, wexstd not), a machine root with the setup script, installs the launcher
# (WEX_SYSRUN_ENABLE=1; the paths are compiled into ntdll, so it uses /etc/wineexplorer and
# /usr/local/libexec/wineexplorer and refuses to run if they exist) and removes everything at the end.
# Needs Xvfb, gcc and the mingw compilers, runuser, setfacl. WEX_C2_KEEP=1 keeps the fixture for inspection.
#
# Checks: (i) a limited client cannot create, write, rename or delete in the machine root through Win32 file APIs
# (the server's path), while a client with the system group can; (ii) a replacement server started by the user
# without the launcher is refused by wex-sysrun and by a client with the system group; (iii) a prefix whose
# system.reg symlink leads to a machine-root file does not get that file overwritten and HKLM still comes from
# the machine root; (iv) HKU\S-1-5-18 and .DEFAULT are not writable by the limited token; (v) the server is not
# ptraceable and stays non-dumpable after the setegid round trips, also with fs.suid_dumpable=1.
set -euo pipefail

[ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 2; }

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${WINE_BUILD_DIR:-/home/user/build/dev}"
ROOT=/var/lib/wexsys-c2
M="$ROOT/machine"
GRP=wexsysc2
ADM=wexadm
STD=wexstd
CONF=/etc/wineexplorer/sysrun.conf
LIBEXEC=/usr/local/libexec/wineexplorer
SYSRUN="$LIBEXEC/wex-sysrun"
WINM="Z:${M//\//\\}"
PASS=0
DUMPABLE_SAVED=""
XV_PID=""

# WEX_C3_REUSE=1 reuses the fixture that an earlier run left with WEX_C2_KEEP=1 (development of the checks)
REUSE="${WEX_C3_REUSE:-0}"
if [ "$REUSE" != 1 ]; then
for p in "$ROOT" /etc/wineexplorer "$LIBEXEC"; do
    [ ! -e "$p" ] || { echo "$p exists; remove it first" >&2; exit 2; }
done
getent group "$GRP" >/dev/null && { echo "group $GRP exists" >&2; exit 2; }
for u in "$ADM" "$STD"; do id "$u" >/dev/null 2>&1 && { echo "user $u exists" >&2; exit 2; }; done
fi

fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { echo "$1=PASS"; PASS=$((PASS + 1)); }

kill_servers()
{
    local u
    for u in "$ADM" "$STD"; do
        id "$u" >/dev/null 2>&1 || continue
        pkill -u "$u" -x wineserver 2>/dev/null || true
    done
    sleep 1
}

cleanup()
{
    local rc=$?
    set +e
    ! type standin_off >/dev/null 2>&1 || standin_off
    [ -z "$DUMPABLE_SAVED" ] || echo "$DUMPABLE_SAVED" > /proc/sys/fs/suid_dumpable
    kill_servers
    pkill -u "$ADM" 2>/dev/null; pkill -u "$STD" 2>/dev/null
    [ -z "$XV_PID" ] || kill "$XV_PID" 2>/dev/null
    if [ "${WEX_C2_KEEP:-0}" = 1 ]; then
        echo "fixture kept: $ROOT, users $ADM $STD, group $GRP, $CONF, $LIBEXEC" >&2
        exit "$rc"
    fi
    for u in "$ADM" "$STD"; do
        uid="$(id -u "$u" 2>/dev/null)"
        userdel -r "$u" 2>/dev/null
        [ -z "$uid" ] || rm -rf "/tmp/.wine-$uid"
    done
    groupdel "$GRP" 2>/dev/null
    rm -rf "$ROOT" /etc/wineexplorer "$LIBEXEC"
    rmdir /usr/local/libexec 2>/dev/null
    exit "$rc"
}
trap cleanup EXIT

# ---- fixture -------------------------------------------------------------------------------------------------
install -d -m 0755 "$ROOT" "$ROOT/bin" "$ROOT/probes"
[ "$REUSE" = 1 ] || groupadd --system "$GRP"
coproc XV { Xvfb -displayfd 1 -screen 0 1024x768x24 -nolisten tcp 2>/dev/null; }
read -r -u "${XV[0]}" DNUM
[ -n "$DNUM" ] || fail "Xvfb did not start"

x86_64-w64-mingw32-gcc -O2 -Wall -o "$ROOT/probes/probe64.exe" "$SRC/scripts/sysrun_server_probe.c" -ladvapi32
i686-w64-mingw32-gcc -O2 -Wall -o "$ROOT/probes/probe32.exe" "$SRC/scripts/sysrun_server_probe.c" -ladvapi32
x86_64-w64-mingw32-gcc -O2 -Wall -o "$ROOT/probes/rolesprobe64.exe" "$SRC/scripts/sysrun_roles_probe.c" -ladvapi32
i686-w64-mingw32-gcc -O2 -Wall -o "$ROOT/probes/rolesprobe32.exe" "$SRC/scripts/sysrun_roles_probe.c" -ladvapi32
cp "$ROOT/probes/rolesprobe64.exe" "$ROOT/probes/wexsvc.exe"     # the service image, so that its process has its own name
gcc -O2 -Wall -o "$ROOT/bin/asgroup" "$SRC/scripts/sysrun_asgroup.c"
chmod 0755 "$ROOT/probes"/*.exe

if [ "$REUSE" != 1 ]; then
echo "--- machine root and launcher"
env WEX_MACHINE_GROUP="$GRP" WEX_MACHINE_ROOT="$M" WEX_SKEL_PFX="$ROOT/skel/.wine" WEX_BACKUP_DIR="$ROOT/backup" \
    WEX_WINE_BIN="$BUILD/wine" WEX_WINESERVER_BIN="$BUILD/server/wineserver" WEX_NTDLL="$BUILD/dlls/ntdll/ntdll.so" \
    WEX_SYSRUN_ENABLE=1 DISPLAY=":$DNUM" WINEDEBUG=-all \
    bash "$SRC/scripts/setup-machine-root.sh" > "$ROOT/setup.log" 2>&1 || { tail -20 "$ROOT/setup.log" >&2; fail "setup failed"; }
[ "$(stat -c '%a %U:%G' "$SYSRUN")" = "2755 root:$GRP" ] || fail "launcher not installed setgid"
fi
install -m 2755 -o root -g "$GRP" "$ROOT/bin/asgroup" "$ROOT/bin/asgroup.sg"
GRP_GID="$(getent group "$GRP" | cut -d: -f3)"

if [ "$REUSE" != 1 ]; then
for u in "$ADM" "$STD"; do
    useradd -m -k "$ROOT/skel" -s /bin/bash "$u"
done
usermod -a -G sudo "$ADM"
fi
ADM_GID="$(id -g "$ADM")"; STD_GID="$(id -g "$STD")"; STD_UID="$(id -u "$STD")"; ADM_UID="$(id -u "$ADM")"

# Values for the probes, written by root into the machine hives before any server runs.
reg_append()   # file section line...
{
    local file="$1" section="$2"; shift 2
    { printf '\n[%s] 1700000000\n#time=1d9a0000000000\n' "$section"; printf '%s\n' "$@"; } >> "$file"
}
if [ "$REUSE" != 1 ]; then
printf 'MARKER' > "$M/drive_c/windows/wex_marker.txt"
printf 'MARKER2' > "$M/drive_c/windows/wex_marker2.txt"
chown "root:$GRP" "$M/drive_c/windows/wex_marker.txt" "$M/drive_c/windows/wex_marker2.txt"
chmod 664 "$M/drive_c/windows/wex_marker.txt" "$M/drive_c/windows/wex_marker2.txt"
reg_append "$M/system.reg" 'Software\\WexProbe' '"FromMachine"="machine"'
fi

run_as() { local u="$1"; shift; (cd /tmp && runuser -u "$u" -- env -i HOME="/home/$u" USER="$u" LOGNAME="$u" PATH=/usr/bin:/bin LANG=C \
    DISPLAY=":$DNUM" WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' "$@"); }
wine_as() { local u="$1"; shift; run_as "$u" "$BUILD/wine" "$@"; }
probe() { local u="$1" arch="$2" mode="$3"; shift 3; wine_as "$u" "$ROOT/probes/probe$arch.exe" "$mode" "$WINM" "$@" 2>&1; }
server_pid()   # a live wineserver of the user (a killed one can stay a zombie in a container without init)
{
    local p
    for p in $(pgrep -u "$1" -x wineserver); do
        grep -q '^State:.*Z' "/proc/$p/status" 2>/dev/null || { echo "$p"; return; }
    done
}
prefix_server_env() { run_as "$1" env WINEPREFIX="/home/$1/.wine" "$BUILD/server/wineserver" "${@:2}"; }

hold_start() { probe "$STD" 64 hold >/dev/null 2>&1 & HOLD=$!; sleep 4; }   # a client keeps the server alive
hold_stop() { prefix_server_env "$STD" -k || true; prefix_server_env "$STD" -w; wait "$HOLD" 2>/dev/null || true; }

expect_pass()   # name user arch mode
{
    local name="$1" out
    out="$(probe "${@:2}")" || { echo "$out" >&2; fail "$name"; }
    grep -q "^$4=PASS" <<<"$out" || { echo "$out" >&2; fail "$name: no PASS line"; }
    ok "$name"
}

if [ "${WEX_C3_ONLY:-0}" != 1 ]; then   # WEX_C3_ONLY=1 runs only the system role checks (sections 7 and 8)
# ---- 1. the server starts through the launcher ----------------------------------------------------------------
echo "--- launcher-started server"
hold_start   # the first wine start of the user starts the server through the launcher
OUT="$(probe "$STD" 64 hklm-read)" || { echo "$OUT" >&2; fail "first wine start as $STD: $OUT"; }
grep -q '^hklm-read=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "HKLM did not come from the machine root"; }
ok c2_hklm_comes_from_machine_root
SPID="$(server_pid "$STD")"; [ -n "$SPID" ] || fail "no server of $STD"
IDS="$(awk '/^Uid:/ { u = $2 "/" $3 "/" $4 } /^Gid:/ { print u, $2, $3, $4 }' "/proc/$SPID/status")"
[ "$IDS" = "$STD_UID/$STD_UID/$STD_UID $STD_GID $STD_GID $GRP_GID" ] || fail "server ids are not (user, user, saved $GRP): $IDS"
ok c2_server_started_by_launcher_effective_gid_dropped
[ -s "$M/.update-timestamp" ] || fail "no machine .update-timestamp"
SVC="$(pgrep -u "$STD" -f services.exe | head -1 || true)"
[ -n "$SVC" ] || fail "no services.exe"
SG="$(awk '/^Gid:/ { print $2, $3, $4 }' "/proc/$SVC/status")"
[ "$SG" = "$STD_GID $GRP_GID $GRP_GID" ] || fail "services.exe ids are not (user, $GRP, $GRP): $SG"
ok c2_services_exe_runs_with_system_group

# ---- 2. (i) limited client cannot write the machine root; a system group client can ----------------------------
echo "--- files (i)"
for arch in 64 32; do
    expect_pass "c2_limited_files_denied_$arch" "$STD" "$arch" files-denied
done
[ "$(cat "$M/drive_c/windows/wex_marker.txt")" = MARKER ] || fail "the marker in the machine root changed under a limited client"
[ -z "$(ls "$M/drive_c/windows" | grep -E '^wex_(probe|user_probe)' || true)" ] ||
    fail "files of a limited client appeared in the machine root: $(ls "$M/drive_c/windows" | grep -E '^wex_(probe|user_probe)')"
ok c2_machine_root_unchanged_after_limited_client
for arch in 64 32; do
    OUT="$(run_as "$STD" "$ROOT/bin/asgroup.sg" "$BUILD/wine" "$ROOT/probes/probe$arch.exe" files-allowed "$WINM" 2>&1)" ||
        { echo "$OUT" >&2; fail "files-allowed $arch"; }
    grep -q '^files-allowed=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "files-allowed $arch: no PASS"; }
    ok "c2_system_group_client_can_write_machine_root_$arch"
done

# ---- 3. (iii) a system.reg symlink into the machine root ------------------------------------------------------
echo "--- symlinked hives (iii)"
prefix_server_env "$STD" -k || true; prefix_server_env "$STD" -w
for hive in system.reg userdef.reg; do
    ls -l "/home/$STD/.wine/$hive" | grep -q -- '->' || fail "skeleton prefix has no $hive link"
done
for hive in system.reg userdef.reg; do
    run_as "$STD" ln -sfn "$M/drive_c/windows/wex_marker2.txt" "/home/$STD/.wine/$hive"
done
probe "$STD" 64 hklm-write >/dev/null || fail "hklm-write"
OUT="$(probe "$STD" 64 hklm-read)"; grep -q '^hklm-read=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "HKLM not from the machine root with a diverted prefix link"; }
ok c2_hklm_from_machine_root_with_diverted_symlink
prefix_server_env "$STD" -k || true; prefix_server_env "$STD" -w
[ "$(cat "$M/drive_c/windows/wex_marker2.txt")" = MARKER2 ] || fail "the machine-root file behind the prefix symlink was overwritten"
ok c2_symlink_target_not_overwritten
grep -q '"Written"="yes"' "$M/system.reg" || fail "the machine hive was not saved to the machine root"
ok c2_machine_hive_saved_with_raised_group
[ "$(stat -c '%U:%G' "$M/system.reg" "$M/userdef.reg" | sort -u)" = "root:$GRP" ] ||
    fail "a saved machine hive is not root:$GRP any more (its owner could change it): $(stat -c '%U:%G %n' "$M/system.reg" "$M/userdef.reg")"
ok c2_machine_hives_stay_root_owned_after_save
for hive in system.reg userdef.reg; do
    [ "$(readlink "/home/$STD/.wine/$hive")" = "$M/drive_c/windows/wex_marker2.txt" ] || fail "the prefix link $hive was replaced"
    run_as "$STD" ln -sfn "$M/$hive" "/home/$STD/.wine/$hive"
done
ok c2_prefix_links_left_alone

# ---- 4. (ii) a replacement server ------------------------------------------------------------------------------
echo "--- replacement server (ii)"
prefix_server_env "$STD" -p                # a plain server, no launcher: egid = gid = the user's
RPID="$(server_pid "$STD")"; [ -n "$RPID" ] || fail "replacement server did not start"
RIDS="$(awk '/^Gid:/ { print $2, $3, $4 }' "/proc/$RPID/status")"
[ "$RIDS" = "$STD_GID $STD_GID $STD_GID" ] || fail "replacement server ids: $RIDS"
set +e
OUT="$(run_as "$STD" "$SYSRUN" wineboot-init 2>&1 </dev/null)"; rc=$?
set -e
[ "$rc" -eq 77 ] && grep -q "the server was not started by this launcher" <<<"$OUT" || fail "launcher accepted a replacement server (rc $rc): $OUT"
ok c2_launcher_refuses_replacement_server
set +e
OUT="$(run_as "$STD" "$ROOT/bin/asgroup.sg" "$BUILD/wine" "$ROOT/probes/probe64.exe" files-allowed "$WINM" 2>&1)"; rc=$?
set -e
[ "$rc" -ne 0 ] && grep -q "refusing a wine server that was not started by the system role launcher" <<<"$OUT" ||
    fail "a client with the system group used a replacement server (rc $rc): $OUT"
ok c2_system_group_client_refuses_replacement_server
[ ! -e "$M/drive_c/windows/wex_wexsys_made.txt" ] || fail "the replacement server was used to write the machine root"
# a limited client of the replacement server is a normal Wine client: it still cannot write the machine root.
# The first client starts wineboot and services.exe, which keep its output open: run it with the output discarded.
probe "$STD" 64 hklm-read >/dev/null 2>&1 || true
expect_pass c2_limited_files_denied_on_replacement_server "$STD" 64 files-denied
prefix_server_env "$STD" -k || true; prefix_server_env "$STD" -w

# ---- 5. wineboot --init runs with the system group and writes the machine root through the server --------------
echo "--- wineboot --init through the launcher"
rm -f "$M/.update-timestamp"
hold_start
OUT="$(probe "$STD" 64 hklm-read)" || fail "start after removing .update-timestamp: $OUT"
[ -f "$M/.update-timestamp" ] || fail "wineboot-init did not write the machine root's .update-timestamp"
[ "$(stat -c '%u:%G' "$M/.update-timestamp")" = "$STD_UID:$GRP" ] || fail ".update-timestamp owner or group: $(stat -c '%u:%G' "$M/.update-timestamp")"
ok c2_wineboot_init_with_system_group_writes_machine_root
hold_stop

# ---- 6. (v) not ptraceable, non-dumpable after the setegid round trips ---------------------------------------
echo "--- ptrace and dumpable (v)"
cat > "$ROOT/ptrace_try.py" <<'EOF'
import ctypes, os, sys, signal
libc = ctypes.CDLL(None, use_errno=True)
libc.ptrace.argtypes = [ctypes.c_long, ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p]
def attach(pid):
    r = libc.ptrace(16, pid, None, None)   # PTRACE_ATTACH
    e = ctypes.get_errno()
    if r == 0:
        os.waitpid(pid, 0)
        libc.ptrace(17, pid, None, None)   # PTRACE_DETACH
    return r, e
def memopen(pid):
    try:
        os.close(os.open("/proc/%d/mem" % pid, os.O_RDONLY))
        return 0
    except OSError as ex:
        return ex.errno
target = int(sys.argv[1])
child = os.fork()
if child == 0:
    signal.pause()
    os._exit(0)
r, e = attach(child)
cm = memopen(child)
os.kill(child, signal.SIGKILL); os.waitpid(child, 0)
print("CONTROL_ATTACH", r, e, "CONTROL_MEM", cm)
r, e = attach(target)
print("TARGET_ATTACH", r, e, "TARGET_MEM", memopen(target))
EOF
check_server_protected()   # label
{
    local pid ids out
    pid="$(server_pid "$STD")"; [ -n "$pid" ] || fail "$1: no server"
    ids="$(awk '/^Gid:/ { print $2, $3, $4 }' "/proc/$pid/status")"
    [ "$ids" = "$STD_GID $STD_GID $GRP_GID" ] || fail "$1: server gids $ids"
    [ "$(stat -c %U "/proc/$pid/environ")" = root ] || fail "$1: server is dumpable (/proc/$pid/environ is not root-owned)"
    out="$(run_as "$STD" python3 -I "$ROOT/ptrace_try.py" "$pid" 2>&1)" || fail "$1: ptrace helper: $out"
    case "$out" in *"CONTROL_ATTACH 0 "*"CONTROL_MEM 0"*) ;; *) fail "$1: control ptrace failed: $out" ;; esac
    case "$out" in
        *"TARGET_ATTACH -1 1 "*"TARGET_MEM 13"*|*"TARGET_ATTACH -1 13 "*"TARGET_MEM 13"*) ;;
        *) fail "$1: ptrace or /proc/pid/mem of the server not denied: $out" ;;
    esac
    ok "c2_server_not_ptraceable_non_dumpable_$1"
}
hold_start
probe "$STD" 64 hklm-write >/dev/null   # a machine hive change: the next save raises and drops the group
check_server_protected before_save
hold_stop                                # the exit flushes the hive: a raise and a drop
hold_start
probe "$STD" 64 hklm-read >/dev/null
for _ in 1 2 3; do probe "$STD" 64 files-denied >/dev/null; done      # opens by a limited client: no raise
OUT="$(run_as "$STD" "$ROOT/bin/asgroup.sg" "$BUILD/wine" "$ROOT/probes/probe64.exe" files-allowed "$WINM" 2>&1)" || fail "files-allowed: $OUT"
sleep 32                                 # at least one periodic save
check_server_protected after_round_trips
# with fs.suid_dumpable=1 a credential change would make the process dumpable unless the server resets the flag
DUMPABLE_SAVED="$(cat /proc/sys/fs/suid_dumpable)"
if echo 1 > /proc/sys/fs/suid_dumpable 2>/dev/null; then
    hold_stop
    hold_start
    probe "$STD" 64 hklm-write >/dev/null
    OUT="$(run_as "$STD" "$ROOT/bin/asgroup.sg" "$BUILD/wine" "$ROOT/probes/probe64.exe" files-allowed "$WINM" 2>&1)" || fail "files-allowed: $OUT"
    check_server_protected suid_dumpable_1
    echo "$DUMPABLE_SAVED" > /proc/sys/fs/suid_dumpable; DUMPABLE_SAVED=""
else
    echo "NOTE: fs.suid_dumpable is read-only here, the suid_dumpable=1 case was not run"
    DUMPABLE_SAVED=""
fi
hold_stop

fi   # WEX_C3_ONLY

# ---- 7. (iv) HKU well-known profiles, with elevation enforced -------------------------------------------------
echo "--- registry (iv), elevation enforced"
# the machine hive as it is before the enforcement and the test services are added (a reused fixture starts from it)
if [ "$REUSE" = 1 ]; then cat "$ROOT/system.reg.pre" > "$M/system.reg"; else cp "$M/system.reg" "$ROOT/system.reg.pre"; fi
reg_append "$M/system.reg" 'Software\\Wine\\Elevation' '"Helper"="C:\\windows\\system32\\consent.exe"'
for u in "$STD" "$ADM"; do
    [ "${WEX_C3_ONLY:-0}" != 1 ] || [ "${WEX_C3_SKIP7:-0}" != 1 ] || continue
    for arch in 64 32; do
        expect_pass "c2_enforced_hku_service_profiles_not_writable_${u}_$arch" "$u" "$arch" registry
        expect_pass "c2_enforced_limited_files_denied_${u}_$arch" "$u" "$arch" files-denied
    done
    prefix_server_env "$u" -k || true; prefix_server_env "$u" -w
done

# ---- 8. the system role (C3), elevation enforced --------------------------------------------------------------
# services.exe, winedevice.exe and the LocalSystem service report SYSTEM; the LocalService and NetworkService
# services report their accounts and have no system group; a limited and a standard user cannot get at the system
# processes, cannot create or reconfigure a service, cannot make tokens; an administrator token can open them;
# a copy of a system program in a user directory gets no role; in a prefix without the launcher nothing changes.
echo "--- system role (C3)"
SVCDIR=/tmp/wexsvc
SVCEXE='Z:\\var\\lib\\wexsys-c2\\probes\\wexsvc.exe'
add_service()   # name account report-name start-type
{
    reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\'"$1" '"ErrorControl"=dword:00000001' \
        '"ImagePath"="'"$SVCEXE svc $1 "'Z:\\tmp\\wexsvc\\'"$3"'.txt Z:\\var\\lib\\wexsys-c2\\machine"' \
        '"ObjectName"="'"$2"'"' '"Start"=dword:0000000'"$4" '"Type"=dword:00000010'
}
add_service WexSvcSys 'LocalSystem' sys 2
add_service WexSvcLs 'NT AUTHORITY\\LocalService' ls 2
add_service WexSvcNs 'NT AUTHORITY\\NetworkService' ns 2
add_service WexSvcBad 'wexstd' bad 3

fact() { grep -m1 "^$2=" "$1" | cut -d= -f2-; }
expect_fact()   # name file key want
{
    local got; got="$(fact "$2" "$3")"
    [ "$got" = "$4" ] || { cat "$2" >&2; fail "$1: $3 is '$got', want '$4' ($2)"; }
    ok "$1"
}
expect_privs()   # name file want...
{
    local name="$1" file="$2" got want; shift 2
    got="$(grep '^priv=' "$file" | LC_ALL=C sort | tr '\n' ' ')"
    want="$(printf 'priv=%s\n' "$@" | LC_ALL=C sort | tr '\n' ' ')"
    [ "$got" = "$want" ] || { cat "$file" >&2; fail "$name: privileges '$got', want '$want'"; }
    ok "$name"
}
expect_in()   # name file line
{
    grep -qxF -- "$3" "$2" || { cat "$2" >&2; fail "$1: '$3' not in $2"; }
    ok "$1"
}
wait_files() { local i f all; for i in $(seq 1 60); do all=1; for f in "$@"; do [ -s "$f" ] || all=0; done; [ $all = 1 ] && return 0; sleep 2; done; fail "no service report: $*"; }
svc_pid() { local p; for p in $(pgrep -u "$1" -f "wexsvc.exe svc $2 "); do grep -q '^State:.*Z' "/proc/$p/status" 2>/dev/null || { echo "$p"; return; }; done; }
proc_gids() { awk '/^Gid:/ { print $2, $3, $4 }' "/proc/$1/status"; }
rprobe() { local u="$1" arch="$2" mode="$3"; shift 3; wine_as "$u" "$ROOT/probes/rolesprobe$arch.exe" "$mode" "$@" 2>&1 | tr -d '\r'; }
expect_rpass()   # name user arch mode args...
{
    local name="$1" out; out="$(rprobe "${@:2}")" || { echo "$out" >&2; fail "$name"; }
    grep -q "^$4=PASS" <<<"$out" || { echo "$out" >&2; fail "$name: no PASS line"; }
    ok "$name"
}
# a stand-in for a system role program: the probe in place of the machine root's winedevice.exe, started with the
# system group (that is what the launcher does for the fixed programs); the image is what the server checks
STANDIN_ON=0
standin_on()
{
    local d arch
    for d in system32 syswow64; do
        [ "$d" = system32 ] && arch=64 || arch=32
        mv "$M/drive_c/windows/$d/winedevice.exe" "$M/drive_c/windows/$d/winedevice.exe.c3orig"
        install -m 775 -o root -g "$GRP" "$ROOT/probes/rolesprobe$arch.exe" "$M/drive_c/windows/$d/winedevice.exe"
    done
    STANDIN_ON=1
}
standin_off()
{
    local d
    [ "$STANDIN_ON" = 1 ] || return 0
    for d in system32 syswow64; do mv -f "$M/drive_c/windows/$d/winedevice.exe.c3orig" "$M/drive_c/windows/$d/winedevice.exe"; done
    STANDIN_ON=0
}
sys_probe()   # user arch mode args...: the probe as a system role program
{
    local u="$1" arch="$2" mode="$3" img="$M/drive_c/windows/system32/winedevice.exe"; shift 3
    [ "$arch" = 32 ] && img="$M/drive_c/windows/syswow64/winedevice.exe"
    run_as "$u" "$ROOT/bin/asgroup.sg" "$BUILD/wine" "$img" "$mode" "$@" 2>&1 | tr -d '\r'
}
expect_spass()   # name user arch mode args...
{
    local name="$1" out; out="$(sys_probe "${@:2}")" || { echo "$out" >&2; fail "$name"; }
    grep -q "^$4=PASS" <<<"$out" || { echo "$out" >&2; fail "$name: no PASS line"; }
    ok "$name"
}

C3_USERS="${WEX_C3_USERS:-$STD $ADM}"; [ "$C3_USERS" != - ] || C3_USERS=""   # WEX_C3_USERS restricts the users (development)
for u in $C3_USERS; do
    rm -rf "$SVCDIR"; install -d -m 1777 "$SVCDIR"
    echo "--- system role: $u"
    probe "$u" 64 hold >/dev/null 2>&1 & HOLD=$!
    sleep 4
    wait_files "$SVCDIR/sys.txt" "$SVCDIR/ls.txt" "$SVCDIR/ns.txt"

    if [ "$u" = "$STD" ]; then
        # -- the tokens the services report
        expect_fact c3_sys_service_user "$SVCDIR/sys.txt" user S-1-5-18
        expect_fact c3_sys_service_integrity_system "$SVCDIR/sys.txt" integrity 16384
        expect_fact c3_sys_service_administrators_enabled "$SVCDIR/sys.txt" admins enabled
        expect_fact c3_sys_service_owner_administrators "$SVCDIR/sys.txt" owner S-1-5-32-544
        expect_fact c3_sys_service_elevation_default "$SVCDIR/sys.txt" elevation 1
        expect_fact c3_sys_service_no_linked_token "$SVCDIR/sys.txt" linked none
        expect_fact c3_sys_service_primary "$SVCDIR/sys.txt" type 1
        expect_in c3_sys_service_tcb_enabled "$SVCDIR/sys.txt" priv=SeTcbPrivilege:E
        expect_in c3_sys_service_debug_enabled "$SVCDIR/sys.txt" priv=SeDebugPrivilege:E
        expect_in c3_sys_service_assignprimary_disabled "$SVCDIR/sys.txt" priv=SeAssignPrimaryTokenPrivilege:D
        expect_fact c3_sys_service_writes_machine_root "$SVCDIR/sys.txt" machine_root_write allowed
        for who in ls:S-1-5-19:LocalService ns:S-1-5-20:NetworkService; do
            f="$SVCDIR/${who%%:*}.txt"; rest="${who#*:}"; sid="${rest%%:*}"; nm="${rest#*:}"
            expect_fact "c3_${nm}_service_user" "$f" user "$sid"
            expect_fact "c3_${nm}_service_owner_is_user" "$f" owner "$sid"
            expect_fact "c3_${nm}_service_no_administrators" "$f" admins absent
            expect_fact "c3_${nm}_service_integrity_system" "$f" integrity 16384
            expect_fact "c3_${nm}_service_elevation_default" "$f" elevation 1
            expect_fact "c3_${nm}_service_no_linked_token" "$f" linked none
            expect_in "c3_${nm}_service_in_service_group" "$f" group=S-1-5-6:enabled
            expect_in "c3_${nm}_service_in_local_group" "$f" group=S-1-2-0:enabled
            expect_in "c3_${nm}_service_in_authenticated_users" "$f" group=S-1-5-11:enabled
            expect_in "c3_${nm}_service_in_everyone" "$f" group=S-1-1-0:enabled
            expect_fact "c3_${nm}_service_cannot_write_machine_root" "$f" machine_root_write denied
        done
        expect_privs c3_localservice_service_privileges "$SVCDIR/ls.txt" SeAssignPrimaryTokenPrivilege:D SeAuditPrivilege:D \
            SeChangeNotifyPrivilege:E SeCreateGlobalPrivilege:E SeImpersonatePrivilege:E SeShutdownPrivilege:D SeUndockPrivilege:D
        expect_privs c3_networkservice_service_privileges "$SVCDIR/ns.txt" SeAssignPrimaryTokenPrivilege:D SeAuditPrivilege:D \
            SeChangeNotifyPrivilege:E SeCreateGlobalPrivilege:E SeImpersonatePrivilege:E SeIncreaseQuotaPrivilege:D

        # -- the Unix side: the system service keeps the system group, the other two lose it and are not dumpable
        P="$(svc_pid "$u" WexSvcSys)"; [ -n "$P" ] || fail "no WexSvcSys process"
        [ "$(proc_gids "$P")" = "$STD_GID $GRP_GID $GRP_GID" ] || fail "WexSvcSys gids: $(proc_gids "$P")"
        ok c3_sys_service_process_has_system_group
        [ "$(stat -c %U "/proc/$P/environ")" = root ] || fail "the LocalSystem service is dumpable, the user could ptrace it"
        ok c3_sys_service_process_is_not_dumpable
        for img in services.exe winedevice.exe; do
            for P in $(pgrep -u "$u" -f "windows.system32.$img"); do
                grep -q '^State:.*Z' "/proc/$P/status" 2>/dev/null && continue
                [ "$(stat -c %U "/proc/$P/environ")" = root ] || fail "$img ($P) is dumpable, the user could ptrace it"
            done
        done
        ok c3_system_programs_are_not_dumpable
        for nm in WexSvcLs WexSvcNs; do
            P="$(svc_pid "$u" "$nm")"; [ -n "$P" ] || fail "no $nm process"
            [ "$(proc_gids "$P")" = "$STD_GID $STD_GID $STD_GID" ] || fail "$nm gids: $(proc_gids "$P")"
            [ "$(stat -c %U "/proc/$P/environ")" = root ] || fail "$nm is dumpable, the user could ptrace it"
            ok "c3_${nm}_process_has_no_system_group_and_is_not_dumpable"
        done

        # -- starting a service of an account that has no token yet is refused with ERROR_SERVICE_LOGON_FAILED
        standin_on
        expect_spass c3_service_of_a_user_account_is_refused "$u" 64 start-service WexSvcBad 1069

        # -- services.exe, winedevice.exe: the token of the system role processes
        for arch in 64 32; do
            OUT="$(sys_probe "$u" "$arch" proc-token services.exe)" || { echo "$OUT" >&2; fail "proc-token services.exe"; }
            [ "$(grep -c '^user=S-1-5-18$' <<<"$OUT")" -ge 1 ] && [ "$(grep -c '^user=' <<<"$OUT")" = "$(grep -c '^user=S-1-5-18$' <<<"$OUT")" ] ||
                { echo "$OUT" >&2; fail "services.exe token is not SYSTEM"; }
            grep -q '^integrity=16384$' <<<"$OUT" && grep -q '^admins=enabled$' <<<"$OUT" || { echo "$OUT" >&2; fail "services.exe integrity or administrators"; }
            ok "c3_services_exe_is_system_$arch"
        done
        OUT="$(sys_probe "$u" 64 proc-token winedevice.exe)" || { echo "$OUT" >&2; fail "proc-token winedevice.exe"; }
        [ "$(grep -c '^user=' <<<"$OUT")" -ge 1 ] && [ "$(grep -c '^user=' <<<"$OUT")" = "$(grep -c '^user=S-1-5-18$' <<<"$OUT")" ] ||
            { echo "$OUT" >&2; fail "winedevice.exe token is not SYSTEM"; }
        grep -q '^integrity=16384$' <<<"$OUT" || { echo "$OUT" >&2; fail "winedevice.exe integrity"; }
        ok c3_winedevice_exe_is_system
        OUT="$(sys_probe "$u" 64 token)" || { echo "$OUT" >&2; fail "stand-in token"; }
        grep -q '^user=S-1-5-18$' <<<"$OUT" && grep -q '^integrity=16384$' <<<"$OUT" || { echo "$OUT" >&2; fail "the verified system program has no SYSTEM token"; }
        ok c3_verified_system_image_gets_system_token

        # -- the three tokens of the request, and the request is only for the system role
        for arch in 64 32; do
            OUT="$(sys_probe "$u" "$arch" service-tokens)" || { echo "$OUT" >&2; fail "service-tokens $arch"; }
            grep -q '^service-tokens=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "service-tokens $arch"; }
            ok "c3_service_token_request_by_system_role_$arch"
        done

        # -- Services\<name>\Security: the descriptor of a service, read from the registry, written with WRITE_DAC
        expect_rpass c3_default_descriptor_does_not_let_users_start "$u" 64 svc-start WexSvcLs denied
        expect_spass c3_set_service_object_security_by_the_system_role "$u" 64 svc-sd-set WexSvcLs
        expect_rpass c3_service_descriptor_that_was_set_applies "$u" 64 svc-start WexSvcLs allowed
        expect_rpass c3_other_services_keep_the_default_descriptor "$u" 64 svc-start WexSvcNs denied

        # -- a copy of a system program in a user directory gets no role; with the system group it is killed
        run_as "$u" install -d "/home/$u/usr"
        run_as "$u" cp "$ROOT/probes/rolesprobe64.exe" "/home/$u/usr/services.exe"
        OUT="$(wine_as "$u" "/home/$u/usr/services.exe" token 2>&1 | tr -d '\r')" || { echo "$OUT" >&2; fail "user copy of services.exe"; }
        grep -q '^user=S-1-5-18$' <<<"$OUT" && { echo "$OUT" >&2; fail "a user copy of services.exe got SYSTEM"; }
        grep -q '^admins=enabled$' <<<"$OUT" && { echo "$OUT" >&2; fail "a user copy of services.exe got Administrators"; }
        ok c3_user_copy_of_services_exe_gets_no_system_role
        # the server stops it (from its main loop, a moment after the image is seen); it must not keep running, and the
        # server must survive it
        run_as "$u" "$ROOT/bin/asgroup.sg" "$BUILD/wine" "/home/$u/usr/services.exe" hold >/dev/null 2>&1 &
        STOPPED=$!
        sleep 12
        [ -z "$(pgrep -u "$u" -f 'usr.services.exe hold' | while read -r p; do grep -q '^State:.*Z' "/proc/$p/status" 2>/dev/null || echo "$p"; done)" ] ||
            fail "a system group process with a user image kept running"
        wait "$STOPPED" 2>/dev/null || true
        ok c3_system_group_process_with_unlisted_image_is_stopped
        [ -n "$(server_pid "$u")" ] || fail "the server died when it stopped the process"
        expect_rpass c3_server_survives_the_stopped_process "$u" 64 token
    fi

    if [ "$u" = "$ADM" ]; then
        # -- an administrator token (with SeDebugPrivilege enabled) can open the system processes, as on Windows
        standin_on
        for img in services.exe winedevice.exe wexsvc.exe; do
            expect_spass "c3_administrator_can_open_${img%.exe}" "$u" 64 admin-open "$img"
        done
    fi

    # -- the limited and the standard user
    for arch in ${WEX_C3_ARCHS:-64 32}; do
        for img in services.exe winedevice.exe wexsvc.exe; do
            expect_rpass "c3_${u}_cannot_open_${img%.exe}_$arch" "$u" "$arch" proc-denied "$img"
        done
        expect_rpass "c3_${u}_scm_create_and_reconfigure_denied_$arch" "$u" "$arch" scm-denied WexSvcSys
        expect_rpass "c3_${u}_scm_create_and_reconfigure_denied_ns_$arch" "$u" "$arch" scm-denied WexSvcNs   # WexSvcLs got a descriptor above
        expect_rpass "c3_${u}_cannot_create_tokens_$arch" "$u" "$arch" token-denied
        expect_rpass "c3_${u}_scm_connect_and_enumerate_work_$arch" "$u" "$arch" scm-allowed
        expect_rpass "c3_${u}_boot_state_grants_are_gone_$arch" "$u" "$arch" grants denied
    done
    standin_off
    if [ "$u" = "$STD" ]; then
        # the descriptor was saved with the machine hive and is read again by the next services.exe
        prefix_server_env "$u" -k || true; prefix_server_env "$u" -w; wait "$HOLD" 2>/dev/null || true
        [ "$(grep -c '^"Security"=hex' "$M/system.reg")" = 1 ] || fail "the service descriptor was not saved in the machine hive"
        ok c3_service_descriptor_saved_in_the_machine_hive
        rm -f "$SVCDIR"/*.txt
        probe "$u" 64 hold >/dev/null 2>&1 & HOLD=$!
        sleep 4
        wait_files "$SVCDIR/ls.txt"
        expect_rpass c3_service_descriptor_persists_over_a_restart "$u" 64 svc-start WexSvcLs allowed
        expect_rpass c3_other_services_still_have_the_default_after_a_restart "$u" 64 svc-start WexSvcNs denied
    fi
    prefix_server_env "$u" -k || true; prefix_server_env "$u" -w; wait "$HOLD" 2>/dev/null || true
done

if [ "${WEX_C3_NO_OWN:-0}" != 1 ]; then
echo "--- no launcher: a prefix of the user's own"
UP="/home/$STD/.wine-own"
rm -rf "$UP"
rm -rf "$SVCDIR"; install -d -m 1777 "$SVCDIR"
own_wine() { run_as "$STD" WINEPREFIX="$UP" "$BUILD/wine" "$@" 2>&1; }
own_server() { run_as "$STD" WINEPREFIX="$UP" "$BUILD/server/wineserver" "$@"; }
own_wine wineboot.exe -i >"$ROOT/own-boot.log" 2>&1 || true
own_server -w
[ -f "$UP/system.reg" ] && [ ! -L "$UP/system.reg" ] || fail "the own prefix has no system.reg of its own"
{
    printf '\n[Software\\\\Wine\\\\Elevation] 1700000000\n#time=1d9a0000000000\n"Helper"="C:\\\\windows\\\\system32\\\\consent.exe"\n'
    for spec in 'WexSvcSys:LocalSystem:sys' 'WexSvcLs:NT AUTHORITY\\LocalService:ls' 'WexSvcNs:NT AUTHORITY\\NetworkService:ns'; do
        IFS=: read -r nm acct rep <<<"$spec"
        printf '\n[System\\\\ControlSet001\\\\Services\\\\%s] 1700000000\n#time=1d9a0000000000\n"ErrorControl"=dword:00000001\n' "$nm"
        printf '"ImagePath"="%s svc %s Z:\\\\tmp\\\\wexsvc\\\\%s.txt Z:\\\\var\\\\lib\\\\wexsys-c2\\\\machine"\n' "$SVCEXE" "$nm" "$rep"
        printf '"ObjectName"="%s"\n"Start"=dword:00000002\n"Type"=dword:00000010\n' "$acct"
    done
} | run_as "$STD" tee -a "$UP/system.reg" >/dev/null
probe_own() { local arch="$1" mode="$2"; shift 2; own_wine "$ROOT/probes/rolesprobe$arch.exe" "$mode" "$@" | tr -d '\r'; }
own_wine "$ROOT/probes/rolesprobe64.exe" hold >/dev/null 2>&1 & HOLD=$!
sleep 4
wait_files "$SVCDIR/sys.txt" "$SVCDIR/ls.txt" "$SVCDIR/ns.txt"
for r in sys ls ns; do
    [ "$(fact "$SVCDIR/$r.txt" user)" != S-1-5-18 ] && [ "$(fact "$SVCDIR/$r.txt" user)" != S-1-5-19 ] &&
        [ "$(fact "$SVCDIR/$r.txt" user)" != S-1-5-20 ] || fail "a service in a user-writable prefix has a system token: $r"
    [ "$(fact "$SVCDIR/$r.txt" admins)" != enabled ] || fail "a service in a user-writable prefix has Administrators enabled: $r"
done
ok c3_own_prefix_services_run_with_the_users_token
for arch in 64 32; do
    OUT="$(probe_own "$arch" grants allowed)" || { echo "$OUT" >&2; fail "grants allowed $arch"; }
    grep -q '^grants=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "grants allowed $arch"; }
    ok "c3_own_prefix_boot_state_grants_present_$arch"
done
own_server -k || true; own_server -w; wait "$HOLD" 2>/dev/null || true
fi
standin_off

echo "=== sysrun server: $PASS checks passed ==="
