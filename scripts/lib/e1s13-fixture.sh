#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sourced only by the owned S10/S6 fixture. All identities/paths are its own.
E1S13_BASE_ID=

e1s13_cases()
{
    local arch=$1 mode prefix="/home/$STD/.wine" base hive profile storage_gen name pipe row loaded peer_rc
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
        if [ "$mode" = recovery ]; then
            [ -n "$E1S13_BASE_ID" ] && [ ! -L "$base" ] &&
                [ "$(stat -c '%d:%i' "$base")" = "$E1S13_BASE_ID" ] &&
                [ "$(realpath -e "$base")" = "$base" ] || fail 'foreign recovery fixture'
            [ -f "$profile/.e1s14-state" ] && [ ! -L "$profile/.e1s14-state" ] || fail 'missing crash state marker'
            # Capture every surviving transaction byte before the next server
            # attempts to load. No repair/deletion is part of this test.
            e1s14_recovery_hash "$profile" >"$LOG/recovery-$arch.before"
        fi
        name="${phase}_hku_${mode}_${CASE_NO}_$arch"
        pipe="\\\\.\\pipe\\e1s13-$$-$CASE_NO-$arch"
        service_start "$arch" broker "$pipe" "$ADM" "$KEY" "$GENERATION" "$SID" accept "$mode:$storage_gen"
        wait_line 'E1S10 READY'
        loaded=0
        case "$mode" in accept|already-loaded|replay) loaded=1 ;; esac
        run_as "$STD" timeout -k 2 "$PEER_SECONDS" "$BUILD/wine" "$OWNED_OUT/e1s10-$((96 - arch)).exe" hku-peer "$pipe" "$SID" "$KEY" "$ADM" "$loaded" \
            >"$LOG/$name.peer.log" 2>&1 &
        PEER_PID=$!
        peer_rc=0
        wait "$PEER_PID" || peer_rc=$?; PEER_PID=
        case "$mode" in
            crash-*)
                e1s14_crash_finish "$name" "${mode#crash-}" "$peer_rc"
                [ -f "$profile/.e1s14-state" ] || fail 'crash lost recovery marker'
                if [ "$mode" != crash-1 ]; then
                    # After rename (step 3), the temp name is consumed and the
                    # complete new hive is retained beside the state marker.
                    if [ "$mode" = crash-2 ]; then
                        [ -f "$profile/.e1s14-save" ] && [ ! -L "$profile/.e1s14-save" ] || fail 'crash lost temporary hive'
                        grep -F '"Persisted"=dword:014a11ce' "$profile/.e1s14-save" >/dev/null || fail 'crash temp lost completed data'
                    else
                        grep -F '"Persisted"=dword:014a11ce' "$hive" >/dev/null || fail 'renamed hive lost completed data'
                    fi
                fi
                ok "$name"
                continue
                ;;
        esac
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
            e1s14_recovery_hash "$profile" >"$LOG/recovery-$arch.after"
            cmp "$LOG/recovery-$arch.before" "$LOG/recovery-$arch.after" || fail 'recovery denial changed transaction data'
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
        if [ -e "$profile/$file" ]; then
            [ -f "$profile/$file" ] && [ ! -L "$profile/$file" ] || fail 'substituted recovery data'
            sha256sum -- "$profile/$file"
        fi
    done
}

e1s14_crash_finish()
{
    local name=$1 step=$2 peer_rc=$3
    # The server's _exit kills both Wine sides before the adapter can publish
    # .done. Cancel only this fixture's watchdog and reap its bootstrap; never
    # wait for ordinary success or mistake a peer timeout for an injected crash.
    tr -d '\r' <"$BROKER_LOG" | grep -qx "E1S14 CRASH ARMED $step" || fail 'crash positive control absent'
    grep -qx "E1S14 save-crash=$step" "$CASE_DIR/wineserver.stderr.log" || fail 'server crash checkpoint absent'
    [ "$peer_rc" != 0 ] && [ "$peer_rc" != 124 ] && [ "$peer_rc" != 137 ] || fail "peer did not observe prompt server death (rc=$peer_rc)"
    ! grep -q FAIL "$BROKER_LOG" || fail 'crash probe failed before injection'
    if [ -n "$WATCH_PID" ]; then kill "$WATCH_PID" 2>/dev/null || true; wait "$WATCH_PID" 2>/dev/null || true; WATCH_PID=; fi
    stop_prefixes || fail 'crash prefix shutdown'
    if [ -n "$BOOT_PID" ]; then wait "$BOOT_PID" 2>/dev/null || true; BOOT_PID=; fi
    cp "$BROKER_LOG" "$LOG/$name.broker.log"
    cp "$CASE_DIR/wineserver.stderr.log" "$LOG/$name.server.log"
    cat "$ROOT/base.reg" >"$M/system.reg"
    HIVE_ID=$(stat -c '%d:%i' "$M/system.reg")
}
