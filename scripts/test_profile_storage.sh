#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Qwen: run as root in disposable wexbuild. Native component only; no real
# account, system role, prefix, Wine server or host configuration is changed.
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo 'FAIL: run as root' >&2; exit 1; }
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname -- "$SCRIPT_DIR")"
FIXTURE="$(mktemp -d /run/wex-profile-test-XXXXXXXX)"
ACTIVE_PID=''
cleanup()
{
    if [[ -n "$ACTIVE_PID" ]]; then
        kill -KILL "$ACTIVE_PID" 2>/dev/null || :
        wait "$ACTIVE_PID" 2>/dev/null || :
    fi
    rm -rf -- "$FIXTURE"
}
trap cleanup EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
chmod 0755 "$FIXTURE"
cc -O2 -Wall -Wextra -Werror -o "$FIXTURE/production" "$REPO/tools/wex-profile/wex-profile.c"
cc -O2 -Wall -Wextra -Werror -DWEX_E1S12_PROFILE_TEST \
    -o "$FIXTURE/lab" "$REPO/tools/wex-profile/wex-profile.c"
BOB_UID=62001
BOB_GID=62001
P="$FIXTURE/home/prefix"
SEED="$FIXTURE/default-user.reg"
NAME=alice
KEY=0123456789abcdef0123456789abcdef
SID=S-1-5-21-11-22-33-1001
RID=1001
MAP_GEN=1
mkdir -m 0755 "$FIXTURE/home"
mkdir -m 0700 "$P"
mkdir -m 0755 "$P/dosdevices" "$P/drive_c" "$P/drive_c/users"
ln -s ../drive_c "$P/dosdevices/c:"
printf '%s\n' 'Bob live hive marker' > "$P/user.reg"
printf '%s\n' 'shared machine marker' > "$P/system.reg"
printf '%s\n' 'shared default marker' > "$P/userdef.reg"
chown -R "$BOB_UID:$BOB_GID" "$P"
printf '%s\n' 'WINE REGISTRY Version 2' '' \
    '[Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced] 0' \
    '"HideFileExt"=dword:00000000' '' \
    '[Software\\Microsoft\\Windows\\CurrentVersion\\Run] 0' \
    '"unsafe"="DO-NOT-IMPORT"' > "$SEED"
chmod 0644 "$SEED"
sha256sum "$P/user.reg" "$P/system.reg" "$P/userdef.reg" "$SEED" > "$FIXTURE/original.sha"
run() { "$FIXTURE/lab" "$P" "$SEED" "$BOB_UID" "$BOB_GID" "$NAME" "$KEY" 11 22 33 "$RID" "$MAP_GEN" "$@"; }
expect()
{
    local wanted="$1" status=0
    shift
    "$@" > "$FIXTURE/result" 2>&1 || status=$?
    [[ "$status" = "$wanted" ]] || { cat "$FIXTURE/result"; fail "expected $wanted got $status"; }
    if [[ "$wanted" != 0 ]] && grep -q '^status=' "$FIXTURE/result"; then
        grep -q "^status=$wanted profile_fd=-1 hive_fd=-1 reserved_zero=1$" "$FIXTURE/result" || fail 'failure output not cleared'
    fi
}
good() { expect 0 run; grep -q 'acl_pending=1 .*reserved_zero=1' "$FIXTURE/result" || fail 'readiness/lease fields'; }
profile() { sed -n 's/^profile=//p' "$FIXTURE/result"; }
generation() { sed -n 's/.* generation=\([0-9]*\) .*/\1/p' "$FIXTURE/result"; }
start_stopped()
{
    local step="$1" n
    # Execute the binary directly so ACTIVE_PID is the process that owns the
    # flock, not a shell wrapper whose killed child could survive cleanup.
    "$FIXTURE/lab" "$P" "$SEED" "$BOB_UID" "$BOB_GID" "$NAME" "$KEY" 11 22 33 "$RID" "$MAP_GEN" "$step" \
        > "$FIXTURE/background" 2>&1 &
    ACTIVE_PID=$!
    for ((n=0; n<250; n++)); do
        if grep -q "^checkpoint=$step$" "$FIXTURE/background"; then
            [[ "$(awk '{print $3}' "/proc/$ACTIVE_PID/stat")" = T ]] && return 0
        fi
        kill -0 "$ACTIVE_PID" 2>/dev/null || { cat "$FIXTURE/background"; fail 'checkpoint process exited'; }
        sleep 0.02
    done
    fail 'checkpoint deadline'
}
kill_stopped()
{
    kill -KILL "$ACTIVE_PID"
    wait "$ACTIVE_PID" 2>/dev/null || :
    ACTIVE_PID=''
}
resume_ok()
{
    kill -CONT "$ACTIVE_PID"
    local status=0
    wait "$ACTIVE_PID" || status=$?
    ACTIVE_PID=''
    [[ "$status" = 0 ]] || { cat "$FIXTURE/background"; fail "background status $status"; }
}

# Production binary denies even the complete synthetic lab invocation.
expect 1 "$FIXTURE/production" "$P" "$SEED" "$BOB_UID" "$BOB_GID" "$NAME" "$KEY" 11 22 33 "$RID" 1 2
strings "$FIXTURE/production" > "$FIXTURE/production.strings"
if grep -q -e checkpoint= -e WEXPROFILE -e S13_PENDING "$FIXTURE/production.strings"; then fail 'lab implementation in default binary'; fi

# First provision: an actual valid private hive and skeleton, no imported Run
# value, no Alice Unix ownership, no lease, no claim of applied Windows ACLs.
good
grep -q 'reused=0' "$FIXTURE/result" || fail first
REL="$(profile)"
PROFILE="$P/$REL"
GEN="$(generation)"
[[ "$REL" = "drive_c/users/.e1s/$SID/$KEY-alice" ]] || fail layout
grep -q '"HideFileExt"=dword:00000000' "$PROFILE/user.reg" || fail 'audited default not reused'
! grep -q DO-NOT-IMPORT "$PROFILE/user.reg" || fail 'unapproved template data imported'
[[ -d "$PROFILE/AppData/Local/Temp" && -d "$PROFILE/Desktop" && -d "$PROFILE/Documents" ]] || fail skeleton
grep -Fq "$SID" "$PROFILE/.complete" || fail identity
grep -q '^acl=S13_PENDING$' "$PROFILE/.complete" || fail 'ACL gate'
while IFS= read -r path; do
    [[ "$(stat -c '%u:%g:%a' "$path")" = "$BOB_UID:$BOB_GID:700" ]] || fail "directory metadata $path"
done < <(find "$PROFILE" -type d)
while IFS= read -r path; do
    [[ "$(stat -c '%u:%g:%a:%h' "$path")" = "$BOB_UID:$BOB_GID:600:1" ]] || fail "file metadata $path"
done < <(find "$PROFILE" -type f)
sha256sum "$PROFILE/user.reg" "$PROFILE/.complete" > "$FIXTURE/complete.sha"
MAP_GEN=2
good
grep -q 'reused=1' "$FIXTURE/result" || fail idempotence
[[ "$(generation)" = "$GEN" ]] || fail 'generation changed on reuse'
sha256sum -c "$FIXTURE/complete.sha" >/dev/null || fail 'live Alice hive rewritten'

# Explicit S4 rename preserves SID/key and the initial readable label. A
# renamed/recreated identity with a NEW incarnation receives separate storage.
NAME=alice-renamed
good
[[ "$(profile)" = "$REL" ]] || fail 'authoritative rename lost storage'
KEY=1123456789abcdef0123456789abcdef
good
[[ "$(profile)" != "$REL" ]] || fail 'new incarnation reused old profile'
NAME=alice
KEY=2123456789abcdef0123456789abcdef
good
[[ "$(profile)" != "$REL" ]] || fail 'same name recreated identity reused data'
KEY=0123456789abcdef0123456789abcdef
good

# Symlink substitution at every external/managed path component. Readlink
# validation permits ONLY Wine's fixed c: -> ../drive_c link; traversal uses
# actual drive_c descriptors, never this symlink. Also test every skeleton dir.
paths=("$FIXTURE/home" "$P" "$P/dosdevices" "$P/drive_c" "$P/drive_c/users"
       "$P/drive_c/users/.e1s" "$P/drive_c/users/.e1s/$SID" "$PROFILE"
       "$P/drive_c/users/.e1s/.profile.lock" "$PROFILE/.complete" "$PROFILE/user.reg")
while IFS= read -r path; do [[ "$path" = "$PROFILE" ]] || paths+=("$path"); done < <(find "$PROFILE" -type d)
for path in "${paths[@]}"; do
    # Keep the original outside the SID directory: a second label with the
    # same incarnation is independently (and correctly) an IDENTITY denial.
    held="$FIXTURE/substitution-held"
    mv -- "$path" "$held"
    ln -s "$held" "$path"
    expect 4 run
    rm -- "$path"
    mv -- "$held" "$path"
done
rm "$P/dosdevices/c:"
ln -s "$FIXTURE" "$P/dosdevices/c:"
expect 3 run
rm "$P/dosdevices/c:"
ln -s ../drive_c "$P/dosdevices/c:"
chown -h "$BOB_UID:$BOB_GID" "$P/dosdevices/c:"
good

# Duplicate published labels for one SID/incarnation are never guessed between.
duplicate="$P/drive_c/users/.e1s/$SID/$KEY-otherlabel"
mkdir -m 0700 "$duplicate"; chown "$BOB_UID:$BOB_GID" "$duplicate"
expect 8 run
rmdir "$duplicate"
good
old_key="$KEY"
KEY=abababababababababababababababab
conflict="$P/drive_c/users/.e1s/$SID/$KEY-$NAME"
mkdir -m 0700 "$conflict"
cp "$PROFILE/.complete" "$conflict/.complete"
chmod 0600 "$conflict/.complete"
chown -R "$BOB_UID:$BOB_GID" "$conflict"
expect 8 run # valid record from a DIFFERENT incarnation at the selected path
rm "$conflict/.complete"; rmdir "$conflict"
KEY="$old_key"
good

# Wrong owner, unsafe modes, hardlinks, missing skeleton and identity records.
for path in "$PROFILE" "$PROFILE/user.reg" "$PROFILE/.complete" "$P/drive_c/users/.e1s/.profile.lock"; do
    chown 62002 "$path"
    expect 5 run
    chown "$BOB_UID:$BOB_GID" "$path"
done
chmod 0644 "$PROFILE/user.reg"; expect 6 run; chmod 0600 "$PROFILE/user.reg"
chmod 0755 "$PROFILE"; expect 6 run; chmod 0700 "$PROFILE"
ln "$PROFILE/user.reg" "$FIXTURE/hardlink"
expect 6 run
rm "$FIXTURE/hardlink"
mv "$PROFILE/Desktop" "$FIXTURE/desktop-held"
expect 12 run
mv "$FIXTURE/desktop-held" "$PROFILE/Desktop"
mv "$PROFILE/.complete" "$FIXTURE/complete-held"
expect 7 run
mv "$FIXTURE/complete-held" "$PROFILE/.complete"
cp "$PROFILE/.complete" "$FIXTURE/manifest-good"
printf '%s\n' 'WEXPROFILE 1' 'different incarnation' > "$PROFILE/.complete"
expect 8 run
cat "$FIXTURE/manifest-good" > "$PROFILE/.complete"
good
mkdir "$P/drive_c/users/alice"
expect 7 run
rmdir "$P/drive_c/users/alice"
mkdir "$P/drive_c/users/ALICE"
expect 7 run
rmdir "$P/drive_c/users/ALICE"
mkdir "$P/.e1s"
expect 7 run
rmdir "$P/.e1s"
good

# Do not remove unrelated or unsafe stale paths. Owned exact transaction names
# recover under the provision lock; a symlink matching that namespace denies.
SID_DIR="$P/drive_c/users/.e1s/$SID"
touch "$SID_DIR/.txn-not-ours"
good
[[ -f "$SID_DIR/.txn-not-ours" ]] || fail 'unrelated stale name removed'
STALE="$SID_DIR/.txn-ffffffffffffffffffffffffffffffff"
ln -s "$FIXTURE" "$STALE"
expect 4 run
rm "$STALE"
good

# Kill between each transaction step. Before rename no profile is reachable;
# after rename the WHOLE completed bundle exists. Retry validates or cleans
# only the exact stale unpublished transaction and provisions afresh.
for step in 1 2 3 4 5; do
    KEY="$(printf '%032x' "$((900+step))")"
    target="$SID_DIR/$KEY-$NAME"
    start_stopped "$step"
    if [[ "$step" -lt 5 ]]; then
        [[ ! -e "$target" ]] || fail "half profile reachable at $step"
    else
        [[ -f "$target/.complete" && -f "$target/user.reg" && -d "$target/AppData/Local/Temp" ]] || fail 'incomplete publication'
    fi
    kill_stopped
    good
    if [[ "$step" = 5 ]]; then grep -q 'reused=1' "$FIXTURE/result" || fail 'postrename recovery'; fi
    [[ -z "$(find "$SID_DIR" -maxdepth 1 -name '.txn-????????????????????????????????' -print -quit)" ]] || fail 'stale transaction not reclaimed'
done

# Same-account concurrency: held lock refuses second writer cleanly; first
# completes, then the second reuses the exact profile without rewriting it.
KEY=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
start_stopped 2
expect 9 run
resume_ok
good
grep -q 'reused=1' "$FIXTURE/result" || fail 'concurrent retry did not reuse'

# Rename never overwrites a conflicting path, even if root races the writer.
KEY=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
start_stopped 4
target="$SID_DIR/$KEY-$NAME"
mkdir "$target"
printf '%s\n' sentinel > "$target/sentinel"
kill -CONT "$ACTIVE_PID"
status=0; wait "$ACTIVE_PID" || status=$?
ACTIVE_PID=''
[[ "$status" = 8 && -f "$target/sentinel" ]] || fail 'rename overwrote collision'
rm "$target/sentinel"; rmdir "$target"
good

# Observed ancestor replacement between skeleton and publish denies. The
# original unpublished temp is cleaned through its held parent descriptor.
KEY=cccccccccccccccccccccccccccccccc
start_stopped 4
mv "$P/drive_c/users" "$P/drive_c/users.held"
ln -s users.held "$P/drive_c/users"
kill -CONT "$ACTIVE_PID"
status=0; wait "$ACTIVE_PID" || status=$?
ACTIVE_PID=''
[[ "$status" = 4 ]] || { cat "$FIXTURE/background"; fail 'ancestor race not denied'; }
rm "$P/drive_c/users"; mv "$P/drive_c/users.held" "$P/drive_c/users"
good

# Component validation includes slash, backslash, dot, traversal, DOS/product
# reserved names, control/non-ASCII bytes, bounds and malformed key/numbers.
SAVED_KEY="$KEY"
for NAME in '' . .. a.b a/b 'a\b' 'a:b' 'a b' -alice con CoM1 LPT9 Default Public root wexsetup 'alice$' $'alice\n' $'al\303\251ce'; do
    expect 2 run
done
NAME="$(printf '%064d' 0 | tr 0 a)"; expect 2 run
KEY=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee
NAME="$(printf '%063d' 0 | tr 0 a)"; good
[[ "$(profile)" = "drive_c/users/.e1s/$SID/$KEY-$NAME" ]] || fail 'maximum component layout'
NAME=alice
for KEY in '' 0 00000000000000000000000000000000 0123456789ABCDEF0123456789ABCDEF ../0123456789abcdef0123456789abcdef; do expect 2 run; done
KEY="$SAVED_KEY"
RID=1000; expect 7 run
RID=1001
MAP_GEN=0; expect 2 run
MAP_GEN=2
good

# Protected offline seed checks apply on FIRST provision only. Existing Alice
# data is never reseeded, including after default-template updates.
KEY=dddddddddddddddddddddddddddddddd
chown 62002 "$SEED"; expect 10 run; chown 0 "$SEED"
chmod 0664 "$SEED"; expect 10 run; chmod 0644 "$SEED"
cp "$SEED" "$FIXTURE/seed-good"
printf '%s\n' 'not a Wine hive' > "$SEED"; expect 10 run
cat "$FIXTURE/seed-good" > "$SEED"
good
KEY=edededededededededededededededed
printf '%s\n' '' '[Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced] 0' \
    '"HideFileExt"=dword:00000001' >> "$SEED"
expect 10 run
cat "$FIXTURE/seed-good" > "$SEED"
mv "$SEED" "$FIXTURE/seed-held"
ln -s "$FIXTURE/seed-held" "$SEED"
expect 4 run
rm "$SEED"; mv "$FIXTURE/seed-held" "$SEED"
ln "$SEED" "$FIXTURE/seed-link"
expect 10 run
rm "$FIXTURE/seed-link"
good
if command -v setfacl >/dev/null && command -v getfacl >/dev/null; then
    setfacl -m u:62002:r "$PROFILE/user.reg"
    KEY=0123456789abcdef0123456789abcdef
    expect 6 run
    setfacl -b "$PROFILE/user.reg"
    chmod 0600 "$PROFILE/user.reg"
    good
else
    echo 'UNVERIFIED: POSIX ACL injection (install acl to cover it)'
fi
if command -v setfattr >/dev/null; then
    for path in "$P/drive_c" "$PROFILE" "$PROFILE/AppData/Local/Temp" "$PROFILE/user.reg"; do
        setfattr -n user.WINEREPARSE -v fixture "$path"
        KEY=0123456789abcdef0123456789abcdef
        expect 4 run
        setfattr -x user.WINEREPARSE "$path"
    done
    good
else
    echo 'UNVERIFIED: Wine reparse xattr injection (install attr to cover it)'
fi
KEY=0123456789abcdef0123456789abcdef
good
sha256sum -c "$FIXTURE/complete.sha" >/dev/null || fail 'original profile changed'
sha256sum -c "$FIXTURE/original.sha" >/dev/null || fail 'Bob/shared/default seed modified'
echo 'OK: E1-S12 storage transaction, identity, substitution, crash and concurrency fixture'
