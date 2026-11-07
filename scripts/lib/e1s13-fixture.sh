#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sourced only by the owned S10/S6 fixture. All identities/paths are its own.
E1S13_BASE_ID=

e1s13_cases()
{
    local arch=$1 mode prefix="/home/$STD/.wine" base hive profile storage_gen name pipe row loaded
    local -a sid_words=()
    base="$prefix/drive_c/users/.e1s"
    for mode in accept absent unprovisioned wrong-incarnation wrong-reference owner already-loaded replay; do
        stop_prefixes || fail 'S13 prefix shutdown'
        # This prefix belongs to a user created by this fixture; still refuse
        # any replaced managed base. Never adopt a preexisting profile tree.
        if [ -e "$base" ] || [ -L "$base" ]; then
            [ -n "$E1S13_BASE_ID" ] && [ ! -L "$base" ] &&
                [ "$(stat -c '%d:%i' "$base")" = "$E1S13_BASE_ID" ] || fail 'foreign/replaced S13 base'
            [ "$(realpath -e "$base")" = "$base" ] || fail 'substituted S13 ancestors'
            rm -rf -- "$base"
            E1S13_BASE_ID=
        fi
        map_fields "$ADM"
        storage_gen=1
        if [ "$mode" != unprovisioned ]; then
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
        name="${phase}_hku_${mode}_$arch"
        pipe="\\\\.\\pipe\\e1s13-$$-$CASE_NO-$arch"
        service_start "$arch" broker "$pipe" "$ADM" "$KEY" "$GENERATION" "$SID" accept "$mode:$storage_gen"
        wait_line 'E1S10 READY'
        loaded=0
        case "$mode" in accept|already-loaded|replay) loaded=1 ;; esac
        run_as "$STD" timeout -k 2 20 "$BUILD/wine" "$OWNED_OUT/e1s10-$((96 - arch)).exe" hku-peer "$pipe" "$SID" "$KEY" "$ADM" "$loaded" \
            >"$LOG/$name.peer.log" 2>&1 &
        PEER_PID=$!
        wait "$PEER_PID" || fail "S13 peer: $mode"; PEER_PID=
        ! grep -q FAIL "$LOG/$name.peer.log" || fail 'Bob HKCU changed'
        service_finish "$name"
        [ "$(tr -d '\r' < "$LOG/$name.broker.log" | grep -c '^E1S13 HKU CHECK OK$')" = 2 ] || fail 'missing S13 checks'
        if [ "$loaded" = 1 ]; then
            [ "$(stat -c '%u:%a' "$profile/AppData/Local/Temp/s13-inherit.txt")" = "$(id -u "$STD"):600" ] || fail 'inherited file Unix metadata'
            runuser -u "$STD" -- test -r "$hive" || fail 'accepted native-Bob access limit changed'
        fi
        if [ "$mode" = absent ]; then mv -- "$ROOT/s13-held-hive" "$hive"; fi
        ok "$name"
    done
    # S4-unmapped account is rejected even before the S13 storage request.
    run_case "$arch" unknown
}
