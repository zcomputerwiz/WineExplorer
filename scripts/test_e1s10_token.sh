#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root in CLEAN wexbuild / an exclusively owned throwaway VM. NEVER VM B.
# Build/assemble two root-protected WoW64 trees first; see E1_S11_TOKEN_TESTS.md.
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Reuse the actual machine and S6 native-service fixtures, including S6 locking
# and inode-checked cleanup. Neither fixture implementation is copied here.
source "$SRC/scripts/lib/sysrun-fixture.sh"
source "$SRC/scripts/lib/e1s6-fixture.sh"
S13_HARNESS=${WEX_E1S13_HARNESS:-0}
[[ "$S13_HARNESS" = 0 || "$S13_HARNESS" = 1 ]] || exit 2
if [ "$S13_HARNESS" = 1 ]; then source "$SRC/scripts/lib/e1s13-fixture.sh"; fi

fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { echo "$1=PASS"; }
finish()
{
    local rc=$?
    trap - EXIT
    local label=E1S10_TOKEN
    [ "$S13_HARNESS" = 0 ] || label=E1S13_HKU_LOAD
    if [ "$rc" = 0 ]; then echo "$label OK"; else echo "$label FAIL" >&2; fi
    exit "$rc"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
[ "$(id -u)" = 0 ] || fail 'requires root in clean wexbuild or an owned throwaway VM'
[ $# = 0 ] || fail 'configure WINE_BUILD_DIR and WINE_NORMAL_BUILD_DIR; no positional arguments'
[ -n "${WINE_BUILD_DIR:-}" ] && [ -n "${WINE_NORMAL_BUILD_DIR:-}" ] || fail 'both build directories are required'
LAB_BUILD=$(realpath -e "$WINE_BUILD_DIR")
NORMAL_BUILD=$(realpath -e "$WINE_NORMAL_BUILD_DIR")
[ "$LAB_BUILD" != "$NORMAL_BUILD" ] || fail 'normal and lab builds must be separate'
account_autosync_idle()
{
    local unit state
    [ ! -d /run/systemd/system ] || {
        command -v systemctl >/dev/null || fail 'cannot check account synchronization units'
        for unit in wex-accounts.path wex-accounts.timer wex-accounts.service; do
            state=$(systemctl show -p ActiveState --value "$unit") || fail "cannot inspect $unit"
            case "$state" in
                inactive|failed) ;;
                *) fail "foreign account synchronization unit $unit state=$state; use a clean machine" ;;
            esac
        done
    }
}
account_autosync_idle
# No inherited fixture reuse/keep/skip options. Cleanup is mandatory.
for p in /etc/wineexplorer /usr/local/libexec/wineexplorer /var/lib/wexsys-s10; do
    [ ! -e "$p" ] && [ ! -L "$p" ] || fail "foreign fixture: $p exists; use a clean machine"
done
for u in wexadm wexstd wexcar wexret; do
    ! getent passwd "$u" >/dev/null || fail "foreign user $u exists"
    ! getent group "$u" >/dev/null || fail "foreign private group $u exists"
    [ ! -e "/home/$u" ] && [ ! -L "/home/$u" ] || fail "foreign home /home/$u exists"
done
for g in wexsysc2 wexs10admin; do
    ! getent group "$g" >/dev/null || fail "foreign group $g exists"
done
exec {S10_LOCK}>/run/lock/wex-e1s10.lock
flock -n "$S10_LOCK" || fail 'another S10 harness owns the fixture'
for cmd in x86_64-w64-mingw32-gcc i686-w64-mingw32-gcc gcc make python3 Xvfb \
           runuser setfacl getfacl timeout iconv dbus-send flock; do
    command -v "$cmd" >/dev/null || fail "missing $cmd"
done

protected_path()
{
    local p
    p=$(realpath -e "$1")
    while :; do
        [ "$(stat -c %u "$p")" = 0 ] || fail "runtime path is not root-owned: $p"
        [ $(( 0$(stat -c %a "$p") & 022 )) = 0 ] || fail "runtime path is writable by others: $p"
        [ "$p" != / ] || break
        p=$(dirname "$p")
    done
}
for build in "$LAB_BUILD" "$NORMAL_BUILD"; do
    protected_path "$build"
    [ -f "$build/Makefile" ] && grep -q 'i386.*x86_64\|x86_64.*i386' "$build/Makefile" ||
        fail "not a configured WoW64 build: $build"
    for f in wine server/wineserver dlls/ntdll/ntdll.so dlls/wexhost/wexhost.so \
             dlls/ntdll/x86_64-windows/ntdll.dll dlls/ntdll/i386-windows/ntdll.dll \
             dlls/wexhost/x86_64-windows/wexhost.dll dlls/wexhost/i386-windows/wexhost.dll; do
        [ -f "$build/$f" ] || fail "missing built artifact $build/$f"
        protected_path "$build/$f"
    done
    if [ "$S13_HARNESS" = 1 ]; then
        for arch in x86_64 i386; do
            f="dlls/kernelbase/$arch-windows/kernelbase.dll"
            [ -f "$build/$f" ] || fail "missing built artifact $build/$f"
            protected_path "$build/$f"
        done
    fi
done
OWNED_OUT=$(mktemp -d /var/tmp/e1s10-results.XXXXXXXX)
chmod 0755 "$OWNED_OUT"
echo "results=$OWNED_OUT"
cd "$SRC"
# Pin the raw carrier used by the probe. This deliberately fails on drift;
# protocol regeneration/comparison itself remains Qwen's prerequisite.
python3 -c 'import pathlib,re; s=pathlib.Path("wine/include/wine/server_protocol.h").read_text(); e=s.split("enum request\n{")[1].split("};")[0]; names=[n for n in re.findall(r"REQ_(\w+)",e) if n!="NB_REQUESTS"]; assert names[324:]==["create_e1s_account_token","load_e1s_profile"]; assert "#define SERVER_PROTOCOL_VERSION 978" in s and "int pad[22];" in s'
PROBE_SOURCE=scripts/e1s10_token_probe.c
if [ "$S13_HARNESS" = 1 ]; then PROBE_SOURCE=scripts/e1s13_hku_probe.c; fi
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -Isrc/include \
    "$PROBE_SOURCE" -ladvapi32 -o "$OWNED_OUT/e1s10-64.exe"
i686-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -Isrc/include \
    "$PROBE_SOURCE" -ladvapi32 -o "$OWNED_OUT/e1s10-32.exe"
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -municode -mwindows \
    scripts/wexdev-service.c -ladvapi32 -o "$OWNED_OUT/service64.exe"
x86_64-w64-mingw32-gcc -O2 -Wall scripts/sysrun_roles_probe.c -ladvapi32 -o "$OWNED_OUT/roles64.exe"
gcc -O2 -Wall -Wextra tools/wex-accounts/wex-accounts.c -o "$OWNED_OUT/wex-accounts"
if [ "$S13_HARNESS" = 1 ]; then
    gcc -O2 -Wall -Wextra -Werror -DWEX_E1S12_PROFILE_TEST tools/wex-profile/wex-profile.c -o "$OWNED_OUT/wex-profile-s13"
fi
chmod 0755 "$OWNED_OUT"/*.exe "$OWNED_OUT/wex-accounts"
ok probes_both_abis

# Follow S6's documented lab recipe, then S10 step 2. Force the exact objects:
# a flag change alone does not cause make to rebuild an existing object.
LAB_TOKEN_FLAGS='-DWEX_E1S10_TOKEN_TEST -DWEX_E1S6_TRANSPORT_TEST'
LAB_TOKEN_OBJECTS=(server/token.o)
LAB_PROFILE_OBJECTS=() LAB_PROFILE_DLLS=()
if [ "$S13_HARNESS" = 1 ]; then
    LAB_TOKEN_FLAGS+=' -DWEX_E1S13_HKU_TEST'
    LAB_TOKEN_OBJECTS+=(server/registry.o server/e1s_profile.o server/wex_profile_storage.o server/file.o server/fd.o)
    for arch in x86_64 i386; do
        LAB_PROFILE_OBJECTS+=("dlls/ntdll/$arch-windows/reg.o" "dlls/kernelbase/$arch-windows/registry.o")
        LAB_PROFILE_DLLS+=("dlls/ntdll/$arch-windows/ntdll.dll" "dlls/kernelbase/$arch-windows/kernelbase.dll")
    done
fi
for object in "${LAB_TOKEN_OBJECTS[@]}" "${LAB_PROFILE_OBJECTS[@]}" server/sysrun.o server/named_pipe.o dlls/wexhost/e1s6.o; do rm -f -- "$LAB_BUILD/$object"; done
make -C "$LAB_BUILD" -j1 V=1 CFLAGS="-g -O2 $LAB_TOKEN_FLAGS" \
    "${LAB_TOKEN_OBJECTS[@]}" >"$OWNED_OUT/build-token.log" 2>&1 || fail 'lab token/profile objects build (see results)'
if [ "$S13_HARNESS" = 1 ]; then
    # PE objects use architecture-specific CFLAGS, independently of the server.
    # Force both ABIs so old ungated/default objects cannot silently survive.
    make -C "$LAB_BUILD" -j1 V=1 \
        x86_64_CFLAGS='-g -O2 -DWEX_E1S13_HKU_TEST' i386_CFLAGS='-g -O2 -DWEX_E1S13_HKU_TEST' \
        "${LAB_PROFILE_OBJECTS[@]}" >"$OWNED_OUT/build-profile-client.log" 2>&1 || fail 'S13 client objects build'
    for object in "${LAB_PROFILE_OBJECTS[@]}"; do
        grep -F -- "$object" "$OWNED_OUT/build-profile-client.log" >/dev/null || fail "missing S13 client compile: $object"
    done
    tr '\n' ' ' <"$OWNED_OUT/build-profile-client.log" |
        grep -F -- -DWEX_E1S13_HKU_TEST >/dev/null || fail 'missing S13 client compiler flag'
fi
make -C "$LAB_BUILD" -j1 V=1 CFLAGS="-g -O2 -DWEX_E1S6_TRANSPORT_TEST" \
    server/sysrun.o server/named_pipe.o dlls/wexhost/e1s6.o >"$OWNED_OUT/build-s6.log" 2>&1 || fail 'lab S6 objects build'
make -C "$LAB_BUILD" -j1 V=1 server/wineserver dlls/wexhost/wexhost.so "${LAB_PROFILE_DLLS[@]}" \
    >"$OWNED_OUT/build-link.log" 2>&1 || fail 'lab relink'
# make V=1 prints each compile command over several continuation lines: join the log into one line first
tr '\n' ' ' <"$OWNED_OUT/build-token.log" |
    grep -E '(gcc|clang|cc) .* -c .*-DWEX_E1S10_TOKEN_TEST .*-DWEX_E1S6_TRANSPORT_TEST' >/dev/null || fail 'missing token compiler defines'
for object in sysrun named_pipe e1s6; do
    tr '\n' ' ' <"$OWNED_OUT/build-s6.log" |
        grep -E "(gcc|clang|cc) .* -c .*$object\\.c .*-DWEX_E1S6_TRANSPORT_TEST" >/dev/null || fail "missing $object compiler define"
done
has_str() { LC_ALL=C grep -aFq -- "$2" "$1"; }
if [ "$S13_HARNESS" = 1 ]; then
    for object in "${LAB_TOKEN_OBJECTS[@]}"; do
        tr '\n' ' ' <"$OWNED_OUT/build-token.log" |
            grep -E "(gcc|clang|cc) .* -c .*${object%.o}\\.c .*-DWEX_E1S13_HKU_TEST" >/dev/null || fail "missing S13 compiler flag: $object"
    done
    has_str "$LAB_BUILD/server/wineserver" E1S13View || fail 'S13 lab hook absent'
    for marker in E1S13View user.WEX_E1S13_SD; do
        ! has_str "$NORMAL_BUILD/server/wineserver" "$marker" || fail "normal server contains $marker"
    done
    for arch in x86_64 i386; do
        client="dlls/kernelbase/$arch-windows/kernelbase.dll"
        # The view marker is WCHAR data; a raw ASCII scan cannot find it.
        LC_ALL=C strings -e l "$LAB_BUILD/$client" >"$OWNED_OUT/client-lab-$arch.strings" || fail 'lab client strings'
        LC_ALL=C strings -e l "$NORMAL_BUILD/$client" >"$OWNED_OUT/client-normal-$arch.strings" || fail 'normal client strings'
        grep -qxF _E1S13View "$OWNED_OUT/client-lab-$arch.strings" || fail "S13 lab client hook absent: $arch"
        ! grep -qxF _E1S13View "$OWNED_OUT/client-normal-$arch.strings" || fail "normal client contains S13 hook: $arch"
    done
fi
has_str "$LAB_BUILD/server/wineserver" 'E1S token context (private)' || fail 'S10 lab token hook absent'
has_str "$LAB_BUILD/server/wineserver" wexelevate_test.exe || fail 'S6 lab image hook absent'
has_str "$LAB_BUILD/dlls/wexhost/wexhost.so" org.wineexplorer.E1S6Lab || fail 'S6 lab bus hook absent'
for marker in 'E1S token context (private)' wexelevate_test.exe E1S6_DENY; do
    ! has_str "$NORMAL_BUILD/server/wineserver" "$marker" || fail "normal server contains $marker"
done
for marker in org.wineexplorer.E1S6Lab E1S6_DENY; do
    ! has_str "$NORMAL_BUILD/dlls/wexhost/wexhost.so" "$marker" || fail "normal wexhost contains $marker"
done
ok lab_build_and_normal_exclusion

run_fixture() (
    phase=$1
    BUILD=$LAB_BUILD; [ "$phase" != normal ] || BUILD=$NORMAL_BUILD
    ROOT=/var/lib/wexsys-s10 M=/var/lib/wexsys-s10/machine GRP=wexsysc2
    ADM=wexadm STD=wexstd CAR=wexcar RET=wexret ADMIN_GROUP=wexs10admin
    LOG="$OWNED_OUT/$phase"
    CONF=/etc/wineexplorer/sysrun.conf LIBEXEC=/usr/local/libexec/wineexplorer
    SYS32="$M/drive_c/windows/system32"
    STAGED_ID= HIVE_ID= MACHINE_ID= XV_PID= BOOT_PID= PEER_PID= WATCH_PID= CASE_DIR=
    # S6's el_wait_file allows 90s, and its service-report wait allows 120s.
    # Cold Wine/SCM and the other ABI can start before main() prints anything.
    # These are outer orchestration limits; S6 and TEST_INSTALL keep their
    # original five-second authority deadlines, and the membership gate is 3s.
    CASE_SECONDS=120 PEER_SECONDS=90
    declare -a OWNED_USERS=() OWNED_GROUPS=() OWNED_DIRS=() DIR_IDS=()
    install -d -m 0755 "$LOG"
    run_as() {
        local user=$1; shift
        (cd /tmp && runuser -u "$user" -- env -i HOME="/home/$user" USER="$user" LOGNAME="$user" \
            PATH=/usr/bin:/bin LANG=C DISPLAY="$DISPLAY" WINEPREFIX="/home/$user/.wine" \
            WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' "$@")
    }
    stop_prefixes() {
        local user rc=0
        for user in "${OWNED_USERS[@]}"; do
            [ -d "/home/$user/.wine" ] || continue
            # -k fails when no server is running (the normal state before the first start): not an error
            run_as "$user" env WINEPREFIX="/home/$user/.wine" timeout -k 2 20 "$BUILD/server/wineserver" -k || true
            run_as "$user" env WINEPREFIX="/home/$user/.wine" timeout -k 2 20 "$BUILD/server/wineserver" -w || rc=1
        done
        return "$rc"
    }
    snapshot_pipes() {
        local output=$1 rc
        if [ ! -d "/home/$STD/.wine" ]; then
            echo 'Pipe namespace unavailable: owned prefix has not been created' > "$output"
            return 0
        fi
        # A server stalled in a request may not answer enumeration. Do not let
        # evidence collection delay cleanup indefinitely or use a different UID.
        if run_as "$STD" timeout -k 2 5 "$BUILD/wine" "$OWNED_OUT/e1s10-64.exe" pipes >"$output" 2>&1; then :
        else rc=$?; echo "Pipe namespace unavailable/incomplete: exit=$rc (5s bound)" >> "$output"; fi
    }
    retain_evidence() {
        local file
        install -d -m 0755 "$LOG/fixture" || return 1
        # Copy before deleting provisioning, including peers that failed before
        # service_finish and all server/service stderr inherited from bootstrap.
        for file in "$ROOT"/case-*/broker.log "$ROOT"/case-*/broker.log.done \
                    "$ROOT"/case-*/broker-timeout "$ROOT"/case-*/*.stderr.log \
                    "$ROOT"/case-*/*pipes.log "$ROOT"/case-*/command.txt \
                    "$ROOT"/case-*/command.bin "$ROOT/setup.log"; do
            [ -f "$file" ] && [ ! -L "$file" ] || continue
            cp -- "$file" "$LOG/fixture/$(basename "$(dirname "$file")")-$(basename "$file")" || return 1
        done
        for file in "$LOG"/*.peer.log "$LOG"/bootstrap-*.log "$LOG"/denied-*.log; do
            [ -f "$file" ] && [ ! -L "$file" ] || continue
            cp -- "$file" "$LOG/fixture/$(basename "$file")" || return 1
        done
    }
    cleanup_phase() {
        local rc=$? i user uid safe_clean=1 child_owned=$E1S6_CHILD_ID policy_owned=$E1S6_POLICY_ID
        trap - EXIT
        set +e
        # Disable the watchdog before the failure snapshot so it cannot tear
        # down the pipe namespace during enumeration. A timeout watchdog takes
        # its own snapshot before killing the prefix too.
        if [ -n "$WATCH_PID" ]; then kill "$WATCH_PID" 2>/dev/null; wait "$WATCH_PID" 2>/dev/null; WATCH_PID=; fi
        if [ "$rc" != 0 ] && [ -n "$CASE_DIR" ] && [ ! -L "$CASE_DIR" ] &&
           [ -d "$CASE_DIR" ] && [ "${#DIR_IDS[@]}" -gt 0 ] &&
           [ "$(stat -c '%d:%i' "$ROOT" 2>/dev/null)" = "${DIR_IDS[0]}" ]; then
            snapshot_pipes "$CASE_DIR/failure-pipes.log"
            retain_evidence || rc=1
        fi
        stop_prefixes || rc=1
        for i in "$WATCH_PID" "$PEER_PID" "$BOOT_PID"; do
            [ -z "$i" ] || { kill "$i" 2>/dev/null; wait "$i" 2>/dev/null; }
        done
        e1s6_stop
        if { [ -n "$child_owned" ] && { [ -e "$E1S6_CHILD" ] || [ -L "$E1S6_CHILD" ]; }; } ||
           { [ -n "$policy_owned" ] && { [ -e "$E1S6_POLICY" ] || [ -L "$E1S6_POLICY" ]; }; }; then
            echo 'FAIL: S6 fixture cleanup refused a replacement' >&2; rc=1
        fi
        # Copy again after shutdown to include final flushed diagnostics.
        if [ "${#DIR_IDS[@]}" -gt 0 ] && [ "$(stat -c '%d:%i' "$ROOT" 2>/dev/null)" = "${DIR_IDS[0]}" ]; then
            retain_evidence || rc=1
        fi
        [ -z "$XV_PID" ] || { kill "$XV_PID" 2>/dev/null; wait "$XV_PID" 2>/dev/null; }
        if [ -n "$MACHINE_ID" ] && { [ -L "$M" ] || [ "$(stat -c '%d:%i' "$M" 2>/dev/null)" != "$MACHINE_ID" ]; }; then
            echo 'FAIL: machine directory replaced; refusing restoration/removal' >&2
            rc=1; safe_clean=0
        fi
        if [ -n "$STAGED_ID" ] && [ "$safe_clean" = 1 ]; then
            if [ "$(stat -c '%d:%i' "$SYS32/wexelevate_test.exe" 2>/dev/null)" = "$STAGED_ID" ]; then
                rm -- "$SYS32/wexelevate_test.exe" || rc=1
            else echo 'FAIL: staged broker replaced; refusing cleanup' >&2; rc=1; safe_clean=0; fi
        fi
        if [ -n "$HIVE_ID" ] && [ "$safe_clean" = 1 ]; then
            # wineserver legitimately saves by atomic rename. The containing
            # owned machine directory and a regular non-link hive are required.
            if [ -f "$M/system.reg" ] && [ ! -L "$M/system.reg" ] &&
               [ "$(stat -c '%d:%i' "$ROOT")" = "${DIR_IDS[0]}" ]; then
                cat "$ROOT/base.reg" > "$M/system.reg" || rc=1
            else echo 'FAIL: unsafe hive; preserving fixture for inspection' >&2; rc=1; safe_clean=0; fi
        fi
        # All of these users were absent before this invocation and created
        # here. No process of any preexisting account is ever signalled.
        for user in "${OWNED_USERS[@]}"; do
            uid=$(id -u "$user") || { rc=1; continue; }
            pkill -TERM -u "$uid" 2>/dev/null
        done
        sleep 1
        for user in "${OWNED_USERS[@]}"; do
            uid=$(id -u "$user") || { rc=1; continue; }
            pkill -KILL -u "$uid" 2>/dev/null
            userdel -r "$user" || rc=1
            [ ! -d "/tmp/.wine-$uid" ] || rm -rf -- "/tmp/.wine-$uid"
        done
        for i in "${OWNED_GROUPS[@]}"; do
            ! getent group "$i" >/dev/null || groupdel "$i" || rc=1
        done
        # Refuse recursive cleanup of a replaced fixture directory. Paths are
        # fixed absolute names, and were claimed before provisioning began.
        for ((i=${#OWNED_DIRS[@]}-1; i>=0; i--)); do
            if [ "$(stat -c '%d:%i' "${OWNED_DIRS[i]}" 2>/dev/null)" = "${DIR_IDS[i]}" ]; then
                if [ "$safe_clean" = 1 ]; then rm -rf -- "${OWNED_DIRS[i]}" || rc=1; fi
            else echo "FAIL: foreign replacement ${OWNED_DIRS[i]}" >&2; rc=1; fi
        done
        if [ "$rc" != 0 ]; then echo "FAIL: $phase fixture/logs retained where cleanup failed: $LOG" >&2; fi
        exit "$rc"
    }
    trap cleanup_phase EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    # Recheck after the preceding phase; never adopt a kept/foreign fixture.
    for p in "$ROOT" /etc/wineexplorer "$LIBEXEC"; do
        [ ! -e "$p" ] && [ ! -L "$p" ] || fail "foreign $p"
        install -d -m 0755 "$p"
        OWNED_DIRS+=("$p"); DIR_IDS+=("$(stat -c '%d:%i' "$p")")
    done
    for g in "$GRP" "$ADMIN_GROUP"; do
        ! getent group "$g" >/dev/null || fail "foreign group $g"
        groupadd --system "$g"; OWNED_GROUPS+=("$g")
    done
    if [ "$phase" = lab ]; then e1s6_start "$BUILD/server/wineserver" "$GRP" "$LOG"; fi
    coproc XV { exec Xvfb -displayfd 1 -screen 0 1024x768x24 -nolisten tcp 2>"$LOG/xvfb.log"; }
    read -r -t 10 -u "${XV[0]}" DNUM || fail 'Xvfb did not start'
    DISPLAY=":$DNUM"; export DISPLAY
    export WEX_ADMIN_GROUP="$ADMIN_GROUP"
    # Use this checkout's tool for initialization as well as later syncs.
    # The normal installer otherwise enables path/timer writers before useradd.
    account_autosync_idle
    wex_fixture_machine env WEX_ACCOUNTS_AUTOSYNC=0 WEX_ACCOUNTS_BIN="$OWNED_OUT/wex-accounts" || fail "machine provisioning; see $ROOT/setup.log"
    account_autosync_idle
    MACHINE_ID=$(stat -c '%d:%i' "$M")
    if [ "$S13_HARNESS" = 1 ]; then
        printf '%s\n' 'WINE REGISTRY Version 2' '' \
            '[Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced] 0' \
            '"HideFileExt"=dword:00000000' > "$ROOT/s13-seed.reg"
        chmod 0644 "$ROOT/s13-seed.reg"
        { stat -c '%n %u:%g %a size=%s links=%h' "$ROOT" "$ROOT/s13-seed.reg"; getfacl -p "$ROOT" "$ROOT/s13-seed.reg"; getfattr -d -m - "$ROOT" "$ROOT/s13-seed.reg"; } >"$LOG/seed-state.log" 2>&1 || true
        E1S13_BASE_ID=
    fi
    [ "$(stat -c '%a %U:%G' "$LIBEXEC/wex-sysrun")" = "2755 root:$GRP" ] || fail 'launcher not setgid root:fixture-group'
    for user in "$STD" "$ADM" "$CAR" "$RET"; do
        ! getent passwd "$user" >/dev/null || fail "foreign user $user"
        useradd -m -k "$ROOT/skel" -s /bin/bash "$user"
        OWNED_USERS+=("$user")
    done
    for user in "$ADM" "$CAR" "$RET"; do usermod -a -G "$ADMIN_GROUP" "$user"; done
    { stat -c '%n %u:%g %a links=%h' / /etc /etc/wineexplorer /etc/wineexplorer/sysrun.conf /etc/wineexplorer/accounts.map /etc/wineexplorer/.accounts.lock; getfacl -p / /etc /etc/wineexplorer /etc/wineexplorer/sysrun.conf /etc/wineexplorer/accounts.map /etc/wineexplorer/.accounts.lock; getent group "$GRP"; ls -la /etc/wineexplorer; } >"$LOG/map-state-pre.log" 2>&1 || true
    "$OWNED_OUT/wex-accounts" sync >"$LOG/map-sync.log" 2>&1 || { cp -p /etc/wineexplorer/sysrun.conf "$LOG/sysrun.conf.copy" 2>/dev/null; fail "wex-accounts sync (see map-sync.log and map-state-pre.log)"; }
    map_fields() {
        local row
        GENERATION=$(awk 'NR == 1 {print $3}' /etc/wineexplorer/accounts.map)
        row=$(awk -v name="$1" '$1 == name && $4 == "active" {print $3, "S-1-5-" $5 "-" $6 "-" $7 "-" $8 "-" $9}' /etc/wineexplorer/accounts.map)
        read -r KEY SID <<<"$row"
        [[ "$KEY" =~ ^[0-9a-f]{32}$ && "$SID" == S-1-5-21-* && "$GENERATION" =~ ^[1-9][0-9]*$ ]] || fail "missing map identity $1"
    }
    map_fields "$ADM"; ALICE_SID=$SID
    map_fields "$CAR"; [ "$SID" != "$ALICE_SID" ] || fail 'Alice and Carol have the same SID'
    map_fields "$RET"; RET_KEY=$KEY RET_SID=$SID
    { stat -c '%n %u:%g %a links=%h' /etc/wineexplorer /etc/wineexplorer/accounts.map; getfacl -p /etc/wineexplorer /etc/wineexplorer/accounts.map 2>&1; getent group "$GRP"; } >"$LOG/map-state.log" 2>&1 || true
    "$OWNED_OUT/wex-accounts" retire "$RET" >"$LOG/map-retire.log"
    grep -q "^$RET .* $RET_KEY tombstone " /etc/wineexplorer/accounts.map || fail 'retire did not preserve old key'
    # Keep the tombstone but remove NSS identity so later membership syncs
    # cannot silently create a new active incarnation of the retired name.
    userdel -r "$RET"
    unset 'OWNED_USERS[3]'
    ! getent passwd wexs10absent >/dev/null || fail 'unknown account name already exists'
    ! grep -q '^wexs10absent ' /etc/wineexplorer/accounts.map || fail 'unknown map name already exists'
    reg_append "$M/system.reg" 'Software\\Wine\\Elevation' '"Helper"="C:\\windows\\system32\\consent.exe"'
    reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\wexelevate' '"Start"=dword:00000003'
    cp -p "$M/system.reg" "$ROOT/base.reg"
    HIVE_ID=$(stat -c '%d:%i' "$M/system.reg")
    [ ! -e "$SYS32/wexelevate_test.exe" ] && [ ! -L "$SYS32/wexelevate_test.exe" ] || fail 'foreign test broker image'
    winpath() { printf 'Z:%s' "${1//\//\\}"; }
    quote_arg() {
        [[ "$1" != *'"'* && "$1" != *\\ ]] || fail 'unsafe generated Windows argument'
        printf '"%s"' "$1"
    }
    CASE_NO=0
    service_start() {
        local arch=$1 cmd arg image; shift
        stop_prefixes || fail 'prefix shutdown failed'
        # A stopped Wine server may replace the owned hive during its save.
        [ -f "$M/system.reg" ] && [ ! -L "$M/system.reg" ] || fail 'unsafe fixture hive'
        cat "$ROOT/base.reg" > "$M/system.reg"
        HIVE_ID=$(stat -c '%d:%i' "$M/system.reg")
        if [ -n "$STAGED_ID" ]; then
            [ "$(stat -c '%d:%i' "$SYS32/wexelevate_test.exe")" = "$STAGED_ID" ] || fail 'staged image replaced'
            rm -- "$SYS32/wexelevate_test.exe"
        fi
        install -m 0755 -o root -g "$GRP" "$OWNED_OUT/e1s10-$arch.exe" "$SYS32/wexelevate_test.exe"
        STAGED_ID=$(stat -c '%d:%i' "$SYS32/wexelevate_test.exe")
        CASE_NO=$((CASE_NO + 1))
        CASE_DIR="$ROOT/case-$CASE_NO"
        install -d -m 0770 -o root -g "$GRP" "$CASE_DIR"
        BROKER_LOG="$CASE_DIR/broker.log"
        cmd=$(quote_arg "$(winpath "$SYS32/wexelevate_test.exe")")
        for arg in "$@"; do cmd+=" $(quote_arg "$arg")"; done
        printf '%s\n' "$cmd" > "$CASE_DIR/command.txt"
        printf '%s' "$cmd" | iconv -f UTF-8 -t UTF-16LE > "$CASE_DIR/command.bin"
        chmod 0644 "$CASE_DIR/command.bin"
        image="$(quote_arg "$(winpath "$OWNED_OUT/service64.exe")") E1S10Driver $(quote_arg "$(winpath "$BROKER_LOG")") $(quote_arg "$(winpath "$CASE_DIR/command.bin")")"
        image=${image//\\/\\\\}; image=${image//\"/\\\"}
        add_service E1S10Driver LocalSystem unused 2 "$image"
        # Wineserver and service children inherit this stderr: retain it apart
        # from probe stdout, even if the broker fails before transport startup.
        run_as "$STD" timeout -k 2 "$((CASE_SECONDS + 10))" "$BUILD/wine" "$OWNED_OUT/roles64.exe" hold \
            >"$LOG/bootstrap-$CASE_NO.log" 2>"$CASE_DIR/wineserver.stderr.log" &
        BOOT_PID=$!
        DEADLINE=$((SECONDS + CASE_SECONDS))
        # An independent outer guard bounds the broker even while the parent
        # waits for its separately bounded peer. It only stops Bob's owned
        # prefix and is reaped before another case can start in that prefix.
        (
            trap - EXIT INT TERM
            trap 'exit 0' TERM
            until [ -s "$BROKER_LOG.done" ]; do
                if [ "$SECONDS" -ge "$DEADLINE" ]; then
                    echo "FAIL: broker outer ${CASE_SECONDS}s timeout" > "$CASE_DIR/broker-timeout"
                    snapshot_pipes "$CASE_DIR/timeout-pipes.log"
                    retain_evidence || true
                    run_as "$STD" timeout -k 2 20 "$BUILD/server/wineserver" -k || true
                    exit 1
                fi
                sleep .02
            done
        ) & WATCH_PID=$!
    }
    wait_line() {
        until [ -f "$BROKER_LOG" ] && tr -d '\r' < "$BROKER_LOG" | grep -qxF "$1"; do
            [ "$SECONDS" -lt "$DEADLINE" ] && [ ! -s "$BROKER_LOG.done" ] || fail "broker readiness $1 ($BROKER_LOG)"
            sleep .02
        done
    }
    service_finish() {
        local name=$1 mode=${2:-} rc
        # The adapter creates .done before writing its exit code. Existence
        # alone can observe an empty file in that small publication window.
        until [ -s "$BROKER_LOG.done" ]; do
            [ "$SECONDS" -lt "$DEADLINE" ] || fail "${CASE_SECONDS}s broker timeout: $name"
            sleep .02
        done
        rc=$(tr -d '\r\n' < "$BROKER_LOG.done")
        wait "$WATCH_PID" || fail "broker watchdog: $name"; WATCH_PID=
        cp "$BROKER_LOG" "$LOG/$name.broker.log"
        [ "$rc" = 0 ] || fail "broker exit $rc: $name"
        tr -d '\r' < "$BROKER_LOG" | grep -qx 'E1S10 OK' || fail "missing broker completion: $name"
        if [ "$phase" = lab ]; then
            tr -d '\r' < "$BROKER_LOG" | grep -qx 'E1S10 TRANSPORT OK' || fail "missing S6 positive control: $name"
        fi
        ! grep -q FAIL "$BROKER_LOG" "$LOG/bootstrap-$CASE_NO.log" "$CASE_DIR/wineserver.stderr.log" || fail "FAIL line: $name"
        if [ -n "$mode" ]; then
            # Read only after the adapter has flushed all output and published
            # .done; the peer can exit before the broker finishes cleanup.
            tr -d '\r' < "$BROKER_LOG" | grep -qxF "E1S10 CASE $mode OK" || fail "missing case completion: $name"
            case "$mode" in
                accept|replay|thread|process)
                    [ "$(tr -d '\r' < "$BROKER_LOG" | grep -cx 'E1S10 IDENTITY OK')" = 2 ] || fail "missing child/grandchild completion: $name"
                    ;;
            esac
        fi
        stop_prefixes || fail "shutdown: $name"
        wait "$BOOT_PID" 2>/dev/null || true; BOOT_PID=
        # Remove the auto-start adapter before standalone denied clients boot
        # another prefix. Otherwise they would start an unpaired broker.
        cat "$ROOT/base.reg" > "$M/system.reg"
        HIVE_ID=$(stat -c '%d:%i' "$M/system.reg")
        ok "$name"
    }
    run_case() {
        local arch=$1 mode=$2 account=${3:-$ADM} pipe peer_mode=peer name gate
        name="${phase}_${mode}_${account}_$arch"
        map_fields "$account"
        if [ "$mode" = unknown ]; then account=wexs10absent; KEY=11111111111111111111111111111111; fi
        if [ "$mode" = tombstone ]; then account=$RET; KEY=$RET_KEY; SID=$RET_SID; fi
        pipe="\\\\.\\pipe\\e1s10-$$-$CASE_NO-$arch"
        gate="$ROOT/release-$CASE_NO"
        service_start "$arch" broker "$pipe" "$account" "$KEY" "$GENERATION" "$SID" "$mode" "$(winpath "$gate")"
        wait_line 'E1S10 READY'
        [ "$mode" != caller-death ] || peer_mode=peer-death
        run_as "$STD" timeout -k 2 "$PEER_SECONDS" "$BUILD/wine" "$OWNED_OUT/e1s10-$((96 - arch)).exe" "$peer_mode" "$pipe" \
            >"$LOG/$name.peer.log" 2>&1 &
        PEER_PID=$!
        if [ "$mode" = group-removed ] || [ "$mode" = missing-group ]; then
            wait_line 'E1S10 INSTALLED'
            if [ "$mode" = group-removed ]; then
                gpasswd -d "$ADM" "$ADMIN_GROUP" >"$LOG/$name.group.log"
                "$OWNED_OUT/wex-accounts" sync >>"$LOG/$name.group.log"
                # Membership alone must revoke issuance: sync must preserve
                # both the key and generation, isolating this from stale-map.
                old_key=$KEY old_generation=$GENERATION
                map_fields "$ADM"
                [ "$KEY:$GENERATION" = "$old_key:$old_generation" ] || fail 'membership sync changed identity/generation'
            else groupdel "$ADMIN_GROUP"; fi
            touch "$gate"
        fi
        local peer_rc=0
        wait "$PEER_PID" || peer_rc=$?; PEER_PID=
        [ "$peer_rc" = 0 ] || fail "peer exit=$peer_rc (${PEER_SECONDS}s bound): $name; broker=$BROKER_LOG peer=$LOG/$name.peer.log"
        ! grep -q FAIL "$LOG/$name.peer.log" || fail "peer FAIL line: $name"
        tr -d '\r' < "$LOG/$name.peer.log" | grep -qx 'E1S10 PEER OK' || fail "missing peer completion: $name"
        service_finish "$name" "$mode"
        if [ "$mode" = group-removed ]; then
            usermod -a -G "$ADMIN_GROUP" "$ADM"
            "$OWNED_OUT/wex-accounts" sync >>"$LOG/$name.group.log"
        elif [ "$mode" = missing-group ]; then
            groupadd --system "$ADMIN_GROUP"
            for user in "$ADM" "$CAR"; do usermod -a -G "$ADMIN_GROUP" "$user"; done
            "$OWNED_OUT/wex-accounts" sync >"$LOG/$name.group-restored.log"
        fi
    }
    for arch in 64 32; do
        if [ "$phase" = lab ] && [ "$S13_HARNESS" = 1 ]; then
            e1s13_cases "$arch"
        elif [ "$phase" = lab ]; then
            run_case "$arch" accept "$ADM"
            run_case "$arch" accept "$CAR"
            for mode in unknown tombstone stale no-cap digest evidence expired replay absent-install \
                        raw-short raw-trailing raw-oversized raw-malformed thread process caller-death group-removed missing-group; do
                run_case "$arch" "$mode"
            done
        else
            service_start "$arch" normal
            service_finish "normal_system_all_operations_$arch"
        fi
        for spec in "$STD:default" "$ADM:limited"; do
            user=${spec%:*} elevation=${spec#*:}
            deny_mode=denied; [ "$phase" != normal ] || deny_mode=normal-denied
            run_as "$user" timeout -k 2 "$PEER_SECONDS" "$BUILD/wine" "$OWNED_OUT/e1s10-$arch.exe" "$deny_mode" "$elevation" \
                >"$LOG/denied-$user-$arch.log" 2>&1 || fail "denied as $user ($arch)"
            tr -d '\r' < "$LOG/denied-$user-$arch.log" | grep -qx 'E1S10 OK' || fail 'missing denied completion'
            ! grep -q FAIL "$LOG/denied-$user-$arch.log" || fail 'denied FAIL line'
            ok "${phase}_denied_${user}_$arch"
        done
    done
)
run_fixture lab
run_fixture normal
echo 'NOT_COVERED=primary replacement/security-version mutation; S6 re-registration; caller replacement; allocation failure; LUID exhaustion; host UID measurement'
