#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sourced only by the owned S10/S6 fixture. All identities/paths are its own.
E1S13_BASE_ID=

e1s13_cases()
{
    local arch=$1 mode prefix="/home/$STD/.wine" base hive profile storage_gen name pipe row loaded peer_rc recovery_hash=
    local -a sid_words=()
    base="$prefix/drive_c/users/.e1s"
    for mode in accept absent unprovisioned wrong-incarnation wrong-reference owner already-loaded replay \
                unload crash-1 recovery crash-2 recovery crash-3 recovery; do
        stop_prefixes || fail 'S13 prefix shutdown'
        # This prefix belongs to a user created by this fixture; still refuse
        # any replaced managed base. Never adopt a preexisting profile tree.
        if [ "$mode" != recovery ] && { [ -e "$base" ] || [ -L "$base" ]; }; then
            [ -n "$E1S13_BASE_ID" ] && [ ! -L "$base" ] &&
                [ "$(stat -c '%d:%i' "$base")" = "$E1S13_BASE_ID" ] || fail 'foreign/replaced S13 base'
            [ "$(realpath -e "$base")" = "$base" ] || fail 'substituted S13 ancestors'
            rm -rf -- "$base"
            E1S13_BASE_ID=
        fi
        map_fields "$ADM"
        if [ "$mode" != recovery ]; then storage_gen=1; fi
        if [ "$mode" != unprovisioned ] && [ "$mode" != recovery ]; then
            row=$(awk -v name="$ADM" '$1 == name && $4 == "active" {print $6, $7, $8, $9}' /etc/wineexplorer/accounts.map)
            read -r -a sid_words <<<"$row"
            [ "${#sid_words[@]}" = 4 ] || fail 'S13 SID words'
            # Native provisioner opens the protected offline fixture seed as
            # root and permanently drops to Bob before mutating Bob's prefix.
            "$OWNED_OUT/wex-profile-s13" "$prefix" "$ROOT/s13-seed.reg" \
                "$(id -u "$STD")" "$(id -g "$STD")" "$ADM" "$KEY" "${sid_words[@]}" "$GENERATION" \
                >"$LOG/hku-$mode-$arch.storage.log" || fail "S12 provisioning: $mode"
            storage_gen=$(awk '{for(i=1;i<=NF;i++)if($i~/^generation=/){sub(/^generation=/,"",$i);print $i}}' "$LOG/hku-$mode-$arch.storage.log")
            [[ "$storage_gen" =~ ^[1-9][0-9]*$ ]] || fail 'missing S12 storage reference'
            E1S13_BASE_ID=$(stat -c '%d:%i' "$base")
            profile="$base/$SID/$KEY-$ADM"
            hive="$profile/user.reg"
            [ -f "$hive" ] && [ ! -L "$hive" ] || fail 'missing provisioned hive'
            case "$mode" in
                absent) mv -- "$hive" "$ROOT/s13-held-hive" ;;
                owner) chown 0:"$(id -g "$STD")" "$hive" ;;
                wrong-incarnation)
                    python3 - "$profile/.complete" "$KEY" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
assert s.count('incarnation=' + sys.argv[2]) == 1
p.write_text(s.replace('incarnation=' + sys.argv[2], 'incarnation=' + 'f' * 32))
PY
                    ;;
            esac
        fi
        name="${phase}_hku_${mode}_${CASE_NO}_$arch"
        if [ "$mode" = recovery ]; then
            [ -n "$E1S13_BASE_ID" ] && [ ! -L "$base" ] &&
                [ "$(stat -c '%d:%i' "$base")" = "$E1S13_BASE_ID" ] &&
                [ "$(realpath -e "$base")" = "$base" ] || fail 'foreign recovery fixture'
            [ -f "$profile/.e1s14-state" ] && [ ! -L "$profile/.e1s14-state" ] || fail 'missing crash state marker'
            # Capture every surviving transaction byte before the next server
            # attempts to load. No repair/deletion is part of this test.
            e1s14_recovery_hash "$profile" >"$LOG/$name.recovery.before"
            [ -n "$recovery_hash" ] || fail 'recovery has no preceding crash'
            cmp "$recovery_hash" "$LOG/$name.recovery.before" || fail 'transaction changed between crash and recovery'
        fi
        case "$mode" in crash-*) sha256sum -- "$hive" >"$LOG/$name.hive.before" ;; esac
        pipe="\\\\.\\pipe\\e1s13-$$-$CASE_NO-$arch"
        service_start "$arch" broker "$pipe" "$ADM" "$KEY" "$GENERATION" "$SID" accept "$mode:$storage_gen"
        wait_line 'E1S10 READY'
        loaded=0
        case "$mode" in accept|already-loaded|replay) loaded=1 ;; esac
        run_as "$STD" timeout -k 2 "$PEER_SECONDS" "$BUILD/wine" "$OWNED_OUT/e1s10-$((96 - arch)).exe" hku-peer "$pipe" "$SID" "$KEY" "$ADM" "$loaded" \
            >"$LOG/$name.peer.log" 2>&1 &
        PEER_PID=$!
        case "$mode" in
            crash-*)
                e1s14_crash_finish "$name" "${mode#crash-}" "$profile"
                recovery_hash="$LOG/$name.recovery"
                ok "$name"
                continue
                ;;
        esac
        peer_rc=0
        wait "$PEER_PID" || peer_rc=$?; PEER_PID=
        [ "$peer_rc" = 0 ] || fail "S13 peer: $mode exit=$peer_rc"
        ! grep -q FAIL "$LOG/$name.peer.log" || fail 'Bob HKCU changed'
        tr -d '\r' < "$LOG/$name.peer.log" | grep -qx 'E1S10 PEER OK' || fail 'missing S13 peer completion'
        service_finish "$name"
        [ "$(tr -d '\r' < "$LOG/$name.broker.log" | grep -c '^E1S13 HKU CHECK OK$')" = 2 ] || fail 'missing S13 checks'
        if [ "$mode" = unload ]; then
            [ "$(tr -d '\r' < "$LOG/$name.broker.log" | grep -c '^E1S14 UNLOAD OK$')" = 2 ] || fail 'missing unload/reload checks'
            grep -F 'E1S14' "$hive" >/dev/null && grep -F '"Persisted"=dword:014a11ce' "$hive" >/dev/null || fail 'durable hive lost session writes'
            [ ! -e "$profile/.e1s14-state" ] && [ ! -e "$profile/.e1s14-save" ] || fail 'successful unload left interrupted state'
        elif [ "$mode" = recovery ]; then
            [ "$(tr -d '\r' < "$LOG/$name.broker.log" | grep -c '^E1S14 RECOVERY DENIED$')" = 2 ] || fail 'missing recovery denial checks'
            e1s14_recovery_hash "$profile" >"$LOG/$name.recovery.after"
            cmp "$LOG/$name.recovery.before" "$LOG/$name.recovery.after" || fail 'recovery denial changed transaction data'
            recovery_hash=
        fi
        if [ "$loaded" = 1 ]; then
            # Inherited-file ownership belongs to S15 (launch prep in the caller prefix).
            runuser -u "$STD" -- test -r "$hive" || fail 'accepted native-Bob access limit changed'
        fi
        if [ "$mode" = absent ]; then mv -- "$ROOT/s13-held-hive" "$hive"; fi
        ok "$name"
    done
    # S4-unmapped account is rejected even before the S13 storage request.
    run_case "$arch" unknown
}

e1s14_recovery_hash()
{
    local profile=$1 file
    for file in user.reg .e1s14-state .e1s14-save; do
        if [ -e "$profile/$file" ] || [ -L "$profile/$file" ]; then
            [ -f "$profile/$file" ] && [ ! -L "$profile/$file" ] || fail 'substituted recovery data'
            sha256sum -- "$profile/$file"
        fi
    done
}

e1s14_server_start()
{
    case "$1" in crash-[123]:*) ;; *) return 0 ;; esac
    [ -z "$CRASH_SERVER_PID" ] || fail 'previous crash server not reaped'
    # Ordinary Wine startup uses wex-sysrun, not a plain Bob wineserver:
    # sysrun_init needs real gid Bob, effective/saved gid GRP to load the
    # machine hive and pass wineboot-init's server identity check. The fixed
    # launcher CLI cannot pass -f -p. This root-only fixture reproduces its
    # wineserver credentials and build_env(..., 0, ...) for the protected build
    # checked by test_e1s10_token.sh; no launcher/identity check is relaxed.
    # -f preserves the server's exit status through timeout; -p keeps it alive
    # before the first client. Timeout is only a guard, never crash evidence.
    timeout -k 2 "$CASE_SECONDS" /usr/bin/python3 -I - "$STD" "$GRP" "$BUILD/server/wineserver" \
        >"$LOG/server-$CASE_NO.log" 2>"$CASE_DIR/wineserver.stderr.log" <<'PY' &
import grp, os, pwd, sys

def require(ok, reason):
    if not ok:
        raise SystemExit('FAIL: crash server launch: ' + reason)

require(os.getresuid() == (0, 0, 0), 'requires fixture root')
with open('/proc/self/status') as status:
    require(any(line.split() == ['NoNewPrivs:', '0'] for line in status),
            'no_new_privs is set (the ordinary launcher would refuse)')
user = pwd.getpwnam(sys.argv[1])
system_gid = grp.getgrnam(sys.argv[2]).gr_gid
server = sys.argv[3]
require(user.pw_uid != 0 and user.pw_gid != system_gid, 'invalid fixture identity')
require(user.pw_dir == '/home/' + user.pw_name, 'unexpected fixture home')
prefix = user.pw_dir + '/.wine'
require(os.stat(prefix).st_uid == user.pw_uid, 'prefix is not owned by Bob')
os.initgroups(user.pw_name, user.pw_gid)
require(system_gid not in os.getgroups(), 'Bob is a member of the system group')
os.setresgid(user.pw_gid, system_gid, system_gid)
os.setresuid(user.pw_uid, user.pw_uid, user.pw_uid)
require(os.getresuid() == (user.pw_uid,) * 3, 'uid drop failed')
require(os.getresgid() == (user.pw_gid, system_gid, system_gid), 'server groups differ')
# Match wex-sysrun's server environment, cwd, umask and closed extra fds.
# DISPLAY/WINEDEBUG/WINEDLLOVERRIDES belong to run_as clients; build_env
# with with_display=0 strips them from ordinary auto-started servers too.
env = dict(HOME=user.pw_dir, USER=user.pw_name, LOGNAME=user.pw_name,
           PATH='/usr/bin:/bin', WINEPREFIX=prefix, LANG='C')
os.chdir('/')
os.umask(0o022)
os.closerange(3, os.sysconf('SC_OPEN_MAX'))
print('E1S14 SERVER LAUNCH uid=%s gids=%s prefix=%s image=%s' %
      (os.getresuid(), os.getresgid(), prefix, server), flush=True)
os.execve(server, ['wineserver', '-f', '-p'], env)
PY
    CRASH_SERVER_PID=$!
    # Signal 0 only queries the owned prefix's lock holder. Do not let a Wine
    # client race this foreground server by auto-starting another instance.
    until run_as "$STD" timeout -k 2 5 "$BUILD/server/wineserver" -k0 >/dev/null 2>&1; do
        [ "$SECONDS" -lt "$DEADLINE" ] && kill -0 "$CRASH_SERVER_PID" 2>/dev/null || fail 'crash server startup'
        sleep .02
    done
}

e1s14_crash_finish()
{
    local name=$1 step=$2 profile=$3 server_rc=0 peer_rc=0
    # RegFlushKey saves while the peer is blocked reading the result byte;
    # this is not an unload and does not wait for the peer to end. A client
    # may remain blocked after server death, or abort its thread with status 0.
    # Require the actual foreground server's _exit status before any -k or
    # client timeout can be mistaken for crash evidence.
    wait_line "E1S14 CRASH ARMED $step"
    [ -n "$CRASH_SERVER_PID" ] || fail 'missing owned crash server'
    wait "$CRASH_SERVER_PID" || server_rc=$?; CRASH_SERVER_PID=
    printf 'E1S14 SERVER EXIT %s\n' "$server_rc" >"$LOG/$name.crash.log"
    [ "$server_rc" = 86 ] || fail "injected server did not exit 86 (rc=$server_rc)"
    if [ -n "$WATCH_PID" ]; then kill "$WATCH_PID" 2>/dev/null || true; wait "$WATCH_PID" 2>/dev/null || true; WATCH_PID=; fi
    [ ! -e "$CASE_DIR/broker-timeout" ] || fail 'watchdog intervened before crash verification'
    grep -qx "E1S14 save-crash=$step" "$CASE_DIR/wineserver.stderr.log" || fail 'server crash checkpoint absent'
    tr -d '\r' <"$BROKER_LOG" | grep -qx 'E1S10 TRANSPORT OK' || fail 'crash S6 positive control absent'
    ! grep -q FAIL "$BROKER_LOG" || fail 'crash probe failed before injection'
    [ -d "$profile" ] && [ ! -L "$profile" ] || fail 'substituted crash profile'
    [ -f "$profile/user.reg" ] && [ ! -L "$profile/user.reg" ] || fail 'crash lost original/published hive'
    [ -f "$profile/.e1s14-state" ] && [ ! -L "$profile/.e1s14-state" ] &&
        [ ! -s "$profile/.e1s14-state" ] || fail 'crash lost empty recovery marker'
    case "$step" in
        1|2) sha256sum --check --status "$LOG/$name.hive.before" || fail 'crash changed hive before rename' ;;
    esac
    case "$step" in
        1|3)
            [ ! -e "$profile/.e1s14-save" ] && [ ! -L "$profile/.e1s14-save" ] || fail 'unexpected crash temporary hive'
            if [ "$step" = 3 ]; then
                grep -F '"Persisted"=dword:014a11ce' "$profile/user.reg" >/dev/null || fail 'renamed hive lost completed data'
            fi
            ;;
        2)
            [ -f "$profile/.e1s14-save" ] && [ ! -L "$profile/.e1s14-save" ] || fail 'crash lost temporary hive'
            grep -F '"Persisted"=dword:014a11ce' "$profile/.e1s14-save" >/dev/null || fail 'crash temp lost completed data'
            ;;
        *) fail 'unknown crash checkpoint' ;;
    esac
    e1s14_recovery_hash "$profile" >"$LOG/$name.recovery"
    # These waits only reap fixture clients under their existing outer bounds.
    # In particular rc=124/137 says nothing about whether the server crashed.
    wait "$PEER_PID" || peer_rc=$?; PEER_PID=
    printf 'E1S14 PEER REAP %s\n' "$peer_rc" >>"$LOG/$name.crash.log"
    # Preserve every pre-crash peer assertion. ReadFile failure after the
    # marker is an allowed consequence of losing the server, not a test pass.
    tr -d '\r' <"$LOG/$name.peer.log" | awk '
        !seen && /^FAIL/ {bad=1}
        $0 == "E1S10 PEER MARKER SENT" {seen=1}
        END {if (bad || !seen) exit 1}
    ' || fail 'crash peer failed before sending marker'
    stop_prefixes || fail 'crash prefix shutdown'
    if [ -n "$BOOT_PID" ]; then wait "$BOOT_PID" 2>/dev/null || true; BOOT_PID=; fi
    e1s14_recovery_hash "$profile" >"$LOG/$name.recovery.reaped"
    cmp "$LOG/$name.recovery" "$LOG/$name.recovery.reaped" || fail 'crash cleanup changed transaction data'
    cp "$BROKER_LOG" "$LOG/$name.broker.log"
    cp "$CASE_DIR/wineserver.stderr.log" "$LOG/$name.server.log"
    cat "$ROOT/base.reg" >"$M/system.reg"
    HIVE_ID=$(stat -c '%d:%i' "$M/system.reg")
}
