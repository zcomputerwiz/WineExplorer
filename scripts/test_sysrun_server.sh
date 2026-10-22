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

for p in "$ROOT" /etc/wineexplorer "$LIBEXEC"; do
    [ ! -e "$p" ] || { echo "$p exists; remove it first" >&2; exit 2; }
done
getent group "$GRP" >/dev/null && { echo "group $GRP exists" >&2; exit 2; }
for u in "$ADM" "$STD"; do id "$u" >/dev/null 2>&1 && { echo "user $u exists" >&2; exit 2; }; done

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
groupadd --system "$GRP"
coproc XV { Xvfb -displayfd 1 -screen 0 1024x768x24 -nolisten tcp 2>/dev/null; }
read -r -u "${XV[0]}" DNUM
[ -n "$DNUM" ] || fail "Xvfb did not start"

x86_64-w64-mingw32-gcc -O2 -Wall -o "$ROOT/probes/probe64.exe" "$SRC/scripts/sysrun_server_probe.c" -ladvapi32
i686-w64-mingw32-gcc -O2 -Wall -o "$ROOT/probes/probe32.exe" "$SRC/scripts/sysrun_server_probe.c" -ladvapi32
gcc -O2 -Wall -o "$ROOT/bin/asgroup" "$SRC/scripts/sysrun_asgroup.c"
chmod 0755 "$ROOT/probes"/*.exe

echo "--- machine root and launcher"
env WEX_MACHINE_GROUP="$GRP" WEX_MACHINE_ROOT="$M" WEX_SKEL_PFX="$ROOT/skel/.wine" WEX_BACKUP_DIR="$ROOT/backup" \
    WEX_WINE_BIN="$BUILD/wine" WEX_WINESERVER_BIN="$BUILD/server/wineserver" WEX_NTDLL="$BUILD/dlls/ntdll/ntdll.so" \
    WEX_SYSRUN_ENABLE=1 DISPLAY=":$DNUM" WINEDEBUG=-all \
    bash "$SRC/scripts/setup-machine-root.sh" > "$ROOT/setup.log" 2>&1 || { tail -20 "$ROOT/setup.log" >&2; fail "setup failed"; }
[ "$(stat -c '%a %U:%G' "$SYSRUN")" = "2755 root:$GRP" ] || fail "launcher not installed setgid"
install -m 2755 -o root -g "$GRP" "$ROOT/bin/asgroup" "$ROOT/bin/asgroup.sg"
GRP_GID="$(getent group "$GRP" | cut -d: -f3)"

for u in "$ADM" "$STD"; do
    useradd -m -k "$ROOT/skel" -s /bin/bash "$u"
done
usermod -a -G sudo "$ADM"
ADM_GID="$(id -g "$ADM")"; STD_GID="$(id -g "$STD")"; STD_UID="$(id -u "$STD")"; ADM_UID="$(id -u "$ADM")"

# Values for the probes, written by root into the machine hives before any server runs.
reg_append()   # file section line...
{
    local file="$1" section="$2"; shift 2
    { printf '\n[%s] 1700000000\n#time=1d9a0000000000\n' "$section"; printf '%s\n' "$@"; } >> "$file"
}
printf 'MARKER' > "$M/drive_c/windows/wex_marker.txt"
printf 'MARKER2' > "$M/drive_c/windows/wex_marker2.txt"
chown "root:$GRP" "$M/drive_c/windows/wex_marker.txt" "$M/drive_c/windows/wex_marker2.txt"
chmod 664 "$M/drive_c/windows/wex_marker.txt" "$M/drive_c/windows/wex_marker2.txt"
reg_append "$M/system.reg" 'Software\\WexProbe' '"FromMachine"="machine"'

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

# ---- 7. (iv) HKU well-known profiles, with elevation enforced -------------------------------------------------
echo "--- registry (iv), elevation enforced"
reg_append "$M/system.reg" 'Software\\Wine\\Elevation' '"Helper"="C:\\windows\\system32\\consent.exe"'
for u in "$STD" "$ADM"; do
    for arch in 64 32; do
        expect_pass "c2_enforced_hku_service_profiles_not_writable_${u}_$arch" "$u" "$arch" registry
        expect_pass "c2_enforced_limited_files_denied_${u}_$arch" "$u" "$arch" files-denied
    done
    prefix_server_env "$u" -k || true; prefix_server_env "$u" -w
done

echo "=== sysrun server: $PASS checks passed ==="
