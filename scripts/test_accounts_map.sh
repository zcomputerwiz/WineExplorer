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
"$FIXTURE/lab" test-small-number || fail 'small numeric bound'
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
"$FIXTURE/lab" --directory "$CONF///" --test-passwd "$PASSWD" --test-group "$GROUP" lookup bob >/dev/null || fail 'trailing slash'
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
# Simulate interruption after the guard and durable pending map, before rename.
cp -p "$MAP" "$CONF/.accounts.pending"
rm "$MAP"
sync_map
cmp "$MAP" "$FIXTURE/first" || fail 'pending recovery changed identities'
[ ! -e "$CONF/.accounts.pending" ] || fail 'pending map not published'
mkdir -m 0755 "$FIXTURE/retry"
cp "$CONF/sysrun.conf" "$FIXTURE/retry/sysrun.conf"
printf 'incomplete' > "$FIXTURE/retry/.accounts.pending"
"$FIXTURE/lab" --directory "$FIXTURE/retry" --test-passwd "$PASSWD" --test-group "$GROUP" sync --legacy-user bob >/dev/null || fail 'unguarded pending recovery'
grep -q '^bob 1001 .* active 21 0 0 0 1000$' "$FIXTURE/retry/accounts.map" || fail 'recovered legacy identity'
touch "$CONF/.accounts-0123456789abcdef0123456789abcdef"
touch "$CONF/.accounts-not-ours"
sync_map
[ ! -e "$CONF/.accounts-0123456789abcdef0123456789abcdef" ] || fail 'stale temporary file'
[ -e "$CONF/.accounts-not-ours" ] || fail 'unrelated file removed'
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

# A later duplicate elsewhere no longer breaks this lookup; sync still refuses
# the ambiguous snapshot. UID round trips detect a conflicting first entry.
cp "$PASSWD" "$FIXTURE/passwd.good"
printf 'alias:x:1001:2001::/home/alias:/bin/sh\n' >> "$PASSWD"
expect_lookup 0 bob
refuse_sync
cp "$FIXTURE/passwd.good" "$PASSWD"

# Unrelated records beyond the old enumeration ceiling do not affect lookup.
for ((n=0; n<4100; n++)); do printf 'extra%s:x:%s:2001::/none:/bin/sh\n' "$n" "$((30000+n))"; done >> "$PASSWD"
expect_lookup 0 alice
cp "$FIXTURE/passwd.good" "$PASSWD"
# The map parser itself must also accept more than 4096 records.
cp -p "$MAP" "$FIXTURE/map.large.backup"
awk 'NR == 1 {print $1, $2, $3, 6000, $5, $6, $7; a=$5; b=$6; c=$7; next}
     $1 == "bob" && $4 == "active" {print}
     END {for (i=1; i<=4100; i++) printf "extra%d %d %032x active 21 %s %s %s %d\n", i, 30000+i, i, a, b, c, 1000+i}' "$FIXTURE/map.large.backup" > "$MAP"
expect_lookup 0 bob
cp -p "$FIXTURE/map.large.backup" "$MAP"
# Oversized NSS fields exercise reentrant buffer growth in fake NSS.
LONG_FIELD="$(printf '%04096d' 0)"
printf 'bob:x:1001:2001:%s:/home/bob:/bin/sh\n' "$LONG_FIELD" > "$PASSWD"
awk -F: '$1 != "bob"' "$FIXTURE/passwd.good" >> "$PASSWD"
printf 'wexsys:x:19999:\nsudo:x:2000:%s,bob\nusers:x:2001:\nwheel:x:2002:bob\n' "$LONG_FIELD" > "$GROUP"
expect_lookup 0 bob
grep -q 'admin=1' "$FIXTURE/lookup.log" || fail 'large group membership'
cp "$FIXTURE/passwd.good" "$PASSWD"
cp "$FIXTURE/group.good" "$GROUP"
expect_lookup 1 .
expect_lookup 1 ..
expect_lookup 1 -bad

if command -v setfacl >/dev/null; then
    setfacl -m u:65534:r "$MAP"
    expect_lookup 6 bob
    setfacl -b "$MAP"
    setfacl -m u:65534:r "$CONF"
    expect_lookup 6 bob
    setfacl -b "$CONF"
fi

# Mode/owner/ancestor, symlink, hardlink, missing map and config protection.
chown 0:19998 "$MAP"; expect_lookup 6 bob; refuse_sync
grep -q 'accounts.map: group mismatch .*expected_gid=19999' "$FIXTURE/refusal.log" || fail 'missing group-mismatch diagnostic'
chown 0:19999 "$MAP"
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
    grep -q 'lock acquisition failed:' "$FIXTURE/refusal.log" || fail 'lock contention misdiagnosed'
    grep -q 'FAIL status=6 ' "$FIXTURE/refusal.log" || fail 'lock contention status changed'
) 9<> "$CONF/.accounts.lock"
cmp "$MAP" "$FIXTURE/pre-lock" || fail 'contended writer changed map'

# Lab-only arguments and lookup operation must be rejected by the real tool.
if "$FIXTURE/production" --test-passwd "$PASSWD" sync > /dev/null 2>&1; then fail 'production test hook'; fi
if "$FIXTURE/production" lookup bob > /dev/null 2>&1; then fail 'production lookup hook'; fi
# Re-run the good control after every tamper family.
expect_lookup 0 bob
sync_map
# A legacy rename retains the identity and subsequent update-style sync does
# not repeat the stale provisioning name.
BOB="$(active bob)"
sed 's/^bob:/robert:/' "$PASSWD" > "$FIXTURE/passwd.next"
mv "$FIXTURE/passwd.next" "$PASSWD"
"${TOOL[@]}" rename bob robert > "$FIXTURE/rename-legacy.log"
[ "$(active robert | cut -d' ' -f2-)" = "$(printf '%s\n' "$BOB" | cut -d' ' -f2-)" ] || fail 'legacy rename changed identity'
sync_map --initialize
expect_lookup 0 robert

# Built-in Administrator: sysrun.conf "builtin_administrator=NAME" gives that
# account the reserved RID 500 once (separate fixture directory).
B5="$FIXTURE/b5"
mkdir -m 0755 "$B5"
printf 'wexsys:x:19999:\nsudo:x:2000:administrator,alice\nusers:x:2001:\n' > "$FIXTURE/b5.group"
printf 'administrator:x:1100:2001::/home/administrator:/bin/sh\nalice:x:1101:2001::/home/alice:/bin/sh\nbob:x:1102:2001::/home/bob:/bin/sh\n' > "$FIXTURE/b5.passwd"
B5TOOL=("$FIXTURE/lab" --directory "$B5" --test-passwd "$FIXTURE/b5.passwd" --test-group "$FIXTURE/b5.group")
b5_conf() { printf 'group=wexsys\nadmin_group=sudo\n%s' "$1" > "$B5/sysrun.conf"; chmod 0644 "$B5/sysrun.conf"; }
b5_sync() { "${B5TOOL[@]}" sync "$@" > "$FIXTURE/b5.log" 2>&1 || { cat "$FIXTURE/b5.log"; fail "b5 sync $*"; }; }
b5_refuse() { if "${B5TOOL[@]}" sync > "$FIXTURE/b5.log" 2>&1; then fail "b5: $1 accepted"; fi; }
b5_active() { awk -v name="$1" '$1 == name && $4 == "active" {print}' "$B5/accounts.map"; }
b5_rid() { b5_active "$1" | awk '{print $9}'; }

# (a) Without the key the account gets a counter RID (what Safe Mode C4 has today).
b5_conf ''
b5_sync --initialize
[ "$(b5_rid administrator)" -ge 1001 ] || fail 'b5: counter RID expected without the key'
OLD_ADMIN="$(b5_active administrator)"
# (b) With the key, one sync retires that record and maps RID 500 with a new key.
b5_conf 'builtin_administrator=administrator
'
b5_sync
[ "$(b5_rid administrator)" = 500 ] || fail 'b5: RID 500 not assigned'
grep -q "^administrator 1100 $(printf '%s\n' "$OLD_ADMIN" | awk '{print $3}') tombstone " "$B5/accounts.map" || fail 'b5: old record not retired'
[ "$(b5_active administrator | awk '{print $3}')" != "$(printf '%s\n' "$OLD_ADMIN" | awk '{print $3}')" ] || fail 'b5: key reused'
"${B5TOOL[@]}" lookup administrator > "$FIXTURE/b5.lookup" || { cat "$FIXTURE/b5.lookup"; fail 'b5: lookup'; }
grep -q 'admin=1 .*sid=S-1-5-21-[0-9]*-[0-9]*-[0-9]*-500$' "$FIXTURE/b5.lookup" || { cat "$FIXTURE/b5.lookup"; fail 'b5: lookup SID'; }
[ "$(b5_rid alice)" -ge 1001 ] && [ "$(b5_rid bob)" -ge 1001 ] || fail 'b5: other accounts must keep counter RIDs'
# (c) Idempotent: a further sync (the path/timer unit) changes nothing.
cp -p "$B5/accounts.map" "$FIXTURE/b5.good"
b5_sync
cmp "$B5/accounts.map" "$FIXTURE/b5.good" || fail 'b5: resync changed the map'
# (d) A duplicate or invalid key refuses sync and lookup (nobody resolves).
b5_conf 'builtin_administrator=administrator
builtin_administrator=administrator
'
b5_refuse 'duplicate builtin_administrator'
status=0; "${B5TOOL[@]}" lookup alice > "$FIXTURE/b5.lookup" || status=$?
[ "$status" = 7 ] || fail "b5: duplicate key lookup status $status"
b5_conf 'builtin_administrator=-bad
'
b5_refuse 'invalid builtin_administrator'
# (e) RID 500 is never reassigned: after retirement a new account gets a counter RID.
b5_conf 'builtin_administrator=administrator
'
awk -F: '$1 != "administrator"' "$FIXTURE/b5.passwd" > "$FIXTURE/b5.next"; mv "$FIXTURE/b5.next" "$FIXTURE/b5.passwd"
b5_sync
[ -z "$(b5_active administrator)" ] || fail 'b5: removed account still active'
printf 'administrator:x:1103:2001::/home/administrator:/bin/sh\n' >> "$FIXTURE/b5.passwd"
b5_sync
[ "$(b5_rid administrator)" -ge 1001 ] || fail 'b5: RID 500 reused'
# (f) The account must be an administrator, else sync refuses and the map is unchanged.
mkdir -m 0755 "$FIXTURE/b5n"
printf 'group=wexsys\nadmin_group=sudo\nbuiltin_administrator=bob\n' > "$FIXTURE/b5n/sysrun.conf"; chmod 0644 "$FIXTURE/b5n/sysrun.conf"
if "$FIXTURE/lab" --directory "$FIXTURE/b5n" --test-passwd "$FIXTURE/b5.passwd" --test-group "$FIXTURE/b5.group" sync --initialize > "$FIXTURE/b5.log" 2>&1; then
    fail 'b5: non-administrator given RID 500'
fi
[ ! -e "$FIXTURE/b5n/accounts.map" ] || fail 'b5: refused sync wrote a map'
# (g) A second RID 500 record (here a tombstone) is a SID collision.
cp -p "$B5/accounts.map" "$FIXTURE/b5.keep"
python3 -I - "$B5/accounts.map" <<'PY'
import sys
p = sys.argv[1]
lines = open(p).read().splitlines()
recs = [l.split(' ') for l in lines[1:]]
r500 = [r for r in recs if r[8] == '500']
assert len(r500) == 1, r500
twin = list(r500[0]); twin[0] = 'eve'; twin[1] = '1199'; twin[2] = 'f' * 32; twin[3] = 'tombstone'
open(p, 'w').write('\n'.join(lines + [' '.join(twin)]) + '\n')
PY
status=0; "${B5TOOL[@]}" lookup alice > "$FIXTURE/b5.lookup" || status=$?
[ "$status" = 4 ] || fail "b5: duplicate RID 500 lookup status $status"
cp -p "$FIXTURE/b5.keep" "$B5/accounts.map"

# wex-is-admin: same policy parser and membership rule as the server.
cc -O2 -Wall -Wextra -Werror -DWEX_ACCOUNTS_TEST -o "$FIXTURE/is-admin-lab" "$REPO/tools/wex-is-admin/wex-is-admin.c"
cc -O2 -Wall -Wextra -Werror -o "$FIXTURE/is-admin" "$REPO/tools/wex-is-admin/wex-is-admin.c"
ISA=("$FIXTURE/is-admin-lab" --directory "$B5" --test-passwd "$FIXTURE/b5.passwd" --test-group "$FIXTURE/b5.group")
expect_admin()
{
    local wanted="$1" status=0; shift
    "$@" > "$FIXTURE/isa.log" 2>&1 || status=$?
    [ "$status" -eq "$wanted" ] || { cat "$FIXTURE/isa.log"; fail "wex-is-admin $* expected $wanted got $status"; }
}
expect_admin 0 "${ISA[@]}" alice
expect_admin 0 "${ISA[@]}" administrator
expect_admin 1 "${ISA[@]}" bob
expect_admin 1 "${ISA[@]}" nosuchuser
expect_admin 1 "${ISA[@]}" -bad
expect_admin 2 "${ISA[@]}"
expect_admin 2 "${ISA[@]}" alice bob
b5_conf 'admin_group=sudo
'
expect_admin 1 "${ISA[@]}" alice   # duplicate admin_group: nobody is an administrator
printf 'group=wexsys\nadmin_group=nosuchgroup\n' > "$B5/sysrun.conf"
expect_admin 1 "${ISA[@]}" alice   # missing group: nobody is an administrator
b5_conf ''
chmod 0664 "$B5/sysrun.conf"
expect_admin 1 "${ISA[@]}" alice   # group-writable configuration
chmod 0644 "$B5/sysrun.conf"
expect_admin 0 "${ISA[@]}" alice
# The production build has no fixture options: they are just extra arguments.
expect_admin 2 "$FIXTURE/is-admin" --directory "$B5" alice
echo 'OK: accounts map lifecycle, lookup and protection fixture'
