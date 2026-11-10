#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Server-side probes of the system role (docs/ELEVATION_SYSTEM_ROLE_WORKORDER.md, phase C2, constraints a to d, f).
# Run as root on a Wine build (WINE_BUILD_DIR, default /home/user/build/dev). It creates a throw-away system
# group, two users (wexadm in sudo, wexstd not), a machine root with the setup script, installs the launcher
# (WEX_SYSRUN_ENABLE=1; the paths are compiled into ntdll, so it uses /etc/wineexplorer and
# /usr/local/libexec/wineexplorer and refuses to run if they exist) and removes everything at the end.
# Needs Xvfb, gcc and the mingw compilers, runuser, setfacl. WEX_C2_KEEP=1 keeps the fixture for inspection.
# ROOT IN CLEAN wexbuild OR A THROWAWAY VM ONLY; NEVER shared lab guest VM B.
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
# shellcheck source=scripts/lib/sysrun-fixture.sh
source "$SRC/scripts/lib/sysrun-fixture.sh"
if [ "${WEX_E1S6_LAB:-0}" = 1 ]; then
    source "$SRC/scripts/lib/e1s6-fixture.sh"
fi
BUILD="${WINE_BUILD_DIR:-/home/user/build/dev}"
ROOT=/var/lib/wexsys-c2
M="$ROOT/machine"
GRP=wexsysc2
ADM=wexadm
STD=wexstd
CAR=wexcar   # a second administrator, made by the callers section (step 7)
SYS=wexgreeter   # an unmapped system account, made by the symlinked-hives section (3b)
SYS_HOME=/var/lib/$SYS
SYS_CREATED=0
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
    [ ! -e "$p" ] || { echo "$p exists; use clean wexbuild or a throwaway VM, never remove VM B's role" >&2; exit 2; }
done
getent group "$GRP" >/dev/null && { echo "group $GRP exists" >&2; exit 2; }
for u in "$ADM" "$STD"; do id "$u" >/dev/null 2>&1 && { echo "user $u exists" >&2; exit 2; }; done
fi

fail()
{
    echo "FAIL: $*" >&2
    if [ "${WEX_E1S6_LAB:-0}" = 1 ]; then
        local log
        for log in "$ROOT/e1s6-wine.log" "$ROOT/e1s6-service.log"; do
            [ ! -f "$log" ] || { echo "--- $log" >&2; tail -120 "$log" >&2; }
        done
    fi
    exit 1
}
ok() { echo "$1=PASS"; PASS=$((PASS + 1)); }

kill_servers()
{
    local u
    for u in "$ADM" "$STD" "$CAR" "$SYS"; do
        id "$u" >/dev/null 2>&1 || continue
        pkill -u "$u" -x wineserver 2>/dev/null || true
    done
    sleep 1
}

cleanup()
{
    local rc=$?
    set +e
    ! type e1s6_stop >/dev/null 2>&1 || e1s6_stop
    ! type standin_off >/dev/null 2>&1 || standin_off
    [ -z "$DUMPABLE_SAVED" ] || echo "$DUMPABLE_SAVED" > /proc/sys/fs/suid_dumpable
    [ -z "${LEASES_SAVED:-}" ] || echo "$LEASES_SAVED" > /proc/sys/fs/leases-enable
    kill_servers
    pkill -u "$ADM" 2>/dev/null; pkill -u "$STD" 2>/dev/null; pkill -u "$CAR" 2>/dev/null; pkill -u "$SYS" 2>/dev/null   # includes winedevice.exe, which blocks userdel
    sleep 1; pkill -9 -u "$ADM" 2>/dev/null; pkill -9 -u "$STD" 2>/dev/null; pkill -9 -u "$CAR" 2>/dev/null; pkill -9 -u "$SYS" 2>/dev/null
    [ -z "${XV_PID:-}" ] || kill "$XV_PID" 2>/dev/null
    if [ "${WEX_C2_KEEP:-0}" = 1 ]; then
        echo "fixture kept: $ROOT, users $ADM $STD $CAR $SYS, group $GRP, $CONF, $LIBEXEC" >&2
        exit "$rc"
    fi
    for u in "$ADM" "$STD" "$CAR"; do
        uid="$(id -u "$u" 2>/dev/null)"
        userdel -r "$u" 2>/dev/null
        [ -z "$uid" ] || rm -rf "/tmp/.wine-$uid"
    done
    if [ "$SYS_CREATED" = 1 ]; then
        userdel -r "$SYS" 2>/dev/null
    fi
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
DISPLAY=":$DNUM" wex_fixture_machine || { tail -20 "$ROOT/setup.log" >&2; fail "setup failed"; }
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
# Without an elevation helper nobody may persist machine hive changes: the server saves them only with the real
# user's own rights (stock rule), and the machine hives belong to root and the system group.
! grep -q '"Written"="yes"' "$M/system.reg" || fail "the machine hive was saved without enforced elevation"
ok c2_machine_hive_not_saved_without_enforcement
[ "$(stat -c '%U:%G' "$M/system.reg" "$M/userdef.reg" | sort -u)" = "root:$GRP" ] ||
    fail "a saved machine hive is not root:$GRP any more (its owner could change it): $(stat -c '%U:%G %n' "$M/system.reg" "$M/userdef.reg")"
ok c2_machine_hives_stay_root_owned_after_save
for hive in system.reg userdef.reg; do
    [ "$(readlink "/home/$STD/.wine/$hive")" = "$M/drive_c/windows/wex_marker2.txt" ] || fail "the prefix link $hive was replaced"
    run_as "$STD" ln -sfn "$M/$hive" "/home/$STD/.wine/$hive"
done
ok c2_prefix_links_left_alone
for u in "$STD" "$ADM"; do
    wine_as "$u" reg add 'HKLM\Software\WexUnenforced' /v "$u" /d 1 /f >/dev/null 2>&1 || fail "reg add HKLM as $u (the write must work in memory)"
    prefix_server_env "$u" -k || true; prefix_server_env "$u" -w
    ! grep -q 'WexUnenforced' "$M/system.reg" || fail "an HKLM write of $u reached the machine system.reg without enforced elevation"
    ok "c2_unenforced_hklm_write_not_persisted_$u"
done

# ---- 3b. (iii) a system.reg symlink into the machine root, for an unmapped system account (Safe Mode C7) ----
# The greeter runs as a system account that is not in the accounts map. Give it the same skeleton prefix the
# harness gives desktop users and run the 3(iii) checks as it: the machine file behind the prefix symlink is
# never overwritten, HKLM still comes from the machine root, and the server it gets is the launcher-started one.
echo "--- symlinked hives (iii), unmapped system account"
sysrun_as() { local u="$1"; shift; (cd /tmp && runuser -u "$u" -- env -i HOME="$SYS_HOME" USER="$u" LOGNAME="$u" PATH=/usr/bin:/bin LANG=C \
    DISPLAY=":$DNUM" WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' "$@"); }
sysprefix_env() { sysrun_as "$1" env WINEPREFIX="$SYS_HOME/.wine" "$BUILD/server/wineserver" "${@:2}"; }
sysprobe() { local u="$1" arch="$2" mode="$3"; shift 3; sysrun_as "$u" "$BUILD/wine" "$ROOT/probes/probe$arch.exe" "$mode" "$WINM" "$@" 2>&1; }
if ! id "$SYS" >/dev/null 2>&1; then
    useradd --system -d "$SYS_HOME" -s /usr/sbin/nologin "$SYS"
    SYS_CREATED=1
fi
SYS_UID="$(id -u "$SYS")"; SYS_GID="$(id -g "$SYS")"
[ "$SYS_UID" -lt 1000 ] || fail "the system account uid is not below 1000"
[ "$(getent passwd "$SYS" | cut -d: -f7)" = /usr/sbin/nologin ] || fail "the system account shell is not nologin"
[ "$(getent passwd "$SYS" | cut -d: -f6)" = "$SYS_HOME" ] || fail "the system account home is not $SYS_HOME"
for g in sudo "$GRP"; do
    id -nG "$SYS" | grep -qw "$g" && fail "the system account is in $g"
done
[ ! -f /etc/wineexplorer/accounts.map ] || ! grep -q "^$SYS " /etc/wineexplorer/accounts.map || fail "the system account is in the accounts map"
# the same skeleton prefix the harness gives desktop users (the system role is only available for <home>/.wine)
install -d -o "$SYS" -g "$SYS" -m 0755 "$SYS_HOME"
cp -a "$ROOT/skel/.wine" "$SYS_HOME/.wine"
chown -R "$SYS:$SYS" "$SYS_HOME/.wine"
sysprefix_env "$SYS" -k || true; sysprefix_env "$SYS" -w
for hive in system.reg userdef.reg; do
    ls -l "$SYS_HOME/.wine/$hive" | grep -q -- '->' || fail "skeleton prefix has no $hive link"
done
for hive in system.reg userdef.reg; do
    sysrun_as "$SYS" ln -sfn "$M/drive_c/windows/wex_marker2.txt" "$SYS_HOME/.wine/$hive"
done
sysprobe "$SYS" 64 hklm-write >/dev/null || fail "hklm-write"
OUT="$(sysprobe "$SYS" 64 hklm-read)"; grep -q '^hklm-read=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "HKLM not from the machine root with a diverted prefix link"; }
ok c2_symlinked_hives_system_account_hklm_from_machine_root
# the server it gets is the launcher-started one: its saved group is the system group (section 1's recognition)
SPID="$(server_pid "$SYS")"; [ -n "$SPID" ] || fail "no server of $SYS"
IDS="$(awk '/^Uid:/ { u = $2 "/" $3 "/" $4 } /^Gid:/ { print u, $2, $3, $4 }' "/proc/$SPID/status")"
[ "$IDS" = "$SYS_UID/$SYS_UID/$SYS_UID $SYS_GID $SYS_GID $GRP_GID" ] || fail "server ids are not (user, user, saved $GRP): $IDS"
ok c2_symlinked_hives_system_account_server_started_by_launcher
sysprefix_env "$SYS" -k || true; sysprefix_env "$SYS" -w
[ "$(cat "$M/drive_c/windows/wex_marker2.txt")" = MARKER2 ] || fail "the machine-root file behind the prefix symlink was overwritten"
ok c2_symlinked_hives_system_account_target_not_overwritten
! grep -q '"Written"="yes"' "$M/system.reg" || fail "the machine hive was saved without enforced elevation"
ok c2_symlinked_hives_system_account_hive_not_saved_without_enforcement
[ "$(stat -c '%U:%G' "$M/system.reg" "$M/userdef.reg" | sort -u)" = "root:$GRP" ] ||
    fail "a saved machine hive is not root:$GRP any more: $(stat -c '%U:%G %n' "$M/system.reg" "$M/userdef.reg")"
ok c2_symlinked_hives_system_account_hives_stay_root_owned
for hive in system.reg userdef.reg; do
    [ "$(readlink "$SYS_HOME/.wine/$hive")" = "$M/drive_c/windows/wex_marker2.txt" ] || fail "the prefix link $hive was replaced"
    sysrun_as "$SYS" ln -sfn "$M/$hive" "$SYS_HOME/.wine/$hive"
done
ok c2_symlinked_hives_system_account_prefix_links_left_alone

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

echo "--- machine hive writes, elevation enforced"
wine_as "$STD" reg add 'HKLM\Software\WexEnforced' /v std /d 1 /f >/dev/null 2>&1 && fail "a standard user could write HKLM with enforced elevation"
prefix_server_env "$STD" -k || true; prefix_server_env "$STD" -w
! grep -q 'WexEnforced' "$M/system.reg" || fail "a denied HKLM write of $STD reached the machine system.reg"
ok c2_enforced_standard_user_hklm_write_denied_and_not_persisted

# -- the admin_group= override: a repeated line makes the configuration invalid, so even a member of the named
# group gets the standard (non-administrator) token shape (same rule as read_policy in accounts.c)
echo "--- admin_group= override: a repeated line is an invalid configuration"
cp "$CONF" "$ROOT/sysrun.conf.orig"
prefix_server_env "$ADM" -k || true; prefix_server_env "$ADM" -w
grep -v '^admin_group=' "$ROOT/sysrun.conf.orig" > "$CONF"
printf 'admin_group=sudo\nadmin_group=sudo\n' >> "$CONF"
OUT="$(wine_as "$ADM" "$ROOT/probes/rolesprobe64.exe" token 2>&1 | tr -d '\r')" || { echo "$OUT" >&2; fail "admin_group duplicate: token probe"; }
grep -q '^admins=absent$' <<<"$OUT" && grep -q '^elevation=1$' <<<"$OUT" && grep -q '^linked=none$' <<<"$OUT" ||
    { echo "$OUT" >&2; fail "a repeated admin_group= line still gave the administrator token shape"; }
ok c2_admin_group_duplicate_line_gives_standard_token_shape
cat "$ROOT/sysrun.conf.orig" > "$CONF"
prefix_server_env "$ADM" -k || true; prefix_server_env "$ADM" -w

# ---- 8. the system role (C3), elevation enforced --------------------------------------------------------------
# services.exe, winedevice.exe and the LocalSystem service report SYSTEM; the LocalService and NetworkService
# services report their accounts and have no system group; a limited and a standard user cannot get at the system
# processes, cannot create or reconfigure a service, cannot make tokens; an administrator token can open them;
# a copy of a system program in a user directory gets no role; in a prefix without the launcher nothing changes.
echo "--- system role (C3)"
SVCDIR=/tmp/wexsvc
SVCEXE='Z:\\var\\lib\\wexsys-c2\\probes\\wexsvc.exe'
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
            expect_in "c3_${nm}_service_in_users" "$f" group=S-1-5-32-545:enabled
            expect_in "c3_${nm}_service_in_console_logon" "$f" group=S-1-2-1:enabled
            expect_in "c3_${nm}_service_in_this_organization" "$f" group=S-1-5-15:enabled
            expect_fact "c3_${nm}_service_cannot_write_machine_root" "$f" machine_root_write denied
        done
        # native table (docs/ELEVATION_LOCAL_VALIDATION_WORKORDER.md); SeImpersonate stays enabled (documented default)
        expect_privs c3_localservice_service_privileges "$SVCDIR/ls.txt" SeAssignPrimaryTokenPrivilege:D SeAuditPrivilege:D \
            SeChangeNotifyPrivilege:E SeCreateGlobalPrivilege:E SeImpersonatePrivilege:E SeShutdownPrivilege:D SeUndockPrivilege:D \
            SeSystemtimePrivilege:D SeIncreaseWorkingSetPrivilege:D SeTimeZonePrivilege:D
        expect_privs c3_networkservice_service_privileges "$SVCDIR/ns.txt" SeAssignPrimaryTokenPrivilege:D SeAuditPrivilege:D \
            SeChangeNotifyPrivilege:E SeCreateGlobalPrivilege:E SeImpersonatePrivilege:E SeIncreaseQuotaPrivilege:D \
            SeShutdownPrivilege:D SeUndockPrivilege:D SeIncreaseWorkingSetPrivilege:D SeTimeZonePrivilege:D
        expect_privs c3_system_service_privileges "$SVCDIR/sys.txt" \
            SeLockMemoryPrivilege:E SeTcbPrivilege:E SeSystemProfilePrivilege:E SeProfileSingleProcessPrivilege:E \
            SeIncreaseBasePriorityPrivilege:E SeCreatePagefilePrivilege:E SeCreatePermanentPrivilege:E SeDebugPrivilege:E \
            SeAuditPrivilege:E SeChangeNotifyPrivilege:E SeImpersonatePrivilege:E SeCreateGlobalPrivilege:E \
            SeIncreaseWorkingSetPrivilege:E SeTimeZonePrivilege:E SeCreateSymbolicLinkPrivilege:E \
            SeDelegateSessionUserImpersonatePrivilege:E SeAssignPrimaryTokenPrivilege:D SeIncreaseQuotaPrivilege:D \
            SeSecurityPrivilege:D SeTakeOwnershipPrivilege:D SeLoadDriverPrivilege:D SeSystemtimePrivilege:D \
            SeBackupPrivilege:D SeRestorePrivilege:D SeShutdownPrivilege:D SeSystemEnvironmentPrivilege:D \
            SeUndockPrivilege:D SeManageVolumePrivilege:D
        for g in S-1-1-0 S-1-5-32-545 S-1-5-6 S-1-2-1 S-1-5-11 S-1-5-15 S-1-2-0 S-1-5-32-544; do
            expect_in "c3_sys_service_in_group_$g" "$SVCDIR/sys.txt" group=$g:enabled
        done

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
        # wine.inf gives the broker service a descriptor of its own (WexElevate), so the count is not 1 any more
        [ "$(awk '/^\[/ { in_ls = ($0 ~ /Services.*WexSvcLs\]/) } in_ls && /^"Security"=hex/ { n++ } END { print n + 0 }' "$M/system.reg")" = 1 ] &&
            [ "$(grep -c '^"Security"=hex' "$M/system.reg")" = 2 ] || fail "the service descriptor was not saved in the machine hive"
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

# ---- 9. the elevation broker (plan step 4), elevation enforced -------------------------------------------------
# wexelevate.exe is a LocalSystem service. The shipping broker has no way to ask for consent and fails closed
# (ERROR_NOT_READY); a broker built from the same source with -DWEXELEVATE_TEST_APPROVAL calls an approval stub that
# exists only here. Checks: the elevated process has the full token, High integrity, Administrators enabled and
# the system group, its parent is the caller and the caller's handle waits; frozen requests, A/B, arguments, forged
# ids; a standard user and a prefix without the launcher get ERROR_NOT_SUPPORTED; a limited process of the same user
# cannot open the elevated process or its threads; the stub is not in the shipping build.
if [ "${WEX_C4_SKIP:-0}" != 1 ]; then
echo "--- elevation broker (step 4)"
EL=/tmp/wexelev
BROKER_ORIG=""
el_clean() { rm -rf "$EL"; install -d -m 1777 "$EL"; }
winpath() { echo "Z:${1//\//\\}"; }
bprobe() { local u="$1" arch="$2"; shift 2; wine_as "$u" "$ROOT/probes/wexelevate_probe$arch.exe" "$@" 2>&1 | tr -d '\r'; }
bp()   # name user arch mode args...: the probe must print "<mode>=PASS"
{
    local name="$1" out; out="$(bprobe "${@:2}")" || { echo "$out" >&2; fail "$name"; }
    grep -q "^$4=PASS" <<<"$out" || { echo "$out" >&2; fail "$name: no PASS line"; }
    ok "$name"
}
# a user32 program keeps WinSta0 in existence, as the shell does in a session; the broker makes the secure desktop in it
el_up()
{
    # The server and service children inherit bootstrap stderr. Preserve it in
    # S6 runs; otherwise their gated denial-site diagnostics disappear.
    local log=/dev/null
    [ "${WEX_E1S6_LAB:-0}" != 1 ] || log="$ROOT/e1s6-wine.log"
    [ -z "${WEX_EL_LOG:-}" ] || log="$WEX_EL_LOG"   # diagnostics: keep server/service stderr of a normal run
    probe "$1" 64 hold >>"$log" 2>&1 & EL_HOLD=$!
    wine_as "$1" "$ROOT/probes/sdprobe64.exe" hold >>"$log" 2>&1 &
    sleep 4
}
el_down() { prefix_server_env "$1" -k || true; prefix_server_env "$1" -w; wait "$EL_HOLD" 2>/dev/null || true; }
el_wait_file() { local i; for i in $(seq 1 90); do [ -e "$1" ] && return 0; sleep 1; done; fail "no $1"; }
el_proc() { local p; for p in $(pgrep -u "$1" -f "$2"); do grep -q '^State:.*Z' "/proc/$p/status" 2>/dev/null || { echo "$p"; return; }; done; }
el_fact() { grep -m1 "^$2=" "$1" | cut -d= -f2- | tr -d '\r'; }
SYS32="$M/drive_c/windows/system32"
cat > "$ROOT/setval.py" <<'PYEOF'
import sys
path, section, name, value = sys.argv[1:5]   # value "none" removes the line
lines = open(path, encoding='latin-1').read().split('\n')
out, inside, done = [], False, False
head = '[' + section + ']'
for line in lines:
    if line.startswith('['):
        if inside and not done and value != 'none':
            out.append('"%s"=dword:%08x' % (name, int(value))); done = True
        inside = line.startswith(head + ' ')
    if inside and line.startswith('"%s"=' % name):
        continue
    out.append(line)
if inside and not done and value != 'none':
    out.append('"%s"=dword:%08x' % (name, int(value))); done = True
if not done and value != 'none':
    out.append('\n%s 1700000000\n#time=1d9a0000000000\n"%s"=dword:%08x' % (head, name, int(value)))
open(path, 'w', encoding='latin-1').write('\n'.join(out))
PYEOF
set_val() { python3 -I "$ROOT/setval.py" "$M/system.reg" "$1" "$2" "$3"; }   # section name dword|none
POLSEC='Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System'

# the stub is not in a shipping build: the macro is not defined by any Makefile, and the installed program and the
# machine root do not contain the stub's name or the macro
cat > "$ROOT/has_str.py" <<'EOF'
import sys
data = open(sys.argv[1], 'rb').read()
sys.exit(0 if sys.argv[2].encode('utf-16-le') in data or sys.argv[2].encode() in data else 1)
EOF
has_str() { python3 -I "$ROOT/has_str.py" "$1" "$2"; }   # the string as ASCII or UTF-16
x86_64-w64-mingw32-gcc -O2 -Wall -municode -I"$SRC/src/include" -o "$ROOT/probes/wexelevate_ship.exe" \
    "$SRC/src/programs/wexelevate/main.c" -ladvapi32 -luserenv -lbcrypt
x86_64-w64-mingw32-gcc -O2 -Wall -municode -DWEXELEVATE_TEST_APPROVAL -I"$SRC/src/include" -o "$ROOT/probes/wexelevate_test.exe" \
    "$SRC/src/programs/wexelevate/main.c" -ladvapi32 -luserenv -lbcrypt
x86_64-w64-mingw32-gcc -O2 -Wall -o "$ROOT/probes/wexelevate_stub.exe" "$SRC/scripts/wexelevate_stub.c"
for arch in 64 32; do
    cc=x86_64-w64-mingw32-gcc; [ $arch = 32 ] && cc=i686-w64-mingw32-gcc
    $cc -O2 -Wall -DWEXELEVATE_TEST_APPROVAL -I"$SRC/src/include" -o "$ROOT/probes/wexelevate_probe$arch.exe" "$SRC/scripts/wexelevate_probe.c" -ladvapi32 -lshell32 -lmsi
    $cc -O2 -Wall -o "$ROOT/probes/sdprobe$arch.exe" "$SRC/scripts/secure_desktop_probe.c" -ladvapi32 2>/dev/null
done
gcc -O2 -Wall -o "$ROOT/bin/x11probe" "$SRC/scripts/x11_grab_probe.c" -lX11
chmod 0755 "$ROOT/probes"/*.exe
has_str "$ROOT/probes/wexelevate_test.exe" wexelevate_stub || fail "the test broker does not call the stub (control)"
! has_str "$ROOT/probes/wexelevate_ship.exe" wexelevate_stub || fail "a broker built without the macro contains the stub name"
has_str "$ROOT/probes/wexelevate_test.exe" E1S1-REQ || fail "the test broker has no E1-S5 record code (control)"
! has_str "$ROOT/probes/wexelevate_ship.exe" E1S1-REQ || fail "a shipping broker contains disabled E1-S5 record code"
has_str "$ROOT/probes/wexelevate_test.exe" E1S6_DENY || fail 'test broker has no denial-site audit (control)'
! has_str "$ROOT/probes/wexelevate_ship.exe" E1S6_DENY || fail 'shipping broker contains lab denial-site audit'
! grep -rn 'WEXELEVATE_TEST_APPROVAL' "$SRC/src" "$SRC/installer" "$SRC/tools" --include=Makefile.in --include='*.mk' --include='*.inf' \
    --include='*.sh' --include='*.py' >/dev/null 2>&1 || fail "a build file defines WEXELEVATE_TEST_APPROVAL"
! grep -rEn 'WEXELEVATE_TEST_APPROVAL|WEXELEVATE_E1S18|WEXCONSENT_TEST_HOOK|WEX_E1S6_TRANSPORT_TEST|WEX_E1S10_TOKEN_TEST|WEX_E1S7_LAB|WEX_E1S7_TEST|WEX_ACCOUNTS_TEST' \
    "$SRC/src" "$SRC/installer" "$SRC/tools" "$SRC/wine" \
    --include=Makefile.in --include=Makefile --include=configure.ac --include='*.mk' --include='*.inf' \
    --include='*.sh' --include='*.py' >/dev/null 2>&1 || fail "a production build file names an E1 test gate"
# Positive lab controls and separate normal-runtime exclusions. Never label a
# lab runtime as shipping merely because its PE broker was compiled normally.
[ -f "$BUILD/server/wineserver" ] && [ -f "$BUILD/dlls/wexhost/wexhost.so" ] || fail 'missing runtime artifacts for exclusion checks'
if [ "${WEX_E1S6_LAB:-0}" = 1 ]; then
    has_str "$BUILD/server/wineserver" wexelevate_test.exe || fail 'lab server has no S6 image hook'
    has_str "$BUILD/dlls/wexhost/wexhost.so" org.wineexplorer.E1S6Lab || fail 'lab wexhost has no S6 bus hook'
    has_str "$BUILD/server/wineserver" E1S6_DENY || fail 'lab server has no S6 denial-site audit'
    has_str "$BUILD/dlls/wexhost/wexhost.so" E1S6_DENY || fail 'lab wexhost has no S6 denial-site audit'
else
    for marker in wexelevate_test.exe 'E1S token context (private)' E1S6_DENY; do
        ! has_str "$BUILD/server/wineserver" "$marker" || fail "normal server contains $marker"
    done
    ! has_str "$BUILD/dlls/wexhost/wexhost.so" org.wineexplorer.E1S6Lab || fail 'normal wexhost contains S6 bus hook'
    ! has_str "$BUILD/dlls/wexhost/wexhost.so" E1S6_DENY || fail 'normal wexhost contains S6 denial-site audit'
fi
for f in "$BUILD"/programs/wexelevate/*-windows/wexelevate.exe; do
    ! has_str "$f" wexelevate_stub || fail "the installed program $f contains the stub name"
    ! has_str "$f" E1S1-REQ || fail "the installed program $f contains E1-S5 test code"
done
[ -z "$(find "$M" "$BUILD/programs" "$BUILD/dlls" \( -name 'wexelevate_stub*' -o -name 'wexelevate_test*' \) -print -quit)" ] ||
    fail "the stub or the test broker is in the installed tree"
[ -z "$(grep -rlaF wexelevate_stub "$M/drive_c/windows" 2>/dev/null | head -1)" ] || fail "the machine root names the stub"
ok c4_test_stub_absent_from_shipping_build

for u in "$ADM" "$STD"; do
    run_as "$u" install -d "/home/$u/wexel"
    for img in p64 p32 a b victim; do
        arch=64; [ $img = p32 ] && arch=32
        run_as "$u" cp "$ROOT/probes/wexelevate_probe$arch.exe" "/home/$u/wexel/$img.exe"
    done
done

# -- the shipping broker with a policy that selects no prompt (0: elevate without asking) fails closed; a standard user is not served
set_val "$POLSEC" ConsentPromptBehaviorAdmin 0
el_clean
el_up "$ADM"
for arch in 64 32; do
    bp "c4_shipping_broker_fails_closed_admin_$arch" "$ADM" "$arch" run "$(winpath /home/$ADM/wexel/p$arch.exe)" 21 "$(winpath $EL/out.ship)" -
done
el_down "$ADM"
el_up "$STD"
bp c4_standard_user_not_supported_shipping "$STD" 64 run "$(winpath /home/$STD/wexel/p64.exe)" 50 "$(winpath $EL/out.ship)" -
[ ! -e "$EL/out.ship" ] || fail "a refused request started a process"
el_down "$STD"
ok c4_nothing_started_without_consent

set_val "$POLSEC" ConsentPromptBehaviorAdmin none
# -- the test broker and its stub. Wine loads a program of system32 that has a builtin twin from the builtin
# directory whatever the file is, so the test broker has another name and the service points to it.
cat "$M/system.reg" > "$ROOT/system.reg.c4"
install -m 775 -o root -g "$GRP" "$ROOT/probes/wexelevate_test.exe" "$SYS32/wexelevate_test.exe"
install -m 775 -o root -g "$GRP" "$ROOT/probes/wexelevate_stub.exe" "$SYS32/wexelevate_stub.exe"
reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' '"ImagePath"="C:\\windows\\system32\\wexelevate_test.exe"'
BROKER_ORIG=1
el_restore_broker() { [ -z "$BROKER_ORIG" ] || { cat "$ROOT/system.reg.c4" > "$M/system.reg"; rm -f "$SYS32/wexelevate_test.exe" "$SYS32/wexelevate_stub.exe" "$SYS32/consent_test.exe"; BROKER_ORIG=""; }; }

el_up "$ADM"
IMG="$(winpath /home/$ADM/wexel/p64.exe)"

# the control: a limited process has neither the token nor the folder
el_clean
bp c4_control_limited_process_facts "$ADM" 64 facts "$(winpath $EL/out.limited)"
[ "$(el_fact "$EL/out.limited" elevation)" = 3 ] && [ "$(el_fact "$EL/out.limited" admins)" = denyonly ] &&
    [ "$(el_fact "$EL/out.limited" program_files)" != writable ] || { cat "$EL/out.limited" >&2; fail "the limited control is not limited"; }
ok c4_control_limited_process_is_limited

# -- 1. the elevated process
for arch in 64 32; do
    el_clean; touch "$EL/approve.all"
    bp "c4_admin_elevates_full_token_parent_is_caller_wait_and_exit_code_$arch" "$ADM" "$arch" run "$(winpath /home/$ADM/wexel/p$arch.exe)" 0 "$(winpath $EL/out.run)" -
done
el_clean; touch "$EL/approve.all"
bp c4_environment_block_of_the_request_arrives "$ADM" 64 run "$IMG" 0 "$(winpath $EL/out.env)" e
bp c4_default_environment_of_the_user_without_a_block "$ADM" 64 run "$IMG" 0 "$(winpath $EL/out.env2)" -
bp c4_elevated_caller_is_not_served "$ADM" 64 run "$IMG" 0 "$(winpath $EL/out.again)" - again
[ "$(el_fact "$EL/out.again" again)" = 50 ] || { cat "$EL/out.again" >&2; fail "an elevated caller was served (again=$(el_fact "$EL/out.again" again))"; }
ok c4_elevated_caller_gets_not_supported

bp c4_child_of_an_elevated_process_is_elevated_and_can_write_program_files "$ADM" 64 run "$IMG" 0 "$(winpath $EL/out.spawn)" - spawn
[ "$(el_fact "$EL/out.spawn.sub" elevation)" = 2 ] && [ "$(el_fact "$EL/out.spawn.sub" program_files)" = writable ] &&
    [ "$(el_fact "$EL/out.spawn.sub" integrity)" = 12288 ] || { cat "$EL/out.spawn.sub" >&2; fail "the child of an elevated process is not elevated"; }
ok c4_grandchild_is_elevated_and_keeps_the_system_group

# -- 2. the Unix side of the elevated process, and a limited process of the same user against it (B7, B8)
el_clean; touch "$EL/approve.all"
bprobe "$ADM" 64 attack "$IMG" "$(winpath $EL/out.attack)" t1 > "$ROOT/attack.log" 2>&1 & APID=$!
for _ in $(seq 1 60); do CP="$(el_proc "$ADM" 'child .*hold t1')"; [ -n "$CP" ] && break; sleep 1; done
[ -n "${CP:-}" ] || { cat "$ROOT/attack.log" >&2; fail "no elevated process"; }
for _ in $(seq 1 30); do grep -q 'ready=1' "$EL/out.attack" 2>/dev/null && break; sleep 1; done
[ "$(proc_gids "$CP")" = "$ADM_GID $GRP_GID $GRP_GID" ] || fail "the elevated process gids: $(proc_gids "$CP")"
ok c4_elevated_process_has_the_system_group
[ "$(stat -c %U "/proc/$CP/environ")" = root ] || fail "the elevated process is dumpable, the user could ptrace it"
ok c4_elevated_process_is_not_dumpable
touch "$EL/continue.t1"
wait "$APID" || { cat "$ROOT/attack.log" >&2; fail "the attack probe"; }
grep -q '^attack=PASS' "$ROOT/attack.log" || { cat "$ROOT/attack.log" >&2; fail "the attack probe: no PASS"; }
ok c4_limited_process_cannot_open_the_elevated_process_or_its_threads

# -- 3. frozen requests
# A writer from Unix (the broker holds a read lease on the frozen image): it blocks until the broker has reacted,
# which is a few milliseconds, then goes on and changes the file.
cat > "$ROOT/mmapw.py" <<'EOF'
import mmap, sys
with open(sys.argv[1], 'r+b') as f:
    m = mmap.mmap(f.fileno(), 0)
    m[0:1] = bytes([m[0] ^ 0xff])
    m.flush()
    m.close()
EOF
chmod 0644 "$ROOT/mmapw.py"
has_lease() { grep -qE " LEASE .*:$(stat -c %i "$1") " /proc/locks; }
wait_no_lease() { local i; for i in $(seq 1 40); do has_lease "$1" || return 0; sleep 0.25; done; return 1; }
lease_writer()   # variant file: run the writer; the file must have changed afterwards
{
    local before after
    before="$(md5sum < "$2")"
    case "$1" in
        append) run_as "$ADM" timeout 30 bash -c 'printf x >> "$1"' _ "$2" ;;
        truncate) run_as "$ADM" timeout 30 bash -c ': > "$1"' _ "$2" ;;
        mmap) run_as "$ADM" timeout 30 python3 "$ROOT/mmapw.py" "$2" ;;
    esac || return 1
    after="$(md5sum < "$2")"
    [ "$before" != "$after" ]
}
frozen()   # name variant expected-error
{
    el_clean; touch "$EL/wait-for-gate" "$EL/approve.victim.exe"
    run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/victim.exe"
    bprobe "$ADM" 64 run "$(winpath /home/$ADM/wexel/victim.exe)" "$3" "$(winpath $EL/out.frozen)" - > "$ROOT/frozen.log" 2>&1 & FP=$!
    el_wait_file "$EL/started.victim.exe"
    case "$2" in
        append|truncate|mmap) lease_writer "$2" "/home/$ADM/wexel/victim.exe" || fail "$1: the writer did not complete" ;;
        replace) run_as "$ADM" bash -c 'cp "$1" "$1.new" && mv "$1.new" "$1"' _ "/home/$ADM/wexel/victim.exe" ;;
        none) ;;
    esac
    touch "$EL/gate"
    wait "$FP" || { cat "$ROOT/frozen.log" >&2; fail "$1"; }
    grep -q '^run=PASS' "$ROOT/frozen.log" || { cat "$ROOT/frozen.log" >&2; fail "$1: no PASS"; }
    ok "$1"
}
frozen c4_unchanged_image_runs_after_a_pending_approval none 0
frozen c4_image_changed_in_place_after_freezing_is_rejected append 1392
[ ! -e "$EL/out.frozen" ] || fail "the changed image ran"
frozen c4_image_replaced_after_freezing_is_rejected replace 1392
[ ! -e "$EL/out.frozen" ] || fail "the replaced image ran"
frozen c4_writer_truncating_the_frozen_image_fails_the_request truncate 1392
[ ! -e "$EL/out.frozen" ] || fail "the truncated image ran"
frozen c4_writer_with_a_shared_writable_mapping_fails_the_request mmap 1392
[ ! -e "$EL/out.frozen" ] || fail "the modified image ran"
wait_no_lease "/home/$ADM/wexel/victim.exe" || fail "the lease outlived a failed request"
ok c4_lease_is_given_up_when_the_request_fails

# -- 3b. the read lease on the image while the elevated program runs (consent work order, Part A)
lease_running()   # name variant
{
    local img="/home/$ADM/wexel/lv.exe" i
    el_clean; touch "$EL/approve.all"
    run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "$img"
    bprobe "$ADM" 64 leasekill "$(winpath $img)" "$(winpath $EL/out.lk)" lk > "$ROOT/lk.log" 2>&1 & LP=$!
    for i in $(seq 1 60); do grep -q 'ready=1' "$EL/out.lk" 2>/dev/null && break; sleep 1; done
    grep -q 'ready=1' "$EL/out.lk" 2>/dev/null || { cat "$ROOT/lk.log" >&2; fail "$1: no elevated process"; }
    has_lease "$img" || fail "$1: no lease while the elevated process runs"
    lease_writer "$2" "$img" || fail "$1: the writer did not complete or changed nothing"
    wait "$LP" || { cat "$ROOT/lk.log" >&2; fail "$1"; }
    grep -q '^leasekill=PASS' "$ROOT/lk.log" || { cat "$ROOT/lk.log" >&2; fail "$1: no PASS"; }
    wait_no_lease "$img" || fail "$1: the lease outlived the process"
    ok "$1"
}
lease_running c4_writer_appending_ends_the_running_elevated_program append
lease_running c4_writer_truncating_ends_the_running_elevated_program truncate
lease_running c4_writer_mapping_the_image_writable_ends_the_running_elevated_program mmap

# a Wine program as the writer: the wineserver opens the file first (and blocks in open() until the broker lets go,
# which it does without asking the wineserver) and only then refuses by share mode (sharing violation). The open is
# enough to end the elevated program; nothing is written.
el_clean; touch "$EL/approve.all"
run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/lv.exe"
SUM="$(md5sum < /home/$ADM/wexel/lv.exe)"
bprobe "$ADM" 64 leasekill "$(winpath /home/$ADM/wexel/lv.exe)" "$(winpath $EL/out.wk)" wk > "$ROOT/wk.log" 2>&1 & LP=$!
for i in $(seq 1 60); do grep -q 'ready=1' "$EL/out.wk" 2>/dev/null && break; sleep 1; done
grep -q 'ready=1' "$EL/out.wk" 2>/dev/null || { cat "$ROOT/wk.log" >&2; fail "no elevated process"; }
WOUT="$(bprobe "$ADM" 64 winwrite "$(winpath /home/$ADM/wexel/lv.exe)" || true)"
grep -q 'error 32' <<<"$WOUT" || { echo "$WOUT" >&2; fail "a Wine program could open the running image for writing"; }
wait "$LP" || { cat "$ROOT/wk.log" >&2; fail "the elevated program was not ended by a Wine program's open for writing"; }
grep -q '^leasekill=PASS' "$ROOT/wk.log" || { cat "$ROOT/wk.log" >&2; fail "wine writer: no PASS"; }
[ "$SUM" = "$(md5sum < /home/$ADM/wexel/lv.exe)" ] || fail "the image changed"
wait_no_lease "/home/$ADM/wexel/lv.exe" || fail "the lease outlived the process"
ok c4_wine_program_opening_the_running_image_for_writing_ends_the_program_and_is_refused

# a normal run: the lease goes with the process, and the file is writable at once afterwards
el_clean; touch "$EL/approve.all"
run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/lv.exe"
bp c4_normal_run_with_a_lease "$ADM" 64 run "$(winpath /home/$ADM/wexel/lv.exe)" 0 "$(winpath $EL/out.lease)" -
wait_no_lease "/home/$ADM/wexel/lv.exe" || fail "the lease outlived a normal run"
lease_writer append "/home/$ADM/wexel/lv.exe" || fail "the writer after a normal run"
ok c4_lease_is_given_up_when_the_program_exits

# a root-owned image (the user cannot write it) needs no lease; a user-owned one has one
for who in user root; do
    el_clean; touch "$EL/approve.all"
    if [ $who = root ]; then
        install -m 755 -o root -g root "$ROOT/probes/wexelevate_probe64.exe" "$SYS32/leaseprobe.exe"; img="$SYS32/leaseprobe.exe"; wimg='C:\windows\system32\leaseprobe.exe'
    else
        run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/lv.exe"; img="/home/$ADM/wexel/lv.exe"; wimg="$(winpath $img)"
    fi
    bprobe "$ADM" 64 run "$wimg" 0 "$(winpath $EL/out.own)" "-" "hold h$who" > "$ROOT/lh.log" 2>&1 & LP=$!
    for i in $(seq 1 60); do grep -q 'ready=1' "$EL/out.own" 2>/dev/null && break; sleep 1; done
    grep -q 'ready=1' "$EL/out.own" 2>/dev/null || { cat "$ROOT/lh.log" >&2; fail "no elevated process ($who)"; }
    if [ $who = root ]; then ! has_lease "$img" || fail "a lease on a root-owned image"; else has_lease "$img" || fail "no lease on a user-owned image"; fi
    touch "$EL/release.h$who"
    wait "$LP" || { cat "$ROOT/lh.log" >&2; fail "run of the $who-owned image"; }
done
rm -f "$SYS32/leaseprobe.exe"
ok c4_root_owned_image_needs_no_lease_and_user_owned_has_one

# no lease can be taken: a writer has the file open (32), a file the user can write but does not own (665)
el_clean; touch "$EL/approve.all"
run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/lv.exe"
run_as "$ADM" bash -c 'exec 3>>"$1"; sleep 25' _ "/home/$ADM/wexel/lv.exe" & WP=$!
sleep 1
bp c4_image_open_for_writing_by_someone_fails_with_sharing_violation "$ADM" 64 run "$(winpath /home/$ADM/wexel/lv.exe)" 32 "$(winpath $EL/out.nolease)" -
pkill -u "$ADM" -f 'sleep 25' || true; wait "$WP" 2>/dev/null || true
install -m 666 -o root -g root "$ROOT/probes/wexelevate_probe64.exe" "/tmp/wexelev/rootw.exe"
bp c4_image_writable_by_the_user_but_not_owned_fails_with_file_system_limitation "$ADM" 64 run "$(winpath $EL/rootw.exe)" 665 "$(winpath $EL/out.nolease)" -
[ ! -e "$EL/out.nolease" ] || fail "a request without a lease started a process"
ok c4_no_lease_no_process

# -- 4. A/B concurrency: approving A does not approve B
el_clean; touch "$EL/wait-for-gate" "$EL/approve.a.exe"
bprobe "$ADM" 64 concurrent "$(winpath /home/$ADM/wexel/a.exe)" "$(winpath /home/$ADM/wexel/b.exe)" "$(winpath $EL/out.a)" "$(winpath $EL/out.b)" \
    > "$ROOT/conc.log" 2>&1 & CPID=$!
el_wait_file "$EL/started.a.exe"; el_wait_file "$EL/started.b.exe"
[ "$(head -1 "$EL/started.a.exe")" != "$(head -1 "$EL/started.b.exe")" ] || fail "two requests have the same id"
touch "$EL/gate"
wait "$CPID" || { cat "$ROOT/conc.log" >&2; fail "concurrent"; }
grep -q '^concurrent=PASS' "$ROOT/conc.log" || { cat "$ROOT/conc.log" >&2; fail "concurrent: no PASS"; }
ok c4_approving_a_does_not_approve_b

# -- 5. arguments, a refusal, bad requests, a forged id
el_clean; touch "$EL/approve.all"
bp c4_long_unicode_and_quoted_arguments_survive "$ADM" 64 args "$IMG" "$(winpath $EL/out.args)"
bp c4_long_unicode_and_quoted_arguments_survive_32 "$ADM" 32 args "$(winpath /home/$ADM/wexel/p32.exe)" "$(winpath $EL/out.args32)"
bp c4_forged_pid_is_attributed_to_the_real_caller "$ADM" 64 forge "$IMG" "$(winpath $EL/out.forge)"
TAGCD="wexcd-$$"
bp c4_impersonating_opener_is_denied "$ADM" 64 impersonate "$IMG" "$(winpath $EL/out.imp)"
# E1-S3: real writers, with no server/broker timing hook. The parent remains
# alive while the inherited duplicate is written by a different process.
for arch in 64 32; do
    pimage="$(winpath /home/$ADM/wexel/p$arch.exe)"
    for writer_case in self transceive write-impersonate transceive-impersonate delegate delegate-transceive dacl-restore; do
        rm -f "$EL/started.p$arch.exe"
        bp "c4_consumed_writer_${writer_case}_$arch" "$ADM" "$arch" provenance "$writer_case" "$pimage" "$(winpath $EL/out.provenance.$writer_case.$arch)"
        case "$writer_case" in
            self|transceive) ;;
            *) [ ! -e "$EL/started.p$arch.exe" ] || fail "writer mismatch reached consent: $writer_case/$arch" ;;
        esac
    done
done
bp c4_caller_death_leaves_no_child "$ADM" 64 callerdeath "$IMG" "$(winpath $EL/out.death)" "$TAGCD"
# R13: the caller's job and debugger do not reach the elevated process (both ABIs). These cases run against the
# production broker recognition, not the lab alias: the canonical system32/wexelevate.exe becomes the test broker's
# inode (a hard link) and the session restarts, so the broker image is recognised afresh at its first mapping.
el_down "$ADM"
R13_ORIG=""
if [ -e "$SYS32/wexelevate.exe" ]; then R13_ORIG="$ROOT/wexelevate.exe.r13"; cp -p "$SYS32/wexelevate.exe" "$R13_ORIG"; fi
ln -f "$SYS32/wexelevate_test.exe" "$SYS32/wexelevate.exe"
el_up "$ADM"
for arch in 64 32; do
    pimage="$(winpath /home/$ADM/wexel/p$arch.exe)"
    TAGJ="wexjob-$$-$arch"
    bp "c4_caller_job_not_inherited_$arch" "$ADM" "$arch" jobparent "$pimage" "$(winpath $EL/out.job.$arch)" "$TAGJ"
    # the probe ended itself in TerminateJobObject; the elevated child must have survived it
    CP=""
    for _ in $(seq 1 30); do CP="$(el_proc "$ADM" "child .*hold $TAGJ")"; [ -n "$CP" ] && break; sleep 1; done
    [ -n "${CP:-}" ] || fail "c4_caller_job_not_inherited_$arch: the elevated process did not survive the job termination"
    touch "$EL/release.$TAGJ"
    TAGD="wexdbg-$$-$arch"
    bp "c4_caller_debugger_not_inherited_$arch" "$ADM" "$arch" debugparent "$pimage" "$(winpath $EL/out.dbg.$arch)" "$TAGD"
done
# R8 step 3: dialog protection, loader pin, dialog image
cat "$M/system.reg" > "$ROOT/system.reg.r8"
regpath() { echo "Z:${1//\//\\\\}"; }

for arch in 64 32; do
    # protected: mode 1, image probe r8child; openpid gets error 5
    el_down "$ADM"
    el_clean
    cat "$ROOT/system.reg.r8" > "$M/system.reg"
    reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' \
        '"Start"=dword:00000002'
    reg_append "$M/system.reg" 'Software\\WineExplorer\\ElevationTest' \
        '"R8DialogTest"=dword:00000001' \
        '"R8DialogImage"="'"$(regpath "/home/$ADM/wexel/p$arch.exe")"'"' \
        '"R8DialogArgs"="r8child '"$(regpath "$EL/r8_pid_$arch.txt")"'"' \
        '"R8DialogResult"="'"$(regpath "$EL/r8_res_$arch.txt")"'"'
    r8_log="/tmp/wex_el_r8_${arch}.log"
    rm -f "$r8_log"
    WEX_EL_LOG="$r8_log" el_up "$ADM"
    wine_as "$ADM" "$ROOT/probes/rolesprobe64.exe" start-service wexelevate 0 >/dev/null 2>&1 || true
    for _ in $(seq 1 10); do [ -s "$EL/r8_res_$arch.txt" ] && break; sleep 1; done
    [ -s "$EL/r8_res_$arch.txt" ] || fail "r8_dialog_protected_open_denied_$arch: no result file"
    tr -d '\r' < "$EL/r8_res_$arch.txt" | grep -qx 'create_error=0' || fail "r8_dialog_protected_open_denied_$arch: create_error not 0"
    child_pid="$(tr -d '\r' < "$EL/r8_res_$arch.txt" | grep '^pid=' | cut -d= -f2)"
    [ -n "$child_pid" ] && [ "$child_pid" != 0 ] || fail "r8_dialog_protected_open_denied_$arch: no child pid"
    bprobe "$ADM" "$arch" openpid "$child_pid" "$(winpath "$EL/r8_open_${arch}.txt")"
    tr -d '\r' < "$EL/r8_open_${arch}.txt" | grep -qx 'error=5' || fail "r8_dialog_protected_open_denied_$arch: open not denied"
    ok "r8_dialog_protected_open_denied_$arch"

    if has_str "$BUILD/server/wineserver" "wex-r8:"; then
        grep -q 'wex-r8: dialog [0-9a-fA-F]* protected' "$r8_log" || fail "r8_pin_is_child_pid_$arch: no protected log"
        child_unix_pid="$(el_proc root "r8child")"
        [ -n "$child_unix_pid" ] || child_unix_pid="$(el_proc "$ADM" "r8child")"
        [ -n "$child_unix_pid" ] || fail "r8_pin_is_child_pid_$arch: child unix process not found"
        grep -q "wex-r8: dialog [0-9a-fA-F]* pin pid=$child_unix_pid" "$r8_log" || fail "r8_pin_is_child_pid_$arch: pin pid does not match unix pid"
        ok "r8_pin_is_child_pid_$arch"

        rm -f "$EL/r8_caller_${arch}.txt"
        bprobe "$ADM" "$arch" r8child "$(winpath "$EL/r8_caller_${arch}.txt")" >/dev/null 2>&1 &
        CPID=$!
        for _ in $(seq 1 10); do [ -s "$EL/r8_caller_${arch}.txt" ] && break; sleep 1; done
        [ -s "$EL/r8_caller_${arch}.txt" ] || fail "r8_caller_pin_is_unix_pid_$arch: no result file"
        caller_unix_pid="$(el_proc "$ADM" "r8_caller_${arch}.txt")"
        [ -n "$caller_unix_pid" ] || fail "r8_caller_pin_is_unix_pid_$arch: caller unix process not found"
        grep -q "wex-r8: process [0-9a-fA-F]* pin pid=$caller_unix_pid" "$r8_log" || fail "r8_caller_pin_is_unix_pid_$arch: pin pid does not match unix pid"
        kill -9 "$caller_unix_pid" 2>/dev/null || true
        wait "$CPID" 2>/dev/null || true
        ok "r8_caller_pin_is_unix_pid_$arch"

        if [ "$arch" = 64 ]; then
            grep -q 'image consent=0' "$r8_log" || fail "r8_image_probe_not_dialog: probe not consent=0"
            ok r8_image_probe_not_dialog

            wspid="$(server_pid "$ADM")"
            [ -n "$wspid" ] || fail "r8_pidfd_no_leak: wineserver not running"
            fd_before=$(ls -1 "/proc/$wspid/fd" 2>/dev/null | wc -l)
            bprobe "$ADM" 64 fdchurn 2000 >/dev/null 2>&1 || fail "r8_pidfd_no_leak: fdchurn failed"
            fd_after=$(ls -1 "/proc/$wspid/fd" 2>/dev/null | wc -l)
            diff=$(( fd_after > fd_before ? fd_after - fd_before : fd_before - fd_after ))
            [ "$diff" -le 5 ] || fail "r8_pidfd_no_leak: fd leaked: before=$fd_before after=$fd_after diff=$diff"
            ok r8_pidfd_no_leak
        fi
    fi

    # control: mode 3, same image; openpid gets error 0
    el_down "$ADM"
    el_clean
    cat "$ROOT/system.reg.r8" > "$M/system.reg"
    reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' \
        '"Start"=dword:00000002'
    reg_append "$M/system.reg" 'Software\\WineExplorer\\ElevationTest' \
        '"R8DialogTest"=dword:00000003' \
        '"R8DialogImage"="'"$(regpath "/home/$ADM/wexel/p$arch.exe")"'"' \
        '"R8DialogArgs"="r8child '"$(regpath "$EL/r8_pid_ctrl_$arch.txt")"'"' \
        '"R8DialogResult"="'"$(regpath "$EL/r8_res_ctrl_$arch.txt")"'"'
    el_up "$ADM"
    wine_as "$ADM" "$ROOT/probes/rolesprobe64.exe" start-service wexelevate 0 >/dev/null 2>&1 || true
    for _ in $(seq 1 10); do [ -s "$EL/r8_res_ctrl_$arch.txt" ] && break; sleep 1; done
    [ -s "$EL/r8_res_ctrl_$arch.txt" ] || fail "r8_unprotected_control_opens_$arch: no result file"
    tr -d '\r' < "$EL/r8_res_ctrl_$arch.txt" | grep -qx 'create_error=0' || fail "r8_unprotected_control_opens_$arch: create_error not 0"
    child_pid="$(tr -d '\r' < "$EL/r8_res_ctrl_$arch.txt" | grep '^pid=' | cut -d= -f2)"
    [ -n "$child_pid" ] && [ "$child_pid" != 0 ] || fail "r8_unprotected_control_opens_$arch: no child pid"
    bprobe "$ADM" "$arch" openpid "$child_pid" "$(winpath "$EL/r8_open_ctrl_${arch}.txt")"
    tr -d '\r' < "$EL/r8_open_ctrl_${arch}.txt" | grep -qx 'error=0' || fail "r8_unprotected_control_opens_$arch: open failed"
    ok "r8_unprotected_control_opens_$arch"
done

# bad rights: mode 2; create_error=5, pid=0, no child process
el_down "$ADM"
el_clean
cat "$ROOT/system.reg.r8" > "$M/system.reg"
reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' \
    '"Start"=dword:00000002'
reg_append "$M/system.reg" 'Software\\WineExplorer\\ElevationTest' \
    '"R8DialogTest"=dword:00000002' \
    '"R8DialogImage"="'"$(regpath "/home/$ADM/wexel/p64.exe")"'"' \
    '"R8DialogArgs"="r8child '"$(regpath "$EL/r8_pid_bad.txt")"'"' \
    '"R8DialogResult"="'"$(regpath "$EL/r8_res_bad.txt")"'"'
el_up "$ADM"
wine_as "$ADM" "$ROOT/probes/rolesprobe64.exe" start-service wexelevate 0 >/dev/null 2>&1 || true
for _ in $(seq 1 10); do [ -s "$EL/r8_res_bad.txt" ] && break; sleep 1; done
[ -s "$EL/r8_res_bad.txt" ] || fail "r8_dialog_unnarrowed_refused: no result file"
tr -d '\r' < "$EL/r8_res_bad.txt" | grep -qx 'create_error=5' || fail "r8_dialog_unnarrowed_refused: expected create_error=5"
tr -d '\r' < "$EL/r8_res_bad.txt" | grep -qx 'pid=0' || fail "r8_dialog_unnarrowed_refused: expected pid=0"
child_bad="$(el_proc root "r8child")"
[ -n "$child_bad" ] || child_bad="$(el_proc "$ADM" "r8child")"
[ -z "$child_bad" ] || fail "r8_dialog_unnarrowed_refused: child process was created"
ok r8_dialog_unnarrowed_refused

if has_str "$BUILD/server/wineserver" "wex-r8:"; then
    # further mode 1 run with consent.exe
    el_down "$ADM"
    el_clean
    cat "$ROOT/system.reg.r8" > "$M/system.reg"
    reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' \
        '"Start"=dword:00000002'
    reg_append "$M/system.reg" 'Software\\WineExplorer\\ElevationTest' \
        '"R8DialogTest"=dword:00000001' \
        '"R8DialogImage"="C:\\windows\\system32\\consent.exe"' \
        '"R8DialogArgs"="/broker /credentials"' \
        '"R8DialogResult"="'"$(regpath "$EL/r8_res_consent.txt")"'"'
    r8_consent_log="/tmp/wex_el_r8_consent.log"
    rm -f "$r8_consent_log"
    WEX_EL_LOG="$r8_consent_log" el_up "$ADM"
    wine_as "$ADM" "$ROOT/probes/rolesprobe64.exe" start-service wexelevate 0 >/dev/null 2>&1 || true
    for _ in $(seq 1 10); do [ -s "$EL/r8_res_consent.txt" ] && break; sleep 1; done
    grep -q 'image consent=1' "$r8_consent_log" || fail "r8_image_consent_is_dialog: consent not recognized"
    ok r8_image_consent_is_dialog
fi

el_down "$ADM"
cat "$ROOT/system.reg.r8" > "$M/system.reg"
rm -f "$ROOT/system.reg.r8"
rm -f "$SYS32/wexelevate.exe"
[ -z "$R13_ORIG" ] || mv "$R13_ORIG" "$SYS32/wexelevate.exe"
el_up "$ADM"
# E1-S5: broker-owned disabled records, actual H2/S3 consumed message binding.
# Endpoint/account identities, verifier and clock are fixtures; no credentials
# or administrator token are issued. Both client ABIs use the same lab envelope.
for arch in 64 32; do
    pimage="$(winpath /home/$ADM/wexel/p$arch.exe)"
    for record_case in accept request-id nonce digest replay expired token-replaced account cancel binding sequence generation logon \
                       expiry-consume expiry-commit token-precommit version redemption mutated-request; do
        rm -f "$EL/started.p$arch.exe"
        bp "c4_e1s5_${record_case}_$arch" "$ADM" "$arch" e1s "$record_case" "$pimage" "$(winpath $EL/out.e1s5.$record_case.$arch)"
        [ ! -e "$EL/started.p$arch.exe" ] || fail "E1-S5 fixture reached consent: $record_case/$arch"
    done
done
if [ "${WEX_E1S6_LAB:-0}" = 1 ]; then
    e1s6_start "$BUILD/server/wineserver" "$GRP" "$ROOT"
    for arch in 64 32; do
        pimage="$(winpath /home/$ADM/wexel/p$arch.exe)"
        for transport_case in accept stale replay late owner disconnect cancel child-impostor; do
            rm -f "$EL/started.p$arch.exe"
            native_line=$(wc -l < "$ROOT/e1s6-service.log")
            wine_line=$(wc -l < "$ROOT/e1s6-wine.log")
            bp "c4_e1s6_${transport_case}_$arch" "$ADM" "$arch" e1s "transport-$transport_case" \
                "$pimage" "$(winpath $EL/out.e1s6.$transport_case.$arch)"
            [ ! -e "$EL/started.p$arch.exe" ] || fail "S6 transport reached consent"
            # A generic setup denial must never pass a negative fixture. Check
            # the actual targeted boundary, in this case's new log lines only.
            native_case=$(tail -n +"$((native_line + 1))" "$ROOT/e1s6-service.log")
            wine_case=$(tail -n +"$((wine_line + 1))" "$ROOT/e1s6-wine.log")
            grep -qx 'E1S6_STAGE native phase=open' <<<"$native_case" || fail "S6 $transport_case/$arch: no real Open"
            case "$transport_case" in
                accept) grep -qx 'E1S6_STAGE native phase=exchange' <<<"$native_case" ;;
                stale) grep -q 'site=exchange-registration ' <<<"$native_case" ;;
                replay) grep -q 'site=late-replay ' <<<"$native_case" ;;
                late) grep -q 'site=live-child-pin ' <<<"$native_case" ;;
                owner) grep -q 'site=live-owner ' <<<"$wine_case" ;;
                disconnect) grep -q 'site=receive-eof ' <<<"$native_case" ;;
                cancel) grep -qx 'E1S6_STAGE native phase=cancel' <<<"$native_case" ;;
                child-impostor) grep -q 'site=child-identity ' <<<"$native_case" ;;
            esac || fail "S6 $transport_case/$arch: target denial/completion site missing"
        done
        wine_line=$(wc -l < "$ROOT/e1s6-wine.log")
        bp "c4_e1s6_pipe_impostor_$arch" "$ADM" "$arch" transport-impostor
        wine_case=$(tail -n +"$((wine_line + 1))" "$ROOT/e1s6-wine.log")
        grep -q '^E1S6_DENY pipe site=authority$' <<<"$wine_case" ||
            fail "S6 pipe impostor/$arch: server authority denial missing"

        wine_line=$(wc -l < "$ROOT/e1s6-wine.log")
        bp "c4_e1s6_r8_impostor_$arch" "$ADM" "$arch" r8binding-impostor
        wine_case=$(tail -n +"$((wine_line + 1))" "$ROOT/e1s6-wine.log")
        grep -q '^E1S6_DENY pipe site=authority$' <<<"$wine_case" ||
            fail "S6 pipe r8 impostor/$arch: server authority denial missing"

        # WO-23: verified broker BINDING (op 1) test
        el_down "$ADM"
        cp -p "$SYS32/wexelevate_test.exe" "$ROOT/wexelevate_test.exe.r8bak"
        install -m 775 -o root -g "$GRP" "$ROOT/probes/wexelevate_probe$arch.exe" "$SYS32/wexelevate_test.exe"
        if [ "$arch" = 32 ] && [ -d "$M/drive_c/windows/syswow64" ]; then
            [ ! -f "$M/drive_c/windows/syswow64/wexelevate_test.exe" ] || cp -p "$M/drive_c/windows/syswow64/wexelevate_test.exe" "$ROOT/wexelevate_test.exe.r8bak32"
            install -m 775 -o root -g "$GRP" "$ROOT/probes/wexelevate_probe$arch.exe" "$M/drive_c/windows/syswow64/wexelevate_test.exe"
        fi
        cat "$M/system.reg" > "$ROOT/system.reg.r8b"
        reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' \
            '"ImagePath"="C:\\windows\\system32\\wexelevate_test.exe r8binding '"$(regpath "$EL/r8_bind1_${arch}.txt")"'"'
        r8_log="/tmp/wex_r8_b1_${arch}.log"
        rm -f "$r8_log" "$EL/r8_bind1_${arch}.txt"
        WEX_EL_LOG="$r8_log" el_up "$ADM"
        wine_as "$ADM" "$ROOT/probes/rolesprobe64.exe" start-service wexelevate 0 >/dev/null 2>&1 || true
        for _ in $(seq 1 15); do [ -s "$EL/r8_bind1_${arch}.txt" ] && break; sleep 1; done
        [ -s "$EL/r8_bind1_${arch}.txt" ] || fail "r8_binding_$arch: no result file 1"
        tr -d '\r' < "$EL/r8_bind1_${arch}.txt" > "$EL/r8_bind1_${arch}.txt.tmp" && mv "$EL/r8_bind1_${arch}.txt.tmp" "$EL/r8_bind1_${arch}.txt"
        grep -qx 'r8binding=PASS' "$EL/r8_bind1_${arch}.txt" || { cat "$EL/r8_bind1_${arch}.txt" >&2; fail "r8_binding_$arch: step 1 failed"; }
        ok "r8_binding_op1_$arch"

        map_gen1="$(grep '^map_gen=' "$EL/r8_bind1_${arch}.txt" | cut -d= -f2)"
        pol_gen1="$(grep '^policy_gen=' "$EL/r8_bind1_${arch}.txt" | cut -d= -f2)"
        [ -n "$map_gen1" ] && [ -n "$pol_gen1" ] || fail "r8_binding_$arch: missing generations in result 1"

        # Bump generation in accounts.map
        cp -p /etc/wineexplorer/accounts.map /tmp/accounts.map.r8bak
        old_map_gen=$(awk 'NR==1 {print $3}' /etc/wineexplorer/accounts.map)
        new_map_gen=$(( old_map_gen + 42 ))
        sed -i "1s/ $old_map_gen / $new_map_gen /" /etc/wineexplorer/accounts.map
        # Touch sysrun.conf
        sleep 1
        touch /etc/wineexplorer/sysrun.conf

        el_down "$ADM"
        cat "$ROOT/system.reg.r8b" > "$M/system.reg"
        reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' \
            '"ImagePath"="C:\\windows\\system32\\wexelevate_test.exe r8binding '"$(regpath "$EL/r8_bind2_${arch}.txt")"'"'
        rm -f "$EL/r8_bind2_${arch}.txt"
        el_up "$ADM"
        wine_as "$ADM" "$ROOT/probes/rolesprobe64.exe" start-service wexelevate 0 >/dev/null 2>&1 || true
        for _ in $(seq 1 15); do [ -s "$EL/r8_bind2_${arch}.txt" ] && break; sleep 1; done
        # Restore the map now
        cp -p /tmp/accounts.map.r8bak /etc/wineexplorer/accounts.map
        rm -f /tmp/accounts.map.r8bak
        [ -s "$EL/r8_bind2_${arch}.txt" ] || fail "r8_binding_$arch: no result file 2"
        tr -d '\r' < "$EL/r8_bind2_${arch}.txt" > "$EL/r8_bind2_${arch}.txt.tmp" && mv "$EL/r8_bind2_${arch}.txt.tmp" "$EL/r8_bind2_${arch}.txt"
        grep -qx 'r8binding=PASS' "$EL/r8_bind2_${arch}.txt" || { cat "$EL/r8_bind2_${arch}.txt" >&2; fail "r8_binding_$arch: step 2 failed"; }

        map_gen2="$(grep '^map_gen=' "$EL/r8_bind2_${arch}.txt" | cut -d= -f2)"
        pol_gen2="$(grep '^policy_gen=' "$EL/r8_bind2_${arch}.txt" | cut -d= -f2)"

        [ "$map_gen1" != "$map_gen2" ] || fail "r8_binding_$arch: map_gen did not change ($map_gen1 == $map_gen2)"
        ok "r8_binding_map_gen_changed_$arch"

        [ "$pol_gen1" != "$pol_gen2" ] || fail "r8_binding_$arch: policy_gen did not change ($pol_gen1 == $pol_gen2)"
        ok "r8_binding_policy_gen_changed_$arch"

        # WO-24: verified broker ENDORSE_DIALOG (op 2) and RECHECK (op 3) tests
        el_down "$ADM"
        cat "$ROOT/system.reg.r8b" > "$M/system.reg"
        reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' \
            '"ImagePath"="C:\\windows\\system32\\wexelevate_test.exe r8endorse '"$(regpath "$EL/r8_end1_${arch}.txt")"'"'
        r8_log="/tmp/wex_r8_e1_${arch}.log"
        rm -f "$r8_log" "$EL/r8_end1_${arch}.txt" "$EL/r8_end1_${arch}.txt.ready" "$EL/r8_end1_${arch}.txt.done"
        e1s6_line_before=$(wc -l < "$ROOT/e1s6-service.log")
        WEX_EL_LOG="$r8_log" el_up "$ADM"
        wine_as "$ADM" "$ROOT/probes/rolesprobe64.exe" start-service wexelevate 0 >/dev/null 2>&1 &
        SP=$!

        for _ in $(seq 1 300); do [ -f "$EL/r8_end1_${arch}.txt.ready" ] && break; sleep 0.1; done
        [ -f "$EL/r8_end1_${arch}.txt.ready" ] || { kill $SP 2>/dev/null || true; fail "r8_endorse_$arch: phase 1 not ready"; }

        sleep 0.1
        touch /etc/wineexplorer/sysrun.conf
        touch "$EL/r8_end1_${arch}.txt.done"

        for _ in $(seq 1 300); do [ -s "$EL/r8_end1_${arch}.txt" ] && break; sleep 0.1; done
        wait $SP 2>/dev/null || true
        sleep 0.1
        touch /etc/wineexplorer/sysrun.conf

        [ -s "$EL/r8_end1_${arch}.txt" ] || fail "r8_endorse_$arch: no result file"
        tr -d '\r' < "$EL/r8_end1_${arch}.txt" > "$EL/r8_end1_${arch}.txt.tmp" && mv "$EL/r8_end1_${arch}.txt.tmp" "$EL/r8_end1_${arch}.txt"

        for check_name in endorse_ok second_endorse_denied recheck_ok recheck_bad_dialog_id_denied \
                          gen_server_denied gen_root_denied gen_broker_denied \
                          gen_map_denied gen_policy_denied desktop_route_denied \
                          caller_session_denied desktop_mode_denied payload_215_denied \
                          payload_217_denied resumed_d_denied probe_image_d_denied \
                          no_binding_denied touch_policy_denied terminated_d_denied; do
            grep -qx "${check_name}=PASS" "$EL/r8_end1_${arch}.txt" || { cat "$EL/r8_end1_${arch}.txt" >&2; fail "r8_endorse_${check_name}_$arch failed"; }
            ok "r8_endorse_${check_name}_$arch"
        done

        # WO-26 verifier checks
        e1s6_new_log="$(tail -n +"$((e1s6_line_before + 1))" "$ROOT/e1s6-service.log")"
        consent_unix_pid="$(grep 'wex-r8: dialog [0-9a-fA-F]* pin pid=' "$r8_log" | head -1 | sed -n 's/.*pin pid=\([0-9]*\).*/\1/p')"
        [ -n "$consent_unix_pid" ] || fail "r8_endorsed_to_verifier_$arch: consent unix pid not found in server log"
        endorsed_lines="$(grep -c '^E1R8_ENDORSED ok' <<<"$e1s6_new_log" || true)"
        [ "$endorsed_lines" -eq 1 ] || fail "r8_endorsed_to_verifier_$arch: expected 1 ENDORSED line, got $endorsed_lines"
        grep -q "^E1R8_ENDORSED ok .*pid=$consent_unix_pid\$" <<<"$e1s6_new_log" ||
            fail "r8_endorsed_to_verifier_$arch: verifier log pid mismatch"
        ok "r8_endorsed_to_verifier_$arch"

        grep -qx 'closed_channel_denied=PASS' "$EL/r8_end1_${arch}.txt" ||
            fail "r8_endorsed_needs_channel_$arch: endorse after closed channel not denied"
        ok "r8_endorsed_needs_channel_$arch"

        # Restore broker
        el_down "$ADM"
        cat "$ROOT/system.reg.r8b" > "$M/system.reg"
        rm -f "$ROOT/system.reg.r8b"
        mv "$ROOT/wexelevate_test.exe.r8bak" "$SYS32/wexelevate_test.exe"
        if [ -f "$ROOT/wexelevate_test.exe.r8bak32" ]; then
            mv "$ROOT/wexelevate_test.exe.r8bak32" "$M/drive_c/windows/syswow64/wexelevate_test.exe"
        elif [ "$arch" = 32 ] && [ -d "$M/drive_c/windows/syswow64" ]; then
            rm -f "$M/drive_c/windows/syswow64/wexelevate_test.exe"
        fi
        el_up "$ADM"
    done
    native_line=$(wc -l < "$ROOT/e1s6-service.log")
    run_as "$ADM" /usr/bin/python3 -I "$E1S6_CHILD" --impostor >"$ROOT/e1s6-impostor.log" 2>&1 ||
        { cat "$ROOT/e1s6-impostor.log" >&2; fail 'same-UID bus impostor probe failed'; }
    grep -qx 'E1S6_IMPOSTOR PASS' "$ROOT/e1s6-impostor.log" || fail 'bus impostor did not complete'
    native_case=$(tail -n +"$((native_line + 1))" "$ROOT/e1s6-service.log")
    grep -q 'site=endorsement-gid ' <<<"$native_case" ||
        fail 'bus impostor was not rejected at its actual sender credentials'
    ok c4_e1s6_same_uid_bus_impostor

    # WO-27: R8 end-to-end standard-user elevation. Defined here, run at the end of the C4 section (the call
    # before el_restore_broker; WEX_SKIP_R8_E2E=1 skips it): the cases after it fail when it runs earlier
    # (refused consent times out, error 1460): a lab fixture leftover; in a live VM B session refused consent
    # still ends with 1223 (docs/E1_S18E_WORK_ORDERS.md, WO-27 result).
    r8_e2e_case() {
    el_down "$ADM"
    cat "$M/system.reg" > "$ROOT/system.reg.r8e2e"
    reg_append "$M/system.reg" 'Software\\WineExplorer\\Elevation' \
        '"EnableStandardUserElevation"=dword:00000001'
    reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' \
        '"Start"=dword:00000002' \
        '"ImagePath"="C:\\windows\\system32\\wexelevate.exe"'

    # The lab build's E1S18 broker and consent variants (unsloth-tmp/wo23-build-lab.sh), for this case only.
    # Wine loads system32's builtin programs from the build tree by module name, so the build tree's own files
    # are rewritten in place (same inode: the loader and the server's image recognition both still match) and
    # their normal content is written back afterwards.
    # The server recognises the broker and the dialog by the machine-root system32/syswow64 entry, so those
    # entries point at the build files for this case (the originals are kept and put back).
    rm -rf "$ROOT/r8e2e-saved"; mkdir -p "$ROOT/r8e2e-saved/root"
    for pair in "x86_64:$SYS32:system32" "i386:$M/drive_c/windows/syswow64:syswow64"; do
        a="${pair%%:*}"; rest="${pair#*:}"; dir="${rest%:*}"; tag="${rest##*:}"
        [ -d "$dir" ] || continue
        for exe in wexelevate consent; do
            target="$BUILD/programs/$exe/$a-windows/$exe.exe"
            variant="$BUILD/programs/$exe/$a-windows/$exe-e1s18.exe"
            [ -f "$target" ] || continue
            [ -f "$variant" ] || fail "r8_e2e: $variant missing (lab build)"
            cp -p "$target" "$ROOT/r8e2e-saved/$a-$exe.exe"
            cat "$variant" > "$target"
            [ ! -e "$dir/$exe.exe" ] && [ ! -L "$dir/$exe.exe" ] || cp -a "$dir/$exe.exe" "$ROOT/r8e2e-saved/root/$tag-$exe.exe"
            ln -sfn "$target" "$dir/$exe.exe"
        done
    done

    for arch in 64 32; do
        el_down "$STD"
        el_clean
        rm -f "$EL/r8_e2e_res_${arch}.txt"
        e1s6_line_before=$(wc -l < "$ROOT/e1s6-service.log")
        wine_line_before=$(wc -l < "$ROOT/e1s6-wine.log")
        el_up "$STD"

        wine_as "$STD" "$ROOT/probes/wexelevate_probe$arch.exe" runinteractive \
            "$(winpath /home/$STD/wexel/p$arch.exe)" 1223 "$(winpath "$EL/r8_e2e_res_${arch}.txt")" - >/dev/null 2>&1 &
        PROBE_PID=$!

        for _ in $(seq 1 150); do
            wine_new_log="$(tail -n +"$((wine_line_before + 1))" "$ROOT/e1s6-wine.log" 2>/dev/null || true)"
            grep -q 'r8: recheck ok' <<<"$wine_new_log" && break
            sleep 0.1
        done

        wine_new_log="$(tail -n +"$((wine_line_before + 1))" "$ROOT/e1s6-wine.log" 2>/dev/null || true)"
        e1s6_new_log="$(tail -n +"$((e1s6_line_before + 1))" "$ROOT/e1s6-service.log" 2>/dev/null || true)"

        grep -q 'r8: prepare ok' <<<"$wine_new_log" || { echo "$wine_new_log" >&2; fail "r8_e2e_ack_$arch: prepare ok missing"; }
        grep -q 'r8: endorse ok' <<<"$wine_new_log" || { echo "$wine_new_log" >&2; fail "r8_e2e_ack_$arch: endorse ok missing"; }
        grep -q 'r8: ack ok' <<<"$wine_new_log" || { echo "$wine_new_log" >&2; fail "r8_e2e_ack_$arch: ack ok missing"; }
        grep -q 'r8: recheck ok' <<<"$wine_new_log" || { echo "$wine_new_log" >&2; fail "r8_e2e_ack_$arch: recheck ok missing"; }

        order_check="$(grep -E 'r8: (prepare|endorse|ack|recheck) ok' <<<"$wine_new_log" | tr '\n' ' ')"
        [[ "$order_check" =~ prepare.*endorse.*ack.*recheck ]] || fail "r8_e2e_ack_$arch: milestones not in order"

        consent_unix_pid="$(grep 'wex-r8: dialog [0-9a-fA-F]* pin pid=' <<<"$wine_new_log" | head -1 | sed -n 's/.*pin pid=\([0-9]*\).*/\1/p')"
        [ -n "$consent_unix_pid" ] || fail "r8_e2e_ack_$arch: consent unix pid not found in wine log"
        grep -q "^E1R8_ENDORSED ok .*pid=$consent_unix_pid\$" <<<"$e1s6_new_log" || fail "r8_e2e_ack_$arch: E1R8_ENDORSED ok pid mismatch"

        pkill -f consent 2>/dev/null || true
        wait "$PROBE_PID" 2>/dev/null || true

        ok "r8_e2e_ack_$arch"
    done

    # Negative test: with verifier stopped, request is denied and no consent created
    e1s6_stop
    el_down "$STD"
    el_clean
    wine_line_before=$(wc -l < "$ROOT/e1s6-wine.log")
    el_up "$STD"
    wine_as "$STD" "$ROOT/probes/wexelevate_probe64.exe" runinteractive \
        "$(winpath /home/$STD/wexel/p64.exe)" 5 "$(winpath "$EL/out.neg")" - >/dev/null 2>&1 || true
    wine_new_log="$(tail -n +"$((wine_line_before + 1))" "$ROOT/e1s6-wine.log" 2>/dev/null || true)"
    grep -q 'r8: prepare failed' <<<"$wine_new_log" || fail "r8_e2e_no_verifier_denied: prepare failure missing"
    [ ! -e "$EL/out.neg" ] || fail "r8_e2e_no_verifier_denied: child process started"
    ! pgrep -f 'consent' >/dev/null || fail "r8_e2e_no_verifier_denied: consent created"
    ok r8_e2e_no_verifier_denied
    # the later lab cases need the verifier again
    e1s6_start "$BUILD/server/wineserver" "$GRP" "$ROOT"

    # Restore fixture state
    cat "$ROOT/system.reg.r8e2e" > "$M/system.reg"; rm -f "$ROOT/system.reg.r8e2e"
    el_down "$STD"
    for a in x86_64 i386; do
        for exe in wexelevate consent; do
            [ ! -f "$ROOT/r8e2e-saved/$a-$exe.exe" ] || cat "$ROOT/r8e2e-saved/$a-$exe.exe" > "$BUILD/programs/$exe/$a-windows/$exe.exe"
        done
    done
    # machine-root entries: remove the case's links first, so the copy never writes through a link
    for pair in "$SYS32:system32" "$M/drive_c/windows/syswow64:syswow64"; do
        dir="${pair%:*}"; tag="${pair##*:}"
        for exe in wexelevate consent; do
            [ -L "$dir/$exe.exe" ] && rm -f "$dir/$exe.exe"
            [ ! -e "$ROOT/r8e2e-saved/root/$tag-$exe.exe" ] && [ ! -L "$ROOT/r8e2e-saved/root/$tag-$exe.exe" ] ||
                cp -a "$ROOT/r8e2e-saved/root/$tag-$exe.exe" "$dir/$exe.exe"
        done
    done
    rm -rf "$ROOT/r8e2e-saved"
    # the standard user's interactive processes keep WinSta0 alive; later cases run without it
    pkill -u "$STD" 2>/dev/null || true
    sleep 2
    el_up "$ADM"
    }
fi
bp c4_relative_image_path_is_rejected "$ADM" 64 run p64.exe 87 "$(winpath $EL/out.bad)" -
bp c4_missing_image_is_reported "$ADM" 64 run "$(winpath $EL/missing.exe)" 2 "$(winpath $EL/out.bad)" -
rm -f "$EL/approve.all"
bp c4_refused_consent_is_error_cancelled "$ADM" 64 run "$IMG" 1223 "$(winpath $EL/out.refused)" -
[ ! -e "$EL/out.refused" ] || fail "a refused request started a process"
ok c4_refused_request_started_nothing

# the image replaced while the consent is pending: the harness renames a different file over the path from Unix
# (the broker's read handle blocks a rename from Windows), after the probe signals imgrace.ready
el_clean; touch "$EL/wait-for-gate" "$EL/approve.imgrace.exe"
run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/imgrace.exe"
bprobe "$ADM" 64 imagerace "$(winpath /home/$ADM/wexel/imgrace.exe)" "$(winpath $EL/out.imgrace)" > "$ROOT/imgrace.log" 2>&1 & IP=$!
el_wait_file "$EL/imgrace.ready"
run_as "$ADM" bash -c 'cp "$1" "$1.new" && mv "$1.new" "$1"' _ "/home/$ADM/wexel/imgrace.exe"
touch "$EL/imgrace.done"
wait "$IP" || { cat "$ROOT/imgrace.log" >&2; fail "imagerace"; }
grep -q '^imagerace=PASS' "$ROOT/imgrace.log" || { cat "$ROOT/imgrace.log" >&2; fail "imagerace: no PASS"; }
[ ! -e "$EL/out.imgrace" ] || fail "the replaced image ran"
ok c4_image_replaced_while_consent_pending_is_denied_and_starts_nothing

# an existing writer on the image when the request is made
el_clean
bp c4_existing_writer_on_the_image_is_denied "$ADM" 64 writerfirst "$IMG" "$(winpath $EL/out.writer)"
el_down "$ADM"

# -- 6. a standard user
el_clean; touch "$EL/approve.all"
el_up "$STD"
for arch in 64 32; do
    bp "c4_standard_user_gets_not_supported_$arch" "$STD" "$arch" run "$(winpath /home/$STD/wexel/p$arch.exe)" 50 "$(winpath $EL/out.std)" -
done
[ ! -e "$EL/out.std" ] || fail "a standard user's request started a process"
ok c4_standard_user_started_nothing
el_down "$STD"

# -- 7. no lease support: with fs.leases-enable=0 the kernel refuses F_SETLEASE with EINVAL, as a file system without
# lease support does; the request fails with the distinct error and nothing runs, no fallback copy
el_clean; touch "$EL/approve.all"
el_up "$ADM"
run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/lv.exe"
LEASES_SAVED="$(cat /proc/sys/fs/leases-enable)"
echo 0 > /proc/sys/fs/leases-enable
bp c4_file_system_without_leases_fails_the_request_with_file_system_limitation "$ADM" 64 run "$(winpath /home/$ADM/wexel/lv.exe)" 665 "$(winpath $EL/out.nolease)" -
echo "$LEASES_SAVED" > /proc/sys/fs/leases-enable
[ ! -e "$EL/out.nolease" ] || fail "a request without a lease started a process"
bp c4_control_with_leases_enabled_again_runs "$ADM" 64 run "$(winpath /home/$ADM/wexel/lv.exe)" 0 "$(winpath $EL/out.nolease)" -
el_down "$ADM"

# -- 8. click consent (plan step 6, Part B): the broker starts the consent window as its own SYSTEM child, with the
# frozen image and command line; only Continue launches. consent_test.exe is consent.c compiled with the harness hook
# (WEXCONSENT_TEST_HOOK, defined by no build file), installed under another name for the same reason as the test broker.
echo "--- click consent (step 6, part B)"
mkdir -p "$ROOT/shim/wine"
cat > "$ROOT/shim/wine/debug.h" <<'EOF'
#define WINE_DEFAULT_DEBUG_CHANNEL(x)
#define TRACE(...) do {} while (0)
#define WARN(...) do {} while (0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
EOF
CS="$SRC/src/programs/consent"
x86_64-w64-mingw32-windres -I"$CS" -o "$ROOT/consent_rc.o" "$CS/consent.rc"
x86_64-w64-mingw32-gcc -O2 -Wall -municode -mwindows -DWEXCONSENT_TEST_HOOK -I"$ROOT/shim" -I"$CS" -o "$ROOT/probes/consent_test.exe" \
    "$CS/consent.c" "$ROOT/consent_rc.o" -lcomctl32 -lversion -lwintrust -lcrypt32 -luser32 -lgdi32 -ladvapi32
chmod 0755 "$ROOT/probes/consent_test.exe"
x86_64-w64-mingw32-gcc -O2 -Wall -municode -mwindows -I"$CS" -I"$ROOT/shim" -o "$ROOT/consent_ship.exe" \
    "$CS/consent.c" "$ROOT/consent_rc.o" -lcomctl32 -lversion -lwintrust -lcrypt32 -luser32 -lgdi32 -ladvapi32
has_str "$ROOT/probes/consent_test.exe" 'consent.' || fail "the test consent program has no hook (control)"
has_str "$ROOT/probes/consent_test.exe" '/testid' || fail "the test consent program has no /testid (control)"
! has_str "$ROOT/consent_ship.exe" '/testid' || fail "a consent program built without the macro contains the hook"
! has_str "$ROOT/consent_ship.exe" 'wexelev' || fail "a consent program built without the macro names the hook directory"
for f in "$BUILD"/programs/consent/*-windows/consent.exe; do
    # Debug information carries source paths (consent.c includes ../wexelevate/cred_dialog.c): scan the loadable image only.
    case "$f" in *i386-windows*) strip_tool=i686-w64-mingw32-strip ;; *) strip_tool=x86_64-w64-mingw32-strip ;; esac
    "$strip_tool" --strip-debug -o "$ROOT/consent_installed_stripped.exe" "$f" || fail "cannot strip $f for the hook check"
    ! has_str "$ROOT/consent_installed_stripped.exe" '/testid' && ! has_str "$ROOT/consent_installed_stripped.exe" 'wexelev' || fail "the installed program $f contains the hook"
    rm -f "$ROOT/consent_installed_stripped.exe"
done
! has_str "$ROOT/probes/wexelevate_ship.exe" consent_test || fail "a broker built without the macro names the test consent program"
has_str "$ROOT/probes/wexelevate_test.exe" consent_test || fail "the test broker does not use the test consent program (control)"
! grep -rn 'WEXCONSENT_TEST_HOOK' "$SRC/src" "$SRC/installer" "$SRC/tools" --include=Makefile.in --include='*.mk' --include='*.inf' \
    --include='*.sh' --include='*.py' >/dev/null 2>&1 || fail "a build file defines WEXCONSENT_TEST_HOOK"
[ -z "$(find "$M" "$BUILD/programs" "$BUILD/dlls" -name 'consent_test*' -print -quit)" ] || fail "the test consent program is in the installed tree"
ok c5_consent_hook_absent_from_shipping_build

cat > "$ROOT/setpolicy.py" <<'EOF'
import sys
path, value = sys.argv[1], sys.argv[2]
sec = r'[Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System]'
lines = open(path, encoding='latin-1').read().split('\n')
out, inside = [], False
for line in lines:
    if line.startswith('['):
        inside = line.startswith(sec)
    if inside and line.startswith('"ConsentPromptBehaviorAdmin"'):
        continue
    out.append(line)
    if inside and line.startswith('"EnableLUA"') and value != 'none':
        out.append('"ConsentPromptBehaviorAdmin"=dword:%08x' % int(value))
open(path, 'w', encoding='latin-1').write('\n'.join(out))
EOF
set_policy() { python3 -I "$ROOT/setpolicy.py" "$M/system.reg" "$1"; [ "$(grep -c ConsentPromptBehaviorAdmin "$M/system.reg")" = "$2" ]; }
install -m 775 -o root -g "$GRP" "$ROOT/probes/consent_test.exe" "$SYS32/consent_test.exe"
reg_append "$M/system.reg" 'Software\\WineExplorer\\ElevationTest' '"ConsentTimeoutMs"=dword:00004e20'
set_policy 2 1 || fail "the policy value was not set"

sdp() { local arch="$1"; shift; wine_as "$ADM" "$ROOT/probes/sdprobe$arch.exe" "$@" 2>&1 | tr -d '\r'; }
input_now() { sdp 64 query; }   # the input desktop's name; "(OpenInputDesktop returned NULL, err 5)" while it is the secure one
input_secure() { [[ "$(input_now)" == *"err 5"* ]]; }
wait_input_default() { local i; for i in $(seq 1 30); do [ "$(input_now)" = Default ] && return 0; sleep 1; done; return 1; }
consent_alive() { local p; for p in $(pgrep -f "$1"); do grep -q '^State:.*Z' "/proc/$p/status" 2>/dev/null || return 0; done; return 1; }   # a live (not zombie) process
wait_shown()   # prints the path of the first consent.<id>.shown file
{
    local i f
    for i in $(seq 1 90); do
        f="$(ls "$EL"/consent.*.shown 2>/dev/null | head -1)"
        [ -z "$f" ] || { sleep 0.5; echo "$f"; return 0; }
        sleep 1
    done
    return 1
}
consent_case()   # name expect-error action(approve|cancel|none)
{
    local name="$1" expect="$2" action="$3" img shown
    el_clean
    run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/cs.exe"
    img="$(winpath /home/$ADM/wexel/cs.exe)"
    bprobe "$ADM" 64 run "$img" "$expect" "$(winpath $EL/out.c5)" - > "$ROOT/cs.log" 2>&1 & CSP=$!
    shown="$(wait_shown)" || { cat "$ROOT/cs.log" >&2; fail "$name: no consent window"; }
    grep -qF "Program location: $img" "$shown" || { cat "$shown" >&2; fail "$name: the window does not show the frozen image"; }
    grep -qF "Command line: \"$img\" child $(winpath $EL/out.c5)" "$shown" || { cat "$shown" >&2; fail "$name: the window does not show the command line"; }
    grep -q '^publisher=Unknown publisher' "$shown" || { cat "$shown" >&2; fail "$name: no publisher line"; }
    grep -q '^program=cs.exe' "$shown" || { cat "$shown" >&2; fail "$name: no program name"; }
    # the prompt is on the secure desktop (the input desktop cannot even be opened by the limited user) unless the policy says no
    grep -qx "desktop=${WANT_DESKTOP:-Winlogon}" "$shown" || { cat "$shown" >&2; fail "$name: the window is not on ${WANT_DESKTOP:-Winlogon}"; }
    if [ "${WANT_DESKTOP:-Winlogon}" = Winlogon ]; then input_secure || fail "$name: the input desktop is not the secure desktop"
    else [ "$(input_now)" = Default ] || fail "$name: the input left Default"; fi
    [ "$action" = none ] || touch "${shown%.shown}.$action"
    wait "$CSP" || { cat "$ROOT/cs.log" >&2; fail "$name"; }
    grep -q '^run=PASS' "$ROOT/cs.log" || { cat "$ROOT/cs.log" >&2; fail "$name: no PASS"; }
    wait_input_default || fail "$name: the input did not return to Default"
}

el_up "$ADM"
consent_case c5_continue_launches_exactly_one_target 0 approve
[ "$(ls "$EL"/out.c5 2>/dev/null | wc -l)" = 1 ] && [ "$(grep -c '^pid=' "$EL/out.c5")" = 1 ] || fail "not exactly one target"
for _ in $(seq 1 20); do consent_alive 'system32.consent[_]test' || break; sleep 0.5; done
! consent_alive 'system32.consent[_]test' || fail "the consent window is still there"
ok c5_continue_launches_exactly_one_target
consent_case c5_cancel_is_error_cancelled_and_launches_nothing 1223 cancel
[ ! -e "$EL/out.c5" ] || fail "a cancelled request started a process"
ok c5_cancel_launches_nothing
consent_case c5_timeout_is_error_timeout_and_launches_nothing 1460 none
[ ! -e "$EL/out.c5" ] || fail "a request that timed out started a process"
for _ in $(seq 1 20); do consent_alive 'system32.consent[_]test' || break; sleep 0.5; done
! consent_alive 'system32.consent[_]test' || fail "the consent window outlived the timeout"
ok c5_timeout_launches_nothing_and_closes_the_window

# the window is the broker's own child, with the SYSTEM token, not the caller's
el_clean
run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/cs.exe"
bprobe "$ADM" 64 run "$(winpath /home/$ADM/wexel/cs.exe)" 1223 "$(winpath $EL/out.c5)" - > "$ROOT/cs.log" 2>&1 & CSP=$!
shown="$(wait_shown)" || fail "no consent window"
grep -qx 'user=S-1-5-18' "$shown" || { cat "$shown" >&2; fail "the consent window does not run as SYSTEM"; }
ok c5_consent_window_runs_as_system
# a limited process of the same user cannot open it with rights that would let it end it (exit code 0 would approve)
OUT="$(bprobe "$ADM" 64 consentattack || true)"
grep -q '^consentattack=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "a limited process could open the consent process, or its parent is not the broker"; }
ok c5_consent_window_is_a_child_of_the_broker_and_a_limited_process_cannot_open_it
touch "${shown%.shown}.cancel"
wait "$CSP" || { cat "$ROOT/cs.log" >&2; fail "the cancelled request"; }

# the window shows the frozen image, not the first word of the command line
el_clean
bprobe "$ADM" 64 spoofcmd "$(winpath /home/$ADM/wexel/cs.exe)" 'C:\Windows\System32\notepad.exe' "$(winpath $EL/out.spoof)" > "$ROOT/cs.log" 2>&1 & CSP=$!
shown="$(wait_shown)" || fail "no consent window"
grep -qF "Program location: $(winpath /home/$ADM/wexel/cs.exe)" "$shown" && grep -q '^program=cs.exe' "$shown" &&
    grep -qF 'notepad.exe' "$shown" && ! grep -q '^program=notepad' "$shown" || { cat "$shown" >&2; fail "the window shows the command line's program instead of the image"; }
touch "${shown%.shown}.approve"
wait "$CSP" || { cat "$ROOT/cs.log" >&2; fail "spoofcmd"; }
ok c5_window_shows_the_frozen_image_not_the_command_line

# two requests: approving one does not approve the other
el_clean
run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/a.exe"
bprobe "$ADM" 64 concurrent "$(winpath /home/$ADM/wexel/a.exe)" "$(winpath /home/$ADM/wexel/b.exe)" "$(winpath $EL/out.a)" "$(winpath $EL/out.b)" \
    > "$ROOT/conc.log" 2>&1 & CSP=$!
for _ in $(seq 1 90); do [ "$(ls "$EL"/consent.*.shown 2>/dev/null | wc -l)" = 2 ] && break; sleep 1; done
[ "$(ls "$EL"/consent.*.shown 2>/dev/null | wc -l)" = 2 ] || { cat "$ROOT/conc.log" >&2; fail "two consent windows expected"; }
sleep 0.5
for f in "$EL"/consent.*.shown; do
    if grep -qF 'wexel\a.exe' "$f"; then touch "${f%.shown}.approve"; else touch "${f%.shown}.cancel"; fi
done
wait "$CSP" || { cat "$ROOT/conc.log" >&2; fail "concurrent consent"; }
grep -q '^concurrent=PASS' "$ROOT/conc.log" || { cat "$ROOT/conc.log" >&2; fail "concurrent consent: no PASS"; }
ok c5_approving_one_request_does_not_approve_the_other

# a writer while the window waits: the request fails (1392) and the window goes
el_clean
run_as "$ADM" cp "$ROOT/probes/wexelevate_probe64.exe" "/home/$ADM/wexel/cs.exe"
bprobe "$ADM" 64 run "$(winpath /home/$ADM/wexel/cs.exe)" 1392 "$(winpath $EL/out.c5)" - > "$ROOT/cs.log" 2>&1 & CSP=$!
shown="$(wait_shown)" || fail "no consent window"
lease_writer append "/home/$ADM/wexel/cs.exe" || fail "the writer"
wait "$CSP" || { cat "$ROOT/cs.log" >&2; fail "a writer during consent"; }
for _ in $(seq 1 20); do consent_alive 'system32.consent[_]test' || break; sleep 0.5; done
! consent_alive 'system32.consent[_]test' || fail "the consent window outlived the request"
[ ! -e "$EL/out.c5" ] || fail "the changed image ran"
ok c5_writer_during_consent_fails_the_request_and_closes_the_window

# the consent program cannot start: 1062, nothing runs
el_clean
mv "$SYS32/consent_test.exe" "$ROOT/consent_test.hidden"
bp c5_consent_program_missing_is_error_service_not_active "$ADM" 64 run "$(winpath /home/$ADM/wexel/p64.exe)" 1062 "$(winpath $EL/out.c5)" -
mv "$ROOT/consent_test.hidden" "$SYS32/consent_test.exe"
[ ! -e "$EL/out.c5" ] || fail "a request without a consent window started a process"
ok c5_no_consent_window_launches_nothing
el_down "$ADM"

# a standard user is still refused before any window
el_clean
el_up "$STD"
bp c5_standard_user_gets_not_supported_with_click_consent "$STD" 64 run "$(winpath /home/$STD/wexel/p64.exe)" 50 "$(winpath $EL/out.std)" -
! consent_alive 'system32.consent[_]test' && [ ! -e "$EL/out.std" ] || fail "a standard user got a window or a process"
ok c5_standard_user_gets_no_window
el_down "$STD"

# the shipping broker and the shipping consent.exe, policy 2: the real window starts as the broker's child; the caller going away ends it
el_restore_broker
set_policy 2 1 || fail "the policy value was not set"
el_up "$ADM"
el_clean
bprobe "$ADM" 64 run "$(winpath /home/$ADM/wexel/p64.exe)" 1223 "$(winpath $EL/out.ship)" - > "$ROOT/cs.log" 2>&1 & CSP=$!
for _ in $(seq 1 60); do consent_alive 'system32.consent[.]exe' && CW=1 || CW=""; [ -z "$CW" ] || break; sleep 1; done
[ -n "$CW" ] || { cat "$ROOT/cs.log" >&2; fail "the shipping broker started no consent.exe"; }
ok c5_shipping_broker_starts_the_real_consent_window
kill "$CSP" 2>/dev/null; pkill -u "$ADM" -f "wexelevate_probe64.exe run" 2>/dev/null || true
for _ in $(seq 1 30); do consent_alive 'system32.consent[.]exe' || break; sleep 1; done
! consent_alive 'system32.consent[.]exe' || fail "the consent window outlived the caller"
[ ! -e "$EL/out.ship" ] || fail "a process ran without Continue"
ok c5_caller_exit_destroys_the_request_and_closes_the_window
el_down "$ADM"
# ---- 10. the secure desktop (plan step 5): docs/ELEVATION_SECURE_DESKTOP_WORKORDER.md ---------------------------------
# The shipping broker and the real consent.exe on WinSta0\Winlogon (created by the broker, SYSTEM only). A limited process
# of the user attacks the window; xdotool on Xvfb stands in for the real keyboard and mouse (it acts as the X server's
# host, not as a Wine process); x11_grab_probe.c is a native X client. Every way out must return the input to Default.
echo "--- secure desktop (step 5)"
XP() { DISPLAY=":$DNUM" "$ROOT/bin/x11probe" "$@"; }
XD() { DISPLAY=":$DNUM" xdotool "$@"; }
real_consent() { consent_alive 'system32.consent[.]exe'; }
wait_real_consent() { local i; for i in $(seq 1 60); do real_consent && { sleep 3; return 0; }; sleep 1; done; return 1; }
wait_no_consent() { local i; for i in $(seq 1 30); do real_consent || return 0; sleep 1; done; return 1; }
consent_unix_pid() { local p; for p in $(pgrep -f 'system32.consent[.]exe'); do grep -q '^State:.*Z' "/proc/$p/status" 2>/dev/null || { echo "$p"; return; }; done; }
x11_free() { local o; o="$(XP grab)"; grep -q '^keyboard=GrabSuccess' <<<"$o" && grep -q '^pointer=GrabSuccess' <<<"$o"; }   # nothing holds the display
sd_request()   # expected-error: a request from the limited admin for p64.exe, in the background (CSP)
{
    rm -f "$EL/out.sd"
    bprobe "$ADM" 64 run "$(winpath /home/$ADM/wexel/p64.exe)" "$1" "$(winpath $EL/out.sd)" - > "$ROOT/sd.log" 2>&1 & CSP=$!
    wait_real_consent || { cat "$ROOT/sd.log" >&2; fail "no consent.exe"; }
}
sd_done()      # name: the request ended with the expected code, the window is gone and the input is back
{
    wait "$CSP" || { cat "$ROOT/sd.log" >&2; fail "$1: the request"; }
    grep -q '^run=PASS' "$ROOT/sd.log" || { cat "$ROOT/sd.log" >&2; fail "$1: no PASS"; }
    wait_no_consent || fail "$1: the consent window is still there"
    wait_input_default || { echo "input now: [$(input_now)]" >&2; wine_as "$ADM" "$ROOT/probes/sdprobe64.exe" query >&2; echo "rc=$?" >&2; ps -u "$ADM" -o pid,stat,args >&2; fail "$1: the input did not return to Default"; }
    x11_free || fail "$1: the display is still grabbed"
}

set_policy 2 1 || fail "the policy value was not set"
set_val "$POLSEC" PromptOnSecureDesktop none
el_up "$ADM"
el_clean

# -- a limited user cannot take the reserved names, and SwitchDesktop needs its access right
sd_request 1223
OUT="$(sdp 64 squat "$(winpath $EL)" || true)"; grep -q '^squat=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "a limited process could create a reserved desktop"; }
ok sd_limited_process_cannot_create_reserved_desktops
pkill -u "$ADM" -f "wexelevate_probe64.exe run" 2>/dev/null || true
wait "$CSP" 2>/dev/null || true; wait_no_consent; wait_input_default || fail "the input did not return after the caller was killed"
ok sd_caller_killed_returns_to_default
OUT="$(sdp 64 switchrules "$(winpath $EL)" || true)"; grep -q '^switchrules=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "SwitchDesktop rights"; }
ok sd_switchdesktop_needs_the_access_right

# -- the attack battery from a process on Default, 64 and 32 bit, full and restricted token; then the host keyboard
for variant in "64 full" "64 limited" "32 full32"; do
    read -r arch tag <<<"$variant"
    XP paint & PAINT=$!; sleep 1     # a white screen: the snapshot that the secure desktop takes of it must come out dimmed
    sd_request 1223
    input_secure || fail "$tag: the input desktop is not the secure desktop"
    # presentation (phase 3): the display is held, covered by a full-screen override-redirect window with a dimmed snapshot
    o="$(XP grab)"
    grep -q '^keyboard=AlreadyGrabbed' <<<"$o" && grep -q '^pointer=AlreadyGrabbed' <<<"$o" || { echo "$o" >&2; fail "$tag: a native client can grab the display"; }
    XP windows | grep -q '^1024x768 override-redirect=1 mapped=1' || { XP windows >&2; fail "$tag: no full-screen overlay"; }
    read -r R G B <<<"$(XP pixel 4 4)"
    [ "$R" -ge 100 ] && [ "$R" -le 150 ] || fail "$tag: the snapshot is not dimmed (pixel $R $G $B for a white screen)"
    # not through sdp: Wine starts explorer.exe for the attacker's desktop, and it would keep the pipe of `tr` open
    wine_as "$ADM" "$ROOT/probes/sdprobe$arch.exe" scan "$(winpath $EL)" 0 "$tag" > "$ROOT/sd.$tag.out" 2>&1 & AP=$!
    el_wait_file "$EL/att.$tag.ready"
    kill -0 "$CSP" 2>/dev/null && real_consent || { cat "$EL/attacker.$tag.log" >&2; fail "$tag: the attack ended the request"; }
    [ ! -e "$EL/out.sd" ] || fail "$tag: the attack launched the program"
    input_secure || fail "$tag: the attack moved the input off the secure desktop"
    cp "$EL/attacker.$tag.log" "$ROOT/attacker.$tag.log"
    XD key Escape     # the host keyboard reaches the dialog: Cancel
    sd_done "$tag: host Escape cancels the dialog"
    kill "$PAINT" 2>/dev/null || true; wait "$PAINT" 2>/dev/null || true
    touch "$EL/att.$tag.hostdone"; wait "$AP" || true
    cp "$EL/attacker.$tag.log" "$ROOT/attacker.$tag.log"
    A="$ROOT/attacker.$tag.log"
    # nothing of the battery had an effect (phase R's table: each of these worked on Default before)
    for op in 'SendInput(kbd A)' 'SendInput(mouse move+ldown)' 'SendInput(mouse lup)'; do
        grep -F "OP $op" "$A" | grep -q 'returned 0 err=5' || { grep -F "OP $op" "$A" >&2; fail "$tag: $op"; }
    done
    for op in 'PostMessage(BM_CLICK)' 'PostMessage(WM_COMMAND)' 'SendMessageTimeout(WM_COMMAND)' 'SendMessageTimeout(BM_CLICK)' \
              'PostThreadMessage(WM_USER+2)' 'SetWindowsHookEx(GETMESSAGE,tid)' 'SetWindowsHookEx(CALLWNDPROC,tid)' \
              'SetWindowsHookEx(KEYBOARD,tid)' 'AttachThreadInput(me,target)' 'SetForegroundWindow(target)' \
              'SetWindowPos(target,TOPMOST)' 'BringWindowToTop(target)' 'OpenDesktop(Winlogon, ENUM|READ)' \
              'OpenDesktop(Winlogon, SWITCHDESKTOP)' 'SwitchDesktop(Default) back' 'OpenDesktop(Winlogon, CREATEWINDOW'; do
        grep -F "OP $op" "$A" | grep -q ' FAIL ' || { grep -F "$op" "$A" >&2; fail "$tag: $op was not refused"; }
    done
    grep -F 'OP SendMessageTimeout(WM_USER+1)' "$A" | grep -q 'FAIL' || fail "$tag: a cross-desktop SendMessage went through"
    ok "sd_attack_battery_$tag"
    rm -f "$EL/out.sd"
done
ok sd_host_keyboard_reaches_the_secure_dialog_and_the_display_is_exclusive

# -- the host pointer: a real click on Continue approves
sd_request 0
sdp 64 rect "$(winpath $EL)" 0 click >/dev/null
read -r hw tid bt L T Rr Bm < <(tr -d '\r' < "$EL/scan.click.info") || fail "no scan.click.info"
[ -n "$Bm" ] || { cat "$EL/scan.click.info" >&2; fail "the Continue button was not found"; }
XD mousemove $(((L + Rr) / 2)) $(((T + Bm) / 2)) click 1
sd_done "the host pointer clicks Continue"
[ -e "$EL/out.sd" ] || fail "Continue did not launch the program"
ok sd_host_pointer_reaches_the_secure_dialog_and_continue_launches

# -- the other exit paths: caller exit (above), consent crash, broker crash
sd_request 1062
kill -9 "$(consent_unix_pid)"
sd_done "consent crash"
[ ! -e "$EL/out.sd" ] || fail "a crashed consent window launched the program"
ok sd_consent_crash_returns_to_default
sd_request 1223
BPID="$(pgrep -f 'system32.wexelevate[.]exe' | head -1)"
[ -n "$BPID" ] || fail "no broker process"
kill -9 "$BPID"
wait_input_default || fail "broker crash: the server did not return the input"
wait_no_consent || fail "broker crash: the consent window outlived the broker"
x11_free || fail "broker crash: the display is still grabbed"
kill "$CSP" 2>/dev/null || true; wait "$CSP" 2>/dev/null || true
ok sd_broker_crash_returns_to_default_and_ends_the_window
el_down "$ADM"

# -- the policy: absent is 5 (click consent), 0 and unknown values are refused, PromptOnSecureDesktop 0 stays on Default
set_val "$POLSEC" ConsentPromptBehaviorAdmin none
el_up "$ADM"
sd_request 1223            # no value: Windows' default 5
input_secure || fail "default policy: the prompt is not on the secure desktop"
pkill -u "$ADM" -f "wexelevate_probe64.exe run" 2>/dev/null || true
wait "$CSP" 2>/dev/null || true; wait_no_consent; wait_input_default || fail "default policy: the input did not return"
ok sd_default_policy_is_click_consent_on_the_secure_desktop
el_down "$ADM"
for v in 0 1 3 4; do
    set_val "$POLSEC" ConsentPromptBehaviorAdmin "$v"
    el_up "$ADM"
    bp "sd_policy_${v}_is_refused" "$ADM" 64 run "$(winpath /home/$ADM/wexel/p64.exe)" 21 "$(winpath $EL/out.sd)" -
    ! real_consent || fail "policy $v: a window appeared"
    el_down "$ADM"
done
set_policy 2 1 || fail "the policy value was not set"
set_val "$POLSEC" PromptOnSecureDesktop 0
el_up "$ADM"
sd_request 1223
[ "$(input_now)" = Default ] || fail "PromptOnSecureDesktop=0: the input left Default"
x11_free || fail "PromptOnSecureDesktop=0: the display is grabbed"
XD key Escape
sd_done "PromptOnSecureDesktop 0"
ok sd_prompt_on_secure_desktop_0_stays_on_default
el_down "$ADM"
set_val "$POLSEC" PromptOnSecureDesktop none

# -- the test broker: a Winlogon that existed first (1), or one that reads back wider than SYSTEM only (2), is refused (21)
install -m 775 -o root -g "$GRP" "$ROOT/probes/wexelevate_test.exe" "$SYS32/wexelevate_test.exe"
install -m 775 -o root -g "$GRP" "$ROOT/probes/wexelevate_stub.exe" "$SYS32/wexelevate_stub.exe"
install -m 775 -o root -g "$GRP" "$ROOT/probes/consent_test.exe" "$SYS32/consent_test.exe"
reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' '"ImagePath"="C:\\windows\\system32\\wexelevate_test.exe"'
BROKER_ORIG=1
set_policy 2 1 || fail "the policy value was not set"
for t in 1 2; do
    set_val 'Software\\WineExplorer\\ElevationTest' SecureDesktopTest "$t"
    el_up "$ADM"
    bp "sd_broker_refuses_a_bad_secure_desktop_$t" "$ADM" 64 run "$(winpath /home/$ADM/wexel/p64.exe)" 21 "$(winpath $EL/out.sd)" -
    [ ! -e "$EL/out.sd" ] || fail "a program ran without a secure desktop"
    ! consent_alive 'system32.consent[_]test' || fail "a prompt appeared although the secure desktop was refused"
    el_down "$ADM"
done
set_val 'Software\\WineExplorer\\ElevationTest' SecureDesktopTest none
# ---- 11. callers use the broker (plan step 7): docs/ELEVATION_CALLERS_WORKORDER.md ---------------------------------
# shell32 (runas, requireAdministrator, highestAvailable) and msi (a per-machine install by a limited administrator)
# hand the launch to the broker through wexhost.dll, with the helper set. Consent is driven through the test hook of
# consent_test.exe (Approve, Cancel); no input is injected. The test broker and the hook are installed as in section 10.
echo "--- callers use the broker (step 7)"
# programs with a manifest that asks for administrator rights, or for the highest available: the probe, twice more
for lvl in admin highest; do
    case $lvl in admin) LEVEL=requireAdministrator;; highest) LEVEL=highestAvailable;; esac
    cat > "$ROOT/$lvl.manifest" <<EOF
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
<trustInfo xmlns="urn:schemas-microsoft-com:asm.v2"><security><requestedPrivileges>
<requestedExecutionLevel level="$LEVEL" uiAccess="false"/>
</requestedPrivileges></security></trustInfo>
</assembly>
EOF
    echo "1 24 \"$ROOT/$lvl.manifest\"" > "$ROOT/$lvl.rc"
    x86_64-w64-mingw32-windres -o "$ROOT/$lvl.o64" "$ROOT/$lvl.rc"
    i686-w64-mingw32-windres -o "$ROOT/$lvl.o32" "$ROOT/$lvl.rc"
    x86_64-w64-mingw32-gcc -O2 -Wall -I"$SRC/src/include" -o "$ROOT/probes/${lvl}64.exe" "$SRC/scripts/wexelevate_probe.c" "$ROOT/$lvl.o64" -ladvapi32 -lshell32 -lmsi
    i686-w64-mingw32-gcc -O2 -Wall -I"$SRC/src/include" -o "$ROOT/probes/${lvl}32.exe" "$SRC/scripts/wexelevate_probe.c" "$ROOT/$lvl.o32" -ladvapi32 -lshell32 -lmsi
done
chmod 0755 "$ROOT/probes"/*.exe
for u in "$ADM" "$STD"; do
    for img in admin64 admin32 highest64 highest32; do run_as "$u" cp "$ROOT/probes/$img.exe" "/home/$u/wexel/$img.exe"; done
done

bprobe_f() { local out="$1" u="$2" arch="$3"; shift 3; wine_as "$u" "$ROOT/probes/wexelevate_probe$arch.exe" "$@" > "$out.raw" 2>&1; local rc=$?; tr -d '\r' < "$out.raw" > "$out"; return $rc; }   # to a file: a program the probe starts can keep explorer.exe, which inherits the descriptor, alive, and a pipe would never end
cp_case()   # name user probe-arch verb image expect action(approve|cancel|none) elevation|- [extra]
{
    local name="$1" u="$2" arch="$3" verb="$4" img="$5" expect="$6" action="$7" elev="$8" extra="${9:-}" shown
    el_clean
    bprobe_f "$ROOT/cp.log" "$u" "$arch" shexec "$verb" "$(winpath $img)" "$expect" "$(winpath $EL/out.cp)" "$elev" ${extra:+"$extra"} & CPP=$!
    if [ "$action" != none ]; then
        shown="$(wait_shown)" || { cat "$ROOT/cp.log" >&2; fail "$name: no consent window"; }
        grep -qF "Program location: $(winpath $img)" "$shown" || { cat "$shown" >&2; fail "$name: the window does not show the image"; }
        grep -qx 'desktop=Winlogon' "$shown" || { cat "$shown" >&2; fail "$name: the window is not on the secure desktop"; }
        touch "${shown%.shown}.$action"
    fi
    wait "$CPP" || { cat "$ROOT/cp.log" >&2; fail "$name"; }
    grep -q '^shexec=PASS' "$ROOT/cp.log" || { cat "$ROOT/cp.log" >&2; fail "$name: no PASS"; }
    if [ "$action" = none ]; then
        [ -z "$(ls "$EL"/consent.*.shown 2>/dev/null)" ] || fail "$name: a consent window appeared"
    fi
    ok "$name"
}

# -- the shipping broker with a policy that selects no prompt (0): 21 for ShellExecuteEx, nothing runs
el_restore_broker
set_policy 0 1 || fail "policy 0"
el_up "$ADM"
cp_case c7_policy_without_prompt_is_error_not_ready "$ADM" 64 runas "/home/$ADM/wexel/p64.exe" 21 none -
[ ! -e "$EL/out.cp" ] || fail "a program ran with the no-prompt policy"
el_down "$ADM"
set_policy none 0 || fail "policy none"
if [ -z "$BROKER_ORIG" ]; then
    install -m 775 -o root -g "$GRP" "$ROOT/probes/wexelevate_test.exe" "$SYS32/wexelevate_test.exe"
    install -m 775 -o root -g "$GRP" "$ROOT/probes/wexelevate_stub.exe" "$SYS32/wexelevate_stub.exe"
    install -m 775 -o root -g "$GRP" "$ROOT/probes/consent_test.exe" "$SYS32/consent_test.exe"
    reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' '"ImagePath"="C:\\windows\\system32\\wexelevate_test.exe"'
    BROKER_ORIG=1
fi
set_policy 2 1 || fail "the policy value was not set"

cp_create()   # name user image expected-error: CreateProcess alone (E3, no ShellExecute)
{
    local name="$1" u="$2" img="$3" expect="$4"
    el_clean
    bprobe_f "$ROOT/e3.log" "$u" 64 createproc "$(winpath /home/$u/wexel/$img.exe)" "$expect" "$(winpath $EL/out.e3)" || { cat "$ROOT/e3.log" >&2; fail "$name"; }
    grep -q '^createproc=PASS' "$ROOT/e3.log" || { cat "$ROOT/e3.log" >&2; fail "$name: no PASS"; }
    [ -z "$(ls "$EL"/consent.*.shown 2>/dev/null)" ] || fail "$name: CreateProcess asked for consent"
    ok "$name"
}
el_up "$ADM"
# -- a filtered administrator: elevates after Approve, 32 and 64 bit callers and targets
for pa in 64 32; do
    for ta in 64 32; do
        cp_case "c7_runas_${pa}bit_caller_${ta}bit_target_elevates" "$ADM" $pa runas "/home/$ADM/wexel/p$ta.exe" 0 approve 2
        [ "$(el_fact "$EL/out.cp" integrity)" = 12288 ] && [ "$(el_fact "$EL/out.cp" program_files)" = writable ] || fail "runas: the process is not elevated"
    done
done
for ta in 64 32; do
    cp_case "c7_require_administrator_${ta}bit_target_elevates_after_approve" "$ADM" 64 - "/home/$ADM/wexel/admin$ta.exe" 0 approve 2
    cp_case "c7_highest_available_${ta}bit_target_elevates_for_the_filtered_administrator" "$ADM" 64 - "/home/$ADM/wexel/highest$ta.exe" 0 approve 2
done
cp_case c7_require_administrator_from_a_32bit_caller "$ADM" 32 - "/home/$ADM/wexel/admin32.exe" 0 approve 2
# -- cancel: 1223, hInstApp SE_ERR_ACCESSDENIED, no handle, no process
for v in "runas p64" "- admin64"; do
    cp_case "c7_cancel_${v// /_}_is_error_cancelled_and_starts_nothing" "$ADM" 64 "${v% *}" "/home/$ADM/wexel/${v#* }.exe" 1223 cancel -
    [ ! -e "$EL/out.cp" ] || fail "a cancelled ShellExecuteEx started a process"
done
# -- E3: CreateProcess alone never elevates: a manifest that needs it fails with 740 for the filtered administrator
for img in admin64 admin32 highest64 highest32; do
    cp_create "c7_create_process_${img}_needs_elevation_740_for_the_filtered_administrator" "$ADM" $img 740
done
# -- a missing program: no prompt, the file error
cp_case c7_missing_program_is_file_not_found_without_a_prompt "$ADM" 64 runas "/home/$ADM/wexel/nothere.exe" 2 none -
# -- an elevated process starts runas and requireAdministrator programs directly, no second prompt
cp_case c7_elevated_process_runs_runas_and_manifest_programs_without_a_prompt "$ADM" 64 runas "/home/$ADM/wexel/p64.exe" 0 approve 2 "chain $(winpath /home/$ADM/wexel/admin64.exe)"
[ "$(ls "$EL"/consent.*.shown | wc -l)" = 1 ] || fail "the elevated process asked for consent again"
grep -q '^chain_runas=0 exit=42' "$EL/out.cp" && grep -q '^chain_open=0 exit=42' "$EL/out.cp" || { cat "$EL/out.cp" >&2; fail "chain"; }
[ "$(el_fact "$EL/out.cp.chain0" elevation)" = 2 ] && [ "$(el_fact "$EL/out.cp.chain1" elevation)" = 2 ] || fail "the programs of the elevated process are not elevated"
ok c7_chain_runs_directly_with_the_elevated_token
el_down "$ADM"

# -- a standard user: 50 (over-the-shoulder elevation is a later work order); highestAvailable simply runs limited
el_up "$STD"
cp_case c7_standard_user_runas_is_not_supported "$STD" 64 runas "/home/$STD/wexel/p64.exe" 50 none -
cp_case c7_standard_user_require_administrator_is_not_supported "$STD" 64 - "/home/$STD/wexel/admin64.exe" 50 none -
cp_case c7_standard_user_highest_available_runs_limited "$STD" 64 - "/home/$STD/wexel/highest64.exe" 0 none 1
cp_create c7_create_process_require_administrator_740_for_a_standard_user "$STD" admin64 740
cp_create c7_create_process_highest_available_runs_for_a_standard_user "$STD" highest64 0
el_down "$STD"

# -- real, freely licensed installers that require administrator rights (WEX_INSTALLER_DIR: 7z-x64.exe, 7z-x86.exe,
# npp-x64.exe, npp-x86.exe; downloaded by hand, never committed). They install into the machine root's Program Files
# after Approve; another user sees the files and can run the program.
if [ -n "${WEX_INSTALLER_DIR:-}" ]; then
echo "--- installers"
el_up "$ADM"
inst_case()   # name installer expected-file-under-machine-root silent-parameters
{
    local name="$1" exe="$2" file="$3" args="$4"
    run_as "$ADM" cp "$WEX_INSTALLER_DIR/$exe" "/home/$ADM/wexel/$exe"
    cp_case "c7_installer_${name}_asks_once" "$ADM" 64 - "/home/$ADM/wexel/$exe" 0 approve - "raw:$args"
    [ "$(ls "$EL"/consent.*.shown | wc -l)" = 1 ] || fail "$name: more than one consent window"
    [ -f "$M/drive_c/$file" ] || { ls -R "$M/drive_c/Program Files" "$M/drive_c/Program Files (x86)" >&2; fail "$name: $file was not installed"; }
    run_as "$STD" test -r "$M/drive_c/$file" || fail "$name: another user cannot read $file"
    [ "$(stat -c %U "$M/drive_c/$file")" != "$STD" ] || fail "$name: the file belongs to the other user"
    ok "c7_installer_${name}_installs_into_program_files_and_is_visible_to_another_user"
}
inst_case 7zip_64bit 7z-x64.exe 'Program Files/7-Zip/7z.exe' '/S /D="C:\Program Files\7-Zip"'
inst_case 7zip_32bit 7z-x86.exe 'Program Files (x86)/7-Zip/7z.exe' '/S /D="C:\Program Files (x86)\7-Zip"'
inst_case notepadpp_64bit npp-x64.exe 'Program Files/Notepad++/notepad++.exe' '/S'
inst_case notepadpp_32bit npp-x86.exe 'Program Files (x86)/Notepad++/notepad++.exe' '/S'
el_down "$ADM"
el_up "$STD"
OUT="$(wine_as "$STD" "$M/drive_c/Program Files/7-Zip/7z.exe" 2>&1 | head -3)"
grep -q '7-Zip' <<<"$OUT" || { echo "$OUT" >&2; fail "another user cannot run the installed 7-Zip"; }
ok c7_installed_program_runs_for_another_user
el_down "$STD"
fi

# -- a per-machine MSI by a limited administrator is relaunched once through the broker (one consent) and its deferred
# custom action runs with the full token; a per-user MSI does not elevate
if command -v wixl >/dev/null && command -v msibuild >/dev/null; then
echo "--- msi"
MS="$ROOT/msi"; install -d -m 755 "$MS"
echo hello > "$MS/hello.txt"; cp "$ROOT/probes/wexelevate_probe32.exe" "$MS/act.exe"
mkmsi()   # name all-users(1|0) folder-id
{
    local name="$1" allusers="$2" dir="$3" prop=""
    [ "$allusers" = 1 ] && prop='<Property Id="ALLUSERS" Value="1"/>'
    cat > "$MS/$name.wxs" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="*" UpgradeCode="6d3a3f0e-5a49-4a41-9a3e-0c5d2b7b1a0$allusers" Name="WexCallers$name" Language="1033" Version="1.0.0" Manufacturer="WineExplorer test">
    <Package Description="$name" InstallerVersion="200" Compressed="yes"/>
    <Media Id="1" Cabinet="a.cab" EmbedCab="yes"/>
    $prop
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="$dir">
        <Directory Id="INSTALLDIR" Name="WexCallers$name">
          <Component Id="C1" Guid="6d3a3f0e-5a49-4a41-9a3e-0c5d2b7b1a1$allusers"><File Id="F1" Source="$MS/hello.txt"/></Component>
        </Directory>
      </Directory>
    </Directory>
    <Binary Id="ActBin" SourceFile="$MS/act.exe"/>
    <Feature Id="Main" Level="1"><ComponentRef Id="C1"/></Feature>
  </Product>
</Wix>
EOF
    wixl -o "$MS/$name.msi" "$MS/$name.wxs" 2>/dev/null
    # a deferred custom action without impersonation (type 2 | continue 0x40 | in-script 0x400 | no impersonation 0x800) that reports the token it runs with
    msibuild "$MS/$name.msi" -q "INSERT INTO \`CustomAction\` (\`Action\`, \`Type\`, \`Source\`, \`Target\`) VALUES ('Act', 3138, 'ActBin', 'child $(winpath $EL/out.msi.act)')" \
        -q "INSERT INTO \`InstallExecuteSequence\` (\`Action\`, \`Condition\`, \`Sequence\`) VALUES ('Act', 'NOT Installed', 4100)"
    chmod 644 "$MS/$name.msi"
}
# TEMP of the machine root points into the setup user's profile, which a limited process cannot write: the probe sets its own
run_as "$ADM" install -d "/home/$ADM/wextmp"
mkmsi machine 1 ProgramFilesFolder
mkmsi peruser 0 AppDataFolder
msi_case()   # name msi action(approve|none) expected-result
{
    local name="$1" pkg="$2" action="$3" expect="$4" shown
    el_clean
    run_as "$ADM" cp "$MS/$pkg.msi" "/home/$ADM/wexel/$pkg.msi"
    bprobe_f "$ROOT/msi.log" "$ADM" 64 msi "$(winpath /home/$ADM/wexel/$pkg.msi)" "" "$expect" "$(winpath /home/$ADM/wextmp)" & CPP=$!
    if [ "$action" != none ]; then
        shown="$(wait_shown)" || { cat "$ROOT/msi.log" >&2; fail "$name: no consent window"; }
        grep -qF 'msiexec.exe' "$shown" && grep -qF "$pkg.msi" "$shown" || { cat "$shown" >&2; fail "$name: the window does not show msiexec and the package"; }
        touch "${shown%.shown}.$action"
    fi
    wait "$CPP" || { cat "$ROOT/msi.log" >&2; fail "$name"; }
    grep -q '^msi=PASS' "$ROOT/msi.log" || { cat "$ROOT/msi.log" >&2; fail "$name: no PASS"; }
}
el_up "$ADM"
msi_case c7_msi_per_machine_install_asks_once machine approve 0
[ "$(ls "$EL"/consent.*.shown | wc -l)" = 1 ] || fail "the per-machine MSI asked more than once"
[ -f "$M/drive_c/Program Files (x86)/WexCallersmachine/hello.txt" ] || { find "$M/drive_c" -name hello.txt >&2; fail "the per-machine MSI did not install into Program Files"; }
[ "$(el_fact "$EL/out.msi.act" elevation)" = 2 ] && [ "$(el_fact "$EL/out.msi.act" program_files)" = writable ] || { cat "$EL/out.msi.act" >&2; fail "the deferred custom action did not run elevated"; }
ok c7_msi_per_machine_install_one_consent_and_the_deferred_action_is_elevated
msi_case c7_msi_per_machine_cancel_is_user_exit machine cancel 1602
ok c7_msi_per_machine_cancel_returns_error_install_userexit
# a per-user install does not elevate and asks nothing. It does not complete either while elevation is enforced: opening the
# package copies it into the machine's Installer directory, which a limited token cannot write (1627). Recorded in the work order.
rm -rf "$M/drive_c/windows/Installer"
msi_case c7_msi_per_user_install_does_not_elevate peruser none 1627
[ -z "$(ls "$EL"/consent.*.shown 2>/dev/null)" ] || fail "the per-user MSI asked for consent"
ok c7_msi_per_user_install_does_not_elevate_and_asks_nothing
el_down "$ADM"
else
echo "NOTE: wixl or msibuild missing, the MSI checks were not run"
fi

# -- the S2, S3 and S4 assertions that waited for the broker, as far as this fixture can run them: an administrator's own
# (limited) session cannot write the machine root, so the install runs elevated through the broker after Approve
# (cmd.exe /c, "runas"); another administrator does the same; a standard user runs the result and cannot write.
# S2 Test 1 and 1b, S3 Case 1 and Case 3, S4 Case 5. Not run here (they need the installed layout of the guest or
# the COW test programs): the COW break of msvcp140.dll, S3 Case 3b and Case 4.
echo "--- S2/S3/S4 assertions that waited for the broker"
id "$CAR" >/dev/null 2>&1 || { useradd -m -k "$ROOT/skel" -s /bin/bash "$CAR"; usermod -a -G sudo "$CAR"; }
for u in "$ADM" "$CAR"; do
    run_as "$u" install -d "/home/$u/wexel"
done
printf '@echo off\r\necho Shared Tool Running Successfully\r\n' > "$ROOT/tool.bat"
printf 'Installed_By_Alice=1\r\n' > "$ROOT/alice_cfg.ini"
printf 'Modified_By_Carol=2\r\n' > "$ROOT/carol_cfg.ini"
chmod 644 "$ROOT/tool.bat" "$ROOT/alice_cfg.ini" "$ROOT/carol_cfg.ini"
run_as "$ADM" cp "$ROOT/tool.bat" "$ROOT/alice_cfg.ini" "/home/$ADM/wexel/"
run_as "$CAR" cp "$ROOT/carol_cfg.ini" "/home/$CAR/wexel/"
PF="$M/drive_c/Program Files"
el_cmd()   # name user arguments-of-cmd.exe: run "cmd.exe /c <arguments>" elevated through the broker, Approve once
{
    local name="$1" u="$2" args="$3" shown
    el_clean
    bprobe_f "$ROOT/ec.log" "$u" 64 shexec runas 'C:\windows\system32\cmd.exe' 0 "$(winpath $EL/out.ec)" - "raw:/c $args" & ECP=$!
    shown="$(wait_shown)" || { cat "$ROOT/ec.log" >&2; fail "$name: no consent window"; }
    touch "${shown%.shown}.approve"
    wait "$ECP" || { cat "$ROOT/ec.log.raw" >&2; fail "$name"; }
    grep -q '^shexec=PASS' "$ROOT/ec.log" || { cat "$ROOT/ec.log" >&2; fail "$name: no PASS"; }
}
as_wine()   # user args: a Wine program of a user's own (limited) session, output to stdout
{
    local u="$1"; shift
    wine_as "$u" "$@" 2>/dev/null | tr -d '\r'
}

# S4 Case 5: the administrator's own session installs a shared tool; a standard user runs it and cannot write
el_up "$ADM"
as_wine "$ADM" cmd.exe /c 'mkdir "C:\Program Files\SharedToolDenied"' >/dev/null || true
[ ! -d "$PF/SharedToolDenied" ] || fail "the administrator's own limited session wrote Program Files"
el_cmd c7_s4_alice_installs_into_program_files "$ADM" 'mkdir "C:\Program Files\SharedTool" && copy /Y Z:\home\wexadm\wexel\tool.bat "C:\Program Files\SharedTool\tool.bat"'
[ -f "$PF/SharedTool/tool.bat" ] || { ls -la "$PF" >&2; fail "S4: the elevated session did not install SharedTool"; }
[ "$(stat -c %G "$PF/SharedTool/tool.bat")" = "$GRP" ] || [ "$(stat -c %U "$PF/SharedTool/tool.bat")" = "$ADM" ] || fail "S4: unexpected owner of tool.bat"
ok c7_s4_alice_installs_into_program_files_through_the_broker
el_down "$ADM"
el_up "$STD"
OUT="$(as_wine "$STD" cmd.exe /c 'C:\Program Files\SharedTool\tool.bat')"
grep -q 'Shared Tool Running Successfully' <<<"$OUT" || { echo "$OUT" >&2; fail "S4: Bob cannot run the shared tool"; }
as_wine "$STD" cmd.exe /c 'echo x > "C:\Program Files\SharedTool\bob.txt"' >/dev/null || true
[ ! -e "$PF/SharedTool/bob.txt" ] || fail "S4: Bob wrote the machine root"
ok c7_s4_bob_runs_the_shared_tool_and_cannot_write_the_machine_root
el_down "$STD"

# S2 Test 1 and 1b: Alice installs AliceApp, Carol (a second administrator) modifies Alice's settings
el_up "$ADM"
el_cmd c7_s2_alice_installs_aliceapp "$ADM" 'mkdir "C:\Program Files\AliceApp" && copy /Y Z:\var\lib\wexsys-c2\probes\wexelevate_probe64.exe "C:\Program Files\AliceApp\app64.exe" && copy /Y Z:\var\lib\wexsys-c2\probes\wexelevate_probe32.exe "C:\Program Files\AliceApp\app32.exe" && copy /Y Z:\home\wexadm\wexel\alice_cfg.ini "C:\Program Files\AliceApp\settings.ini"'
for f in app64.exe app32.exe settings.ini; do [ -f "$PF/AliceApp/$f" ] || fail "S2: $f was not installed"; done
tr -d '\r' < "$PF/AliceApp/settings.ini" | grep -qx 'Installed_By_Alice=1' || fail "S2: settings.ini content"
ok c7_s2_alice_installed_app64_app32_and_settings_through_the_broker
el_down "$ADM"
el_up "$CAR"
if wine_as "$CAR" cmd.exe /c 'copy /Y Z:\home\wexcar\wexel\carol_cfg.ini "C:\Program Files\AliceApp\settings.ini"' >/dev/null 2>&1; then :; fi
tr -d '\r' < "$PF/AliceApp/settings.ini" | grep -qx 'Installed_By_Alice=1' || fail "S2: Carol's own limited session changed Alice's settings"
el_cmd c7_s2_carol_modifies_alices_settings "$CAR" 'copy /Y Z:\home\wexcar\wexel\carol_cfg.ini "C:\Program Files\AliceApp\settings.ini"'
tr -d '\r' < "$PF/AliceApp/settings.ini" | grep -qx 'Modified_By_Carol=2' || { cat "$PF/AliceApp/settings.ini" >&2; fail "S2: the second administrator could not modify settings.ini"; }
ok c7_s2_second_administrator_modifies_the_installed_files_through_the_broker
el_down "$CAR"
el_up "$STD"
OUT="$(as_wine "$STD" 'C:\Program Files\AliceApp\app64.exe' facts 'Z:\tmp\wexelev\bob.facts')"
grep -q '^facts=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "S2: Bob cannot run AliceApp"; }
OUT="$(as_wine "$STD" 'C:\Program Files\AliceApp\app32.exe' facts 'Z:\tmp\wexelev\bob32.facts')"
grep -q '^facts=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "S2: Bob cannot run the 32-bit AliceApp"; }
ok c7_s2_bob_runs_both_programs_alice_installed
el_down "$STD"

# S3 Case 1 and Case 3: administrator sessions register in HKLM through the broker, the keys persist in the machine hive
# (two administrators, each with a server of their own), and a standard user sees both
CLS='HKLM\Software\Classes\CLSID\{A11CE000-0000-0000-0000-000000000001}'
UNI='HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall'
el_up "$ADM"
el_cmd c7_s3_alice_registers_com_class_and_uninstall_entry "$ADM" "reg add \"$CLS\" /ve /d \"Alice COM Server\" /f && reg add \"$UNI\\AliceApp\" /v DisplayName /d \"Alice Application 1.0\" /f"
el_down "$ADM"
el_up "$CAR"
el_cmd c7_s3_carol_registers_her_own_key "$CAR" "reg add \"$UNI\\CarolApp\" /v DisplayName /d \"Carol Application 1.0\" /f"
el_down "$CAR"
for k in 'Alice COM Server' 'Alice Application 1.0' 'Carol Application 1.0'; do
    grep -qF "$k" "$M/system.reg" || { grep -c . "$M/system.reg" >&2; fail "S3: '$k' is not in the machine hive"; }
done
ok c7_s3_both_administrators_keys_persist_in_the_machine_hive
el_up "$STD"
OUT="$(as_wine "$STD" reg query "$UNI\\AliceApp" /v DisplayName) $(as_wine "$STD" reg query "$UNI\\CarolApp" /v DisplayName) $(as_wine "$STD" reg query "$CLS")"
grep -q 'Alice Application 1.0' <<<"$OUT" && grep -q 'Carol Application 1.0' <<<"$OUT" && grep -q 'Alice COM Server' <<<"$OUT" ||
    { echo "$OUT" >&2; fail "S3: a standard user does not see the registrations"; }
as_wine "$STD" reg add "$UNI\\BobApp" /v DisplayName /d "Bob" /f >/dev/null || true
el_down "$STD"
! grep -q 'BobApp' "$M/system.reg" || fail "S3: a standard user wrote HKLM"
ok c7_s3_standard_user_sees_the_registrations_and_cannot_write_hklm

# WO-27 R8 end to end (lab only), last in the section; see r8_e2e_case
if [ "${WEX_E1S6_LAB:-0}" = 1 ] && [ "${WEX_SKIP_R8_E2E:-0}" != 1 ]; then r8_e2e_case; fi

el_restore_broker
rm -rf "$EL"
fi   # WEX_C4_SKIP

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
if [ "${WEX_C4_SKIP:-0}" != 1 ]; then
    # no launcher: the broker runs on the user's token, so even an administrator's request is refused
    echo "--- broker without the launcher: a prefix of the administrator's own"
    UPA="/home/$ADM/.wine-own"
    rm -rf "$UPA" "$EL"; install -d -m 1777 "$EL"
    own_wine_a() { run_as "$ADM" WINEPREFIX="$UPA" "$BUILD/wine" "$@" 2>&1; }
    own_server_a() { run_as "$ADM" WINEPREFIX="$UPA" "$BUILD/server/wineserver" "$@"; }
    own_wine_a wineboot.exe -i >"$ROOT/own-boot-a.log" 2>&1 || true
    own_server_a -w
    printf '\n[Software\\\\Wine\\\\Elevation] 1700000000\n#time=1d9a0000000000\n"Helper"="C:\\\\windows\\\\system32\\\\consent.exe"\n' |
        run_as "$ADM" tee -a "$UPA/system.reg" >/dev/null
    own_wine_a "$ROOT/probes/wexelevate_probe64.exe" hold >/dev/null 2>&1 & HOLD=$!
    sleep 4
    OUT="$(own_wine_a "$ROOT/probes/wexelevate_probe64.exe" run "$(winpath /home/$ADM/wexel/p64.exe)" 50 "$(winpath $EL/out.own)" - | tr -d '\r')" ||
        { echo "$OUT" >&2; fail "broker request without the launcher"; }
    grep -q '^run=PASS' <<<"$OUT" || { echo "$OUT" >&2; fail "broker request without the launcher: no PASS"; }
    [ ! -e "$EL/out.own" ] || fail "a process was started without the system role"
    ok c4_without_the_launcher_every_request_gets_not_supported
    own_server_a -k || true; own_server_a -w; wait "$HOLD" 2>/dev/null || true
    rm -rf "$UPA" "$EL"
fi
fi
standin_off

echo "=== sysrun server: $PASS checks passed ==="
