#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root-only disposable E1-S4 fixture. No host accounts, config or Wine changed.
# Builds native lab binaries only. Run in Qwen's disposable Linux guest.
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo 'FAIL: run as root' >&2; exit 1; }
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname -- "$SCRIPT_DIR")"
# /tmp is intentionally forbidden by the provider's protected-ancestor check.
FIXTURE="$(mktemp -d /run/wex-accounts-test-XXXXXXXX)"
trap 'rm -rf -- "$FIXTURE"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -m 0755 "$FIXTURE/config"
CONF="$FIXTURE/config"
MAP="$CONF/accounts.map"
PASSWD="$FIXTURE/passwd"
GROUP="$FIXTURE/group"
printf 'group=wexsys\nadmin_group=sudo\n' > "$CONF/sysrun.conf"
chmod 0644 "$CONF/sysrun.conf"
printf 'wexsys:x:19999:\nsudo:x:2000:alice,carol\nusers:x:2001:\nwheel:x:2002:bob\n' > "$GROUP"
printf 'bob:x:1001:2001::/home/bob:/bin/sh\nalice:x:1002:2001::/home/alice:/bin/sh\nprimary:x:1003:2000::/home/primary:/bin/sh\nservice:x:1005:2001::/nonexistent:/usr/sbin/nologin\n' > "$PASSWD"

cc -O2 -Wall -Wextra -Werror -DWEX_ACCOUNTS_TEST \
    -o "$FIXTURE/lab" "$REPO/tools/wex-accounts/wex-accounts.c"
cc -O2 -Wall -Wextra -Werror \
    -o "$FIXTURE/production" "$REPO/tools/wex-accounts/wex-accounts.c"
TOOL=("$FIXTURE/lab" --directory "$CONF" --test-passwd "$PASSWD" --test-group "$GROUP")
sync_map() { "${TOOL[@]}" sync "$@" > "$FIXTURE/sync.log" || { cat "$FIXTURE/sync.log"; fail sync; }; }
expect_lookup()
{
    local wanted="$1" query="$2" status=0
    "${TOOL[@]}" lookup "$query" > "$FIXTURE/lookup.log" || status=$?
    [ "$status" -eq "$wanted" ] || { cat "$FIXTURE/lookup.log"; fail "lookup $query expected $wanted got $status"; }
    if [ "$wanted" -ne 0 ]; then
        grep -q 'name= uid=0 gid=0 admin=0 generation=0 sid=S-1-5-0-0-0-0-0$' "$FIXTURE/lookup.log" || fail 'failure published account data'
    fi
}
active() { awk -v name="$1" '$1 == name && $4 == "active" {print}' "$MAP"; }
generation() { awk 'NR == 1 {print $3}' "$MAP"; }
refuse_sync() { if "${TOOL[@]}" sync > "$FIXTURE/refusal.log" 2>&1; then fail 'unsafe sync accepted'; fi; }

# Initial provision, explicit legacy input, name/uid lookups and both group routes.
sync_map --initialize --legacy-user bob
[ "$(stat -c '%u:%g:%a' "$MAP")" = '0:19999:640' ] || fail 'map metadata'
[ "$(generation)" = 1 ] || fail 'first generation'
grep -q '^bob 1001 .* active 21 0 0 0 1000$' "$MAP" || fail 'legacy SID'
expect_lookup 0 bob
grep -q 'admin=0' "$FIXTURE/lookup.log" || fail 'non-admin'
expect_lookup 0 alice
grep -q 'admin=1' "$FIXTURE/lookup.log" || fail 'supplementary membership'
expect_lookup 0 '#1003'
grep -q 'admin=1' "$FIXTURE/lookup.log" || fail 'primary membership'
expect_lookup 10 service
status=0
"${TOOL[@]}" lookup-no-cap bob > "$FIXTURE/no-cap.log" || status=$?
[ "$status" = 12 ] || fail 'missing capability accepted'
status=0
"${TOOL[@]}" --test-server-uid 1002 lookup alice > "$FIXTURE/legacy.log" || status=$?
[ "$status" = 11 ] || fail 'ordinary local SID ambiguity accepted'
"${TOOL[@]}" --test-server-uid 1001 lookup bob > "$FIXTURE/legacy.log" || fail 'explicit legacy control denied'
cp "$MAP" "$FIXTURE/first"
sync_map --initialize --legacy-user bob
cmp "$MAP" "$FIXTURE/first" || fail 'resync changed bytes or generation'

# Addition preserves every existing record, increments once and allocates a SID.
printf 'carol:x:1004:2001::/home/carol:/bin/sh\n' >> "$PASSWD"
sync_map
[ "$(generation)" = 2 ] || fail 'addition generation'
CAROL="$(active carol)"
[ -n "$CAROL" ] || fail 'new account missing'
ALICE="$(active alice)"
awk -F: '$1 != "alice"' "$PASSWD" > "$FIXTURE/passwd.next"
mv "$FIXTURE/passwd.next" "$PASSWD"
expect_lookup 3 alice  # stale NSS identity before sync
sync_map
expect_lookup 2 alice
grep -q '^alice 1002 .* tombstone ' "$MAP" || fail 'missing tombstone'

# Same name/new UID and same UID/new name get fresh key and SID.
printf 'alice:x:1012:2001::/home/alice:/bin/sh\n' >> "$PASSWD"
sync_map
[ "$(active alice)" != "$ALICE" ] || fail 'recreation reused record'
[ "$(active alice | awk '{print $3}')" != "$(printf '%s\n' "$ALICE" | awk '{print $3}')" ] || fail 'reused incarnation'
[ "$(active alice | awk '{print $9}')" != "$(printf '%s\n' "$ALICE" | awk '{print $9}')" ] || fail 'reused RID'
awk -F: '$1 != "carol"' "$PASSWD" > "$FIXTURE/passwd.next"
printf 'dave:x:1004:2001::/home/dave:/bin/sh\n' >> "$FIXTURE/passwd.next"
mv "$FIXTURE/passwd.next" "$PASSWD"
expect_lookup 3 carol
sync_map
expect_lookup 2 carol
expect_lookup 0 dave
[ "$(active dave | awk '{print $3}')" != "$(printf '%s\n' "$CAROL" | awk '{print $3}')" ] || fail 'uid reuse reused incarnation'

# Explicit retirement handles same-name/same-UID recreation. The next sync
# allocates afresh; retirement itself must not re-add a present NSS name.
DAVE="$(active dave)"
"${TOOL[@]}" retire dave > "$FIXTURE/retire.log"
expect_lookup 2 dave
sync_map
[ "$(active dave | awk '{print $3}')" != "$(printf '%s\n' "$DAVE" | awk '{print $3}')" ] || fail 'retirement reused key'

# Contract-preserving explicit rename, after host rename, keeps incarnation/SID.
sed 's/^dave:/david:/' "$PASSWD" > "$FIXTURE/passwd.next"
mv "$FIXTURE/passwd.next" "$PASSWD"
DAVE="$(active dave)"
"${TOOL[@]}" rename dave david > "$FIXTURE/rename.log"
[ "$(active david | cut -d' ' -f2-)" = "$(printf '%s\n' "$DAVE" | cut -d' ' -f2-)" ] || fail 'rename changed identity'
expect_lookup 0 david
expect_lookup 1 nobody

cp -p "$MAP" "$FIXTURE/good"
# Inject duplicate active SID while preserving valid numeric format/counter.
awk 'NR == 1 {print; next} $1 == "alice" && $4 == "active" {rid=$9} $1 == "david" && $4 == "active" {$9=rid} {print}' \
    "$FIXTURE/good" > "$MAP"
expect_lookup 4 bob
refuse_sync
cp -p "$FIXTURE/good" "$MAP"

# Missing selected group, alternate group, non-alias wexsys and malformed map.
cp "$GROUP" "$FIXTURE/group.good"
printf 'wexsys:x:19999:\nusers:x:2001:\n' > "$GROUP"
expect_lookup 8 bob
refuse_sync
cp "$FIXTURE/group.good" "$GROUP"
printf 'group=wexsys\nadmin_group=wheel\n' > "$CONF/sysrun.conf"
expect_lookup 0 bob
grep -q 'admin=1' "$FIXTURE/lookup.log" || fail 'configured group ignored'
printf 'group=wexsys\nadmin_group=sudo\n' > "$CONF/sysrun.conf"
printf 'garbage\n' > "$MAP"
expect_lookup 7 bob
refuse_sync
cp -p "$FIXTURE/good" "$MAP"
truncate -s 1048577 "$MAP"
expect_lookup 7 bob
refuse_sync
cp -p "$FIXTURE/good" "$MAP"

# Duplicate NSS UIDs are ambiguous even when one name happens to match.
cp "$PASSWD" "$FIXTURE/passwd.good"
printf 'alias:x:1001:2001::/home/alias:/bin/sh\n' >> "$PASSWD"
expect_lookup 9 bob
refuse_sync
cp "$FIXTURE/passwd.good" "$PASSWD"

# Mode/owner/ancestor, symlink, hardlink, missing map and config protection.
chmod 0660 "$MAP"; expect_lookup 6 bob; refuse_sync
chmod 0640 "$MAP"
chown 65534:19999 "$MAP"; expect_lookup 6 bob; refuse_sync
chown 0:19999 "$MAP"
chmod 0775 "$CONF"; expect_lookup 6 bob; chmod 0755 "$CONF"
mv "$MAP" "$FIXTURE/held"
ln -s "$FIXTURE/held" "$MAP"; expect_lookup 6 bob; refuse_sync
rm "$MAP"; mv "$FIXTURE/held" "$MAP"
ln "$MAP" "$FIXTURE/link"; expect_lookup 6 bob; rm "$FIXTURE/link"
chmod 0664 "$CONF/sysrun.conf"; expect_lookup 6 bob; chmod 0644 "$CONF/sysrun.conf"
mv "$MAP" "$FIXTURE/held"
expect_lookup 5 bob
if "${TOOL[@]}" sync --initialize > "$FIXTURE/refusal.log" 2>&1; then fail 'missing map reset history'; fi
mv "$FIXTURE/held" "$MAP"

# Lock contention refuses without altering the previously committed map.
cp -p "$MAP" "$FIXTURE/pre-lock"
(
    flock -n 9 || fail 'fixture lock acquisition'
    refuse_sync
) 9<> "$CONF/.accounts.lock"
cmp "$MAP" "$FIXTURE/pre-lock" || fail 'contended writer changed map'

# Lab-only arguments and lookup operation must be rejected by the real tool.
if "$FIXTURE/production" --test-passwd "$PASSWD" sync > /dev/null 2>&1; then fail 'production test hook'; fi
if "$FIXTURE/production" lookup bob > /dev/null 2>&1; then fail 'production lookup hook'; fi
# Re-run the good control after every tamper family.
expect_lookup 0 bob
sync_map
echo 'OK: accounts map lifecycle, lookup and protection fixture'
