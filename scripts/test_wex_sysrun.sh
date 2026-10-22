#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Tests of the setgid launcher wex-sysrun (tools/wex-sysrun). Run as root: root builds a fixture under /opt (every
# directory up to / must be root-owned and not writable by others, so not /tmp), installs the launcher setgid
# for a throw-away group, and runs it as a throw-away desktop user. Needs gcc, python3, setpriv, runuser.
# Covers each refusal (unknown mode, extra argument, writable trusted file, writable directory, config not
# root-owned or writable, no_new_privs, setgid bit missing, user in the system group) and the success case
# (egid = system group, rgid = the user, not dumpable, AT_SECURE, clean environment, no inherited fds, ptrace by
# the user fails).
set -euo pipefail

[ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 2; }

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../tools/wex-sysrun" && pwd)"
FIX="/opt/wexsysrun-test-$$"
GRP="wexsrg$$"
USR="wexsru$$"
USR2="wexsrv$$"
SYSRUN="$FIX/libexec/wex-sysrun"
CONF="$FIX/etc/sysrun.conf"
OUTDIR="$FIX/out"
PASS=0

cleanup()
{
    [ -z "${FIFO_PID:-}" ] || kill "$FIFO_PID" 2>/dev/null || true
    [ -z "${SYS_PID:-}" ] || kill "$SYS_PID" 2>/dev/null || true
    exec 8>&- 2>/dev/null || true
    rm -rf "$FIX"
    userdel "$USR" 2>/dev/null || true
    userdel "$USR2" 2>/dev/null || true
    groupdel "$GRP" 2>/dev/null || true
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { echo "$1=PASS"; PASS=$((PASS + 1)); }

groupadd --system "$GRP"
useradd -M -N -s /bin/bash -d "/home/$USR" "$USR"
useradd -M -N -s /bin/bash -d "/home/$USR2" "$USR2"
USR_GID="$(id -g "$USR")"
SYS_GID="$(getent group "$GRP" | cut -d: -f3)"
USR_UID="$(id -u "$USR")"

install -d -m 0755 "$FIX" "$FIX/bin" "$FIX/lib" "$FIX/etc" "$FIX/libexec"
install -d -m 0700 "$OUTDIR"
chown "$USR" "$OUTDIR"
gcc -O2 -Wall -o "$FIX/bin/wine" "$SRC_DIR/sysrun-probe.c"
cp "$FIX/bin/wine" "$FIX/bin/wineserver"
: > "$FIX/lib/ntdll.so"
M="$FIX/machine"
for d in "$M" "$M/drive_c" "$M/drive_c/windows" "$M/drive_c/windows/system32"; do
    install -d -m 2775 -o root -g "$GRP" "$d"
done
for f in wineboot.exe services.exe; do
    install -m 0775 -o root -g "$GRP" /dev/null "$M/drive_c/windows/system32/$f"
done
cat > "$CONF" <<EOF
group=$GRP
loader=$FIX/bin/wine
ntdll=$FIX/lib/ntdll.so
wineserver=$FIX/bin/wineserver
machine_root=$M
EOF
chmod 0644 "$CONF"

# The program is built exactly as the setup script builds it, with the fixture's configuration path.
gcc -O2 -Wall -fPIE -pie -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wl,-z,relro,-z,now \
    -DSYSRUN_CONF="\"$CONF\"" -o "$FIX/wex-sysrun.build" "$SRC_DIR/wex-sysrun.c"
install -m 2755 -o root -g "$GRP" "$FIX/wex-sysrun.build" "$SYSRUN"
rm -f "$FIX/wex-sysrun.build"
[ "$(stat -c '%a %U:%G' "$SYSRUN")" = "2755 root:$GRP" ] || fail "launcher is not 2755 root:$GRP: $(stat -c '%a %U:%G' "$SYSRUN")"

# Run the launcher as the desktop user. Extra "VAR=value" words before the mode are put into its environment.
run_as()
{
    local user="$1"; shift
    runuser -u "$user" -- env "$@" 2>&1
}
sysrun() { run_as "$USR" "$@"; }

expect_refused()
{
    local name="$1" reason="$2"; shift 2
    local out rc=0
    out="$("$@")" || rc=$?
    [ "$rc" -eq 77 ] || fail "$name: expected exit 77, got $rc: $out"
    case "$out" in
        *"wex-sysrun: refused: "*"$reason"*) ok "$name" ;;
        *) fail "$name: wrong refusal, expected '$reason': $out" ;;
    esac
}

# Positive control: with a good fixture the launcher runs the loader, so every refusal below is the one tested.
OUT="$(sysrun "$SYSRUN" wineserver </dev/null)" || fail "control: launcher failed on the good fixture: $OUT"
case "$OUT" in *"READY"*) ok "sysrun_control_runs" ;; *) fail "control output: $OUT" ;; esac

# 1. Mode table and arguments.
expect_refused sysrun_unknown_mode_refused "unknown mode" sysrun "$SYSRUN" services
expect_refused sysrun_unknown_mode_path_refused "unknown mode" sysrun "$SYSRUN" /bin/sh
expect_refused sysrun_no_mode_refused "usage" sysrun "$SYSRUN"
expect_refused sysrun_extra_argument_refused "usage" sysrun "$SYSRUN" wineserver -d
expect_refused sysrun_extra_argument_wineboot_refused "usage" sysrun "$SYSRUN" wineboot-init -u

# 2. A trusted file that is not safe: owned by the user, writable by others, writable by the user's group.
for f in "$FIX/bin/wineserver" "$FIX/bin/wine" "$FIX/lib/ntdll.so" "$M/drive_c/windows/system32/wineboot.exe" \
         "$M/drive_c/windows/system32/services.exe"; do
    b="$(basename "$f")"
    orig="$(stat -c '%a %u:%g' "$f")"
    chmod o+w "$f"
    expect_refused "sysrun_file_world_writable_$b" "writable by others" sysrun "$SYSRUN" wineserver
    chmod "${orig%% *}" "$f"
    chown "$USR_UID" "$f"
    expect_refused "sysrun_file_user_owned_$b" "not owned by root" sysrun "$SYSRUN" wineserver
    chown "${orig#* }" "$f"
    chmod g+w "$f"; chgrp "$USR_GID" "$f"
    expect_refused "sysrun_file_user_group_writable_$b" "writable by the invoking user" sysrun "$SYSRUN" wineserver
    chown "${orig#* }" "$f"; chmod "${orig%% *}" "$f"
done
OUT="$(sysrun "$SYSRUN" wineserver </dev/null)" || fail "control after the file tests: $OUT"

# 3. A directory on the way that is not safe, at each level that the test controls (fixture root, a machine root
#    directory, the directory of a symlink target).
for d in "$FIX" "$FIX/bin" "$M" "$M/drive_c/windows/system32"; do
    orig="$(stat -c '%a %u:%g' "$d")"
    chmod o+w "$d"
    expect_refused "sysrun_dir_world_writable_$(basename "$d")" "writable by others" sysrun "$SYSRUN" wineserver
    chmod "${orig%% *}" "$d"
    chgrp "$USR_GID" "$d"; chmod g+w "$d"
    expect_refused "sysrun_dir_user_group_writable_$(basename "$d")" "writable by the invoking user" sysrun "$SYSRUN" wineserver
    chown "${orig#* }" "$d"; chmod "${orig%% *}" "$d"
    chown "$USR_UID" "$d"
    expect_refused "sysrun_dir_user_owned_$(basename "$d")" "not owned by root" sysrun "$SYSRUN" wineserver
    chown "${orig#* }" "$d"; chmod "${orig%% *}" "$d"
done
# A symlink whose target sits in a user-writable directory is refused through its resolved path.
mkdir -p "$FIX/evil"; chown "$USR_UID" "$FIX/evil"
cp "$FIX/bin/wineserver" "$FIX/evil/ws"; chown root "$FIX/evil/ws"
ln -sf "$FIX/evil/ws" "$FIX/bin/ws-link"
sed -i "s|^wineserver=.*|wineserver=$FIX/bin/ws-link|" "$CONF"
expect_refused sysrun_symlink_into_user_dir_refused "not owned by root" sysrun "$SYSRUN" wineserver
sed -i "s|^wineserver=.*|wineserver=$FIX/bin/wineserver|" "$CONF"
rm -rf "$FIX/evil" "$FIX/bin/ws-link"
OUT="$(sysrun "$SYSRUN" wineserver </dev/null)" || fail "control after the directory tests: $OUT"

# 4. The configuration file.
chown "$USR_UID" "$CONF"
expect_refused sysrun_config_user_owned_refused "not owned by root" sysrun "$SYSRUN" wineserver
chown root:root "$CONF"
chmod 664 "$CONF"
expect_refused sysrun_config_group_writable_refused "config is not a regular root-owned file" sysrun "$SYSRUN" wineserver
chmod 666 "$CONF"
expect_refused sysrun_config_world_writable_refused "writable by others" sysrun "$SYSRUN" wineserver
chmod 644 "$CONF"
cp "$CONF" "$CONF.good"
echo "bogus=1" >> "$CONF"
expect_refused sysrun_config_unknown_key_refused "unknown config key" sysrun "$SYSRUN" wineserver
cp "$CONF.good" "$CONF"; rm -f "$CONF.good"
mv "$CONF" "$CONF.real"; ln -s "$CONF.real" "$CONF"
expect_refused sysrun_config_symlink_refused "cannot open config" sysrun "$SYSRUN" wineserver
rm -f "$CONF"; mv "$CONF.real" "$CONF"
OUT="$(sysrun "$SYSRUN" wineserver </dev/null)" || fail "control after the config tests: $OUT"

# 5. The setgid bit does not apply: no_new_privs, a copy without the bit, a user who is in the system group.
expect_refused sysrun_no_new_privs_refused "no_new_privs" run_as "$USR" setpriv --no-new-privs "$SYSRUN" wineserver
cp "$SYSRUN" "$FIX/libexec/no-setgid"; chmod 0755 "$FIX/libexec/no-setgid"
expect_refused sysrun_without_setgid_refused "effective group is not the system group" sysrun "$FIX/libexec/no-setgid" wineserver
rm -f "$FIX/libexec/no-setgid"
usermod -a -G "$GRP" "$USR"
expect_refused sysrun_user_in_system_group_refused "member of the system group" sysrun "$SYSRUN" wineserver
gpasswd -d "$USR" "$GRP" >/dev/null
OUT="$(sysrun "$SYSRUN" wineserver </dev/null)" || fail "control after the group tests: $OUT"
# Root cannot use it either (every file is writable by root).
OUT="$("$SYSRUN" wineserver 2>&1 </dev/null)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || fail "root was allowed to run the launcher"
ok sysrun_root_refused

# 6. Success: identity, dumpability, environment, descriptors.
exec 7</etc/hostname   # an inherited descriptor that must not reach the program
POISON=(WINEDLLPATH=/tmp/evil WINEDLLOVERRIDES=ntdll=n WINEDEBUG=+all WINELOADER=/tmp/evil/wine
        WINESERVER=/tmp/evil/wineserver WINESERVERSOCKET=7 WINEPREFIX=/tmp/evil-prefix WINEARCH=win32
        WINE_MACHINE_ROOT=/tmp/evil-machine LD_PRELOAD=/tmp/evil.so LD_LIBRARY_PATH=/tmp/evil
        LD_AUDIT=/tmp/evil.so HOME=/tmp/evil-home USER=evil LOGNAME=evil PATH=/tmp/evil:/usr/bin
        SHELL=/tmp/evil/sh BASH_ENV=/tmp/evil/env IFS=x LANG=de_DE.UTF-8 LC_ALL=C 'LC_TIME=bad;value'
        DISPLAY=:0 'WAYLAND_DISPLAY=wayland-0' XDG_RUNTIME_DIR=/run/user/1000 TZ=Evil/Zone)
# Control: the poison survives runuser (so the program's output below is the launcher's doing).
CONTROL="$(runuser -u "$USR" -- env "${POISON[@]}" printenv WINEDLLPATH LD_PRELOAD 2>/dev/null)"
[ "$CONTROL" = "$(printf '/tmp/evil\n/tmp/evil.so')" ] || fail "control: the poisoned variables do not reach the user's process: $CONTROL"

for mode in wineserver wineboot-init; do
    OUT="$(sysrun "${POISON[@]}" "$SYSRUN" $mode </dev/null 7</etc/hostname)" || fail "$mode failed: $OUT"
    echo "$OUT" > "$OUTDIR/$mode.txt"
    ENVS="$(grep '^ENV ' <<<"$OUT" | cut -c5-)"
    for bad in WINEDLLPATH WINEDLLOVERRIDES WINEDEBUG WINELOADER WINESERVER WINESERVERSOCKET WINEARCH WINE_MACHINE_ROOT \
               LD_PRELOAD LD_LIBRARY_PATH LD_AUDIT SHELL BASH_ENV IFS TZ; do
        ! grep -q "^$bad=" <<<"$ENVS" || fail "$mode: $bad reached the program"
    done
    grep -qx "WINEPREFIX=/home/$USR/.wine" <<<"$ENVS" || fail "$mode: WINEPREFIX is not the passwd home's prefix: $ENVS"
    grep -qx "HOME=/home/$USR" <<<"$ENVS" || fail "$mode: HOME is not the passwd home"
    grep -qx "USER=$USR" <<<"$ENVS" && grep -qx "LOGNAME=$USR" <<<"$ENVS" || fail "$mode: USER or LOGNAME wrong"
    grep -qx "PATH=/usr/bin:/bin" <<<"$ENVS" || fail "$mode: PATH is not fixed"
    grep -qx "LANG=de_DE.UTF-8" <<<"$ENVS" && grep -qx "LC_ALL=C" <<<"$ENVS" || fail "$mode: LANG or LC_ALL not passed"
    ! grep -q "^LC_TIME=" <<<"$ENVS" || fail "$mode: an LC_ value with unsafe characters was passed"
    if [ "$mode" = wineboot-init ]; then
        grep -qx "DISPLAY=:0" <<<"$ENVS" && grep -qx "WAYLAND_DISPLAY=wayland-0" <<<"$ENVS" &&
            grep -qx "XDG_RUNTIME_DIR=/run/user/1000" <<<"$ENVS" || fail "wineboot-init: display variables not passed"
    else
        ! grep -qE "^(DISPLAY|WAYLAND_DISPLAY|XDG_RUNTIME_DIR)=" <<<"$ENVS" || fail "wineserver: display variables passed"
    fi
    [ "$(grep -c '^ENV ' <<<"$OUT")" -le 12 ] || fail "$mode: unexpected extra variables: $ENVS"
    ok "sysrun_${mode}_environment_clean"
    grep -qx "UID $USR_UID $USR_UID $USR_UID" <<<"$OUT" || fail "$mode: uid changed: $(grep '^UID' <<<"$OUT")"
    grep -qx "GID $USR_GID $SYS_GID $SYS_GID" <<<"$OUT" || fail "$mode: gids are not (user, $GRP, $GRP): $(grep '^GID' <<<"$OUT")"
    grep -qx "DUMPABLE 0" <<<"$OUT" || fail "$mode: process is dumpable"
    grep -qx "AT_SECURE 1" <<<"$OUT" || fail "$mode: AT_SECURE is not set"
    grep -qx "CWD /" <<<"$OUT" || fail "$mode: cwd is not /"
    [ "$(grep '^FD ' <<<"$OUT" | sort | tr '\n' ' ')" = "FD 0 FD 1 FD 2 " ] || fail "$mode: inherited fds: $(grep '^FD ' <<<"$OUT" | tr '\n' ' ')"
    ok "sysrun_${mode}_identity_dumpable_fds"
done
grep -qx "ARG 0 wineserver" "$OUTDIR/wineserver.txt" && ! grep -q "^ARG 1" "$OUTDIR/wineserver.txt" ||
    fail "wineserver argument list is not fixed"
grep -qx "ARG 0 wine" "$OUTDIR/wineboot-init.txt" &&
    grep -qx "ARG 1 $M/drive_c/windows/system32/wineboot.exe" "$OUTDIR/wineboot-init.txt" &&
    grep -qx "ARG 2 --init" "$OUTDIR/wineboot-init.txt" && ! grep -q "^ARG 3" "$OUTDIR/wineboot-init.txt" ||
    fail "wineboot-init argument list is not fixed"
ok sysrun_fixed_argument_lists
exec 7<&-
# A user whose own prefix would be somewhere else still gets the prefix of the passwd home.
OUT="$(run_as "$USR2" "$SYSRUN" wineserver </dev/null)" && grep -qx "ENV WINEPREFIX=/home/$USR2/.wine" <<<"$OUT" ||
    fail "second user did not get the prefix of the passwd home: $OUT"
ok sysrun_prefix_from_passwd_home

# 7. ptrace and /proc/pid/mem by the user fail against the started process, and work against the user's own
#    process (control).
cat > "$FIX/ptrace_try.py" <<'EOF'
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
mkfifo "$FIX/hold"; chmod 0666 "$FIX/hold"
sleep 300 > "$FIX/hold" & FIFO_PID=$!   # keeps the fifo's write side open
runuser -u "$USR" -- "$SYSRUN" wineserver < "$FIX/hold" > "$OUTDIR/hold.txt" 2>&1 & SYS_PID=$!
for _ in $(seq 1 50); do grep -q '^READY' "$OUTDIR/hold.txt" 2>/dev/null && break; sleep 0.1; done
grep -q '^READY' "$OUTDIR/hold.txt" || fail "held process did not start: $(cat "$OUTDIR/hold.txt")"
TPID="$(sed -n 's/^PID //p' "$OUTDIR/hold.txt")"
PT="$(runuser -u "$USR" -- python3 -I "$FIX/ptrace_try.py" "$TPID" 2>&1)" || fail "ptrace helper failed: $PT"
echo "$PT"
case "$PT" in
    *"CONTROL_ATTACH 0 "*"CONTROL_MEM 0"*) ;;
    *) fail "positive control: the user cannot ptrace or read the memory of its own child (container policy?): $PT" ;;
esac
case "$PT" in
    *"TARGET_ATTACH -1 1 "*"TARGET_MEM 13"*|*"TARGET_ATTACH -1 13 "*"TARGET_MEM 13"*) ok sysrun_ptrace_and_mem_denied ;;
    *) fail "ptrace or /proc/pid/mem of the started process was not denied (EPERM=1, EACCES=13): $PT" ;;
esac
kill "$FIFO_PID" 2>/dev/null || true; FIFO_PID=""
wait "$SYS_PID" 2>/dev/null || true; SYS_PID=""

echo "=== wex-sysrun: $PASS checks passed ==="
