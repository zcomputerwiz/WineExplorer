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

# ---- Domain accounts, map version 2 (docs/DOMAIN_LOGON.md, WO 1) ----
# A fake Winbind: wbinfo answers from files in $WB (own-domain, domain SID, users, ping); NSS knows the domain
# users from a second passwd file that /etc/passwd (the local enumeration) does not have.
D2="$FIXTURE/d2"; WB="$FIXTURE/wb"
mkdir -m 0755 "$D2" "$WB"
printf 'group=wexsys\nadmin_group=sudo\n' > "$D2/sysrun.conf"; chmod 0644 "$D2/sysrun.conf"
printf 'wexsys:x:19999:\nsudo:x:2000:\nusers:x:2001:\n' > "$FIXTURE/d2.group"
printf 'bob:x:1001:2001::/home/bob:/bin/sh\nalice:x:1002:2001::/home/alice:/bin/sh\n' > "$FIXTURE/d2.passwd"
cat > "$FIXTURE/d2.winbind" <<'EOF'
WEXLAB\wexuser1:x:21104:20513::/home/WEXLAB/wexuser1:/bin/bash
WEXLAB\wexuser2:x:21105:20513::/home/WEXLAB/wexuser2:/bin/bash
WEXLAB\nologin:x:21106:20513::/home/WEXLAB/nologin:/bin/false
WEXLAB\lowuid:x:900:20513::/home/WEXLAB/lowuid:/bin/bash
WEXLAB\clash:x:1002:20513::/home/WEXLAB/clash:/bin/bash
WEXLAB\group1:x:21107:20513::/home/WEXLAB/group1:/bin/bash
WEXLAB\foreign:x:21108:20513::/home/WEXLAB/foreign:/bin/bash
OTHER\x:x:22000:20513::/home/OTHER/x:/bin/bash
EOF
echo WEXLAB > "$WB/own-domain"
echo 'S-1-5-21-111-222-333' > "$WB/domain-sid"
cat > "$WB/users" <<'EOF'
WEXLAB\wexuser1 S-1-5-21-111-222-333-1104 SID_USER (1)
WEXLAB\wexuser2 S-1-5-21-111-222-333-1105 SID_USER (1)
WEXLAB\nologin S-1-5-21-111-222-333-1106 SID_USER (1)
WEXLAB\lowuid S-1-5-21-111-222-333-1107 SID_USER (1)
WEXLAB\clash S-1-5-21-111-222-333-1108 SID_USER (1)
WEXLAB\group1 S-1-5-21-111-222-333-1109 SID_DOM_GROUP (2)
WEXLAB\foreign S-1-5-21-9-9-9-1110 SID_USER (1)
EOF
touch "$WB/ping"
cat > "$FIXTURE/wbinfo" <<EOF
#!/bin/sh
W="$WB"
case "\$1" in
--own-domain) [ -s "\$W/own-domain" ] && cat "\$W/own-domain" ;;
-D) [ "\$2" = "\$(cat "\$W/own-domain")" ] || exit 1
    printf 'Name              : %s\nAlt_Name          : ad.wexlab.example\nSID               : %s\n' "\$2" "\$(cat "\$W/domain-sid")" ;;
--name-to-sid) N="\$2" awk '\$1 == ENVIRON["N"] { \$1 = ""; sub(/^ /, ""); print; f = 1 } END { exit !f }' "\$W/users" ;;
--ping-dc) [ -e "\$W/ping" ] ;;
*) exit 1 ;;
esac
EOF
chmod 0755 "$FIXTURE/wbinfo"
D2TOOL=("$FIXTURE/lab" --directory "$D2" --test-passwd "$FIXTURE/d2.passwd" --test-group "$FIXTURE/d2.group"
        --test-winbind-passwd "$FIXTURE/d2.winbind" --test-wbinfo "$FIXTURE/wbinfo")
D2MAP="$D2/accounts.map"
admit() { env -i PAM_TYPE="${PAM_TYPE_OVERRIDE:-open_session}" PAM_USER="$1" "${D2TOOL[@]}" admit > "$FIXTURE/admit.log" 2>&1; }
admit_ok() { admit "$1" || { cat "$FIXTURE/admit.log"; fail "admit $1"; }; }
# a refusal with the status expected (so that a case is refused for its own reason), the map unchanged
admit_refused()
{
    cp -p "$D2MAP" "$FIXTURE/d2.before"
    if admit "$1"; then fail "admit $1 accepted ($2)"; fi
    [ -z "${3:-}" ] || grep -q "FAIL status=$3 " "$FIXTURE/admit.log" || { cat "$FIXTURE/admit.log"; fail "admit $1: not status $3 ($2)"; }
    cmp -s "$D2MAP" "$FIXTURE/d2.before" || fail "refused admit of $1 changed the map ($2)"
}
d2_lookup()
{
    local wanted="$1" query="$2" status=0
    "${D2TOOL[@]}" lookup "$query" > "$FIXTURE/d2.lookup" || status=$?
    [ "$status" -eq "$wanted" ] || { cat "$FIXTURE/d2.lookup"; fail "d2 lookup $query expected $wanted got $status"; }
}
d2_sync() { "${D2TOOL[@]}" sync "$@" > "$FIXTURE/d2.sync" 2>&1 || { cat "$FIXTURE/d2.sync"; fail "d2 sync $*"; }; }
d2_gen() { awk 'NR == 1 {print $3}' "$D2MAP"; }

d2_sync --initialize --legacy-user bob
head -c 13 "$D2MAP" | grep -qx 'WEXACCOUNTS 1' || fail 'd2: a fresh map is not version 1'
# admit takes nothing from its arguments and needs PAM's session or account step
if env -i PAM_TYPE=open_session "${D2TOOL[@]}" admit 'WEXLAB\wexuser1' > /dev/null 2>&1; then fail 'admit took a name argument'; fi
if env -i PAM_USER='WEXLAB\wexuser1' "${D2TOOL[@]}" admit > /dev/null 2>&1; then fail 'admit without PAM_TYPE'; fi
PAM_TYPE_OVERRIDE=auth admit_refused 'WEXLAB\wexuser1' 'auth step'
# a local user is sync's business: nothing changes
cp -p "$D2MAP" "$FIXTURE/d2.v1"
admit_ok bob
cmp -s "$D2MAP" "$FIXTURE/d2.v1" || fail 'admit of a local user changed the map'
# the first domain user: the map becomes version 2 with the domain in its header
admit_ok 'WEXLAB\wexuser1'
head -1 "$D2MAP" | grep -q '^WEXACCOUNTS 2 .* 111 222 333 WEXLAB$' || { head -1 "$D2MAP"; fail 'd2: version 2 header'; }
grep -q '^WEXLAB\\wexuser1 21104 [0-9a-f]\{32\} active domain 21 111 222 333 1104$' "$D2MAP" || { cat "$D2MAP"; fail 'd2: domain record'; }
grep -q '^bob 1001 [0-9a-f]\{32\} active local 21 0 0 0 1000$' "$D2MAP" || fail 'd2: the legacy record in version 2'
d2_lookup 0 'WEXLAB\wexuser1'
grep -q 'name=WEXLAB\\wexuser1 uid=21104 .*sid=S-1-5-21-111-222-333-1104$' "$FIXTURE/d2.lookup" || { cat "$FIXTURE/d2.lookup"; fail 'd2: domain SID'; }
d2_lookup 0 '#21104'
d2_lookup 0 alice
grep -q 'sid=S-1-5-21-111-222-333-' "$FIXTURE/d2.lookup" && fail 'd2: a local account got the domain SID'
G="$(d2_gen)"
admit_ok 'WEXLAB\wexuser1'
[ "$(d2_gen)" = "$G" ] || fail 'd2: a second admit changed the map'
# sync keeps the domain record (not in /etc/passwd) and changes nothing
d2_sync
[ "$(d2_gen)" = "$G" ] || { cat "$D2MAP"; fail 'd2: sync changed a map with a domain record'; }
# refusals: each leaves the map as it was (fail closed)
# statuses: 1 unknown, 3 NSS mismatch, 4 SID collision, 9 NSS error, 10 unsupported
admit_refused 'WEXLAB\nologin' 'shell that cannot log on' 10
admit_refused 'WEXLAB\lowuid' 'UID below 1000' 10
# (NSS finds the local account for that UID, so the round trip already disagrees)
admit_refused 'WEXLAB\clash' 'UID of a local account' 3
admit_refused 'WEXLAB\group1' 'a group, not a user' 1
admit_refused 'WEXLAB\foreign' 'SID of another domain' 10
admit_refused 'OTHER\x' 'a domain the machine is not joined to' 10
admit_refused 'WEXLAB\nosuch' 'unknown to NSS' 3
admit_refused 'WEXLAB\a\b' 'malformed name' 3
mv "$WB/own-domain" "$WB/own-domain.off"
admit_refused 'WEXLAB\wexuser2' 'Winbind not answering' 9
mv "$WB/own-domain.off" "$WB/own-domain"
admit_ok 'WEXLAB\wexuser2'
# the controller away: sync keeps every domain record, also one the domain has dropped
sed -i '/wexuser2/d' "$WB/users"
rm "$WB/ping"
d2_sync
grep -q '^WEXLAB\\wexuser2 .* active domain ' "$D2MAP" || fail 'd2: a record was retired while the controller was away'
# the controller back: the dropped user becomes a tombstone, the other stays
touch "$WB/ping"
d2_sync
grep -q '^WEXLAB\\wexuser2 .* tombstone domain ' "$D2MAP" || fail 'd2: a user the domain dropped stayed active'
grep -q '^WEXLAB\\wexuser1 .* active domain ' "$D2MAP" || fail 'd2: a user the domain has was retired'
d2_lookup 2 'WEXLAB\wexuser2'
# the same SID is not given twice, not even to the account coming back
echo 'WEXLAB\wexuser2 S-1-5-21-111-222-333-1105 SID_USER (1)' >> "$WB/users"
admit_refused 'WEXLAB\wexuser2' 'SID of a tombstone' 4
# a local account that takes a domain account's UID wins it
printf 'late:x:21104:2001::/home/late:/bin/sh\n' >> "$FIXTURE/d2.passwd"
d2_sync
grep -q '^WEXLAB\\wexuser1 .* tombstone domain ' "$D2MAP" || fail 'd2: a domain record kept a UID a local account has'
sed -i '/^late:/d' "$FIXTURE/d2.passwd"
d2_sync
# a further user, active, for what follows; the map as it is now is the good one of the parser cases
printf 'WEXLAB\\wexuser3:x:21111:20513::/home/WEXLAB/wexuser3:/bin/bash\n' >> "$FIXTURE/d2.winbind"
echo 'WEXLAB\wexuser3 S-1-5-21-111-222-333-1111 SID_USER (1)' >> "$WB/users"
admit_ok 'WEXLAB\wexuser3'
d2_lookup 0 'WEXLAB\wexuser3'
cp -p "$D2MAP" "$FIXTURE/d2.good"
# another domain: the old domain's records become tombstones and the header follows
echo NEWDOM > "$WB/own-domain"
echo 'S-1-5-21-444-555-666' > "$WB/domain-sid"
d2_sync
head -1 "$D2MAP" | grep -q ' 444 555 666 NEWDOM$' || { head -1 "$D2MAP"; fail 'd2: header after a new domain'; }
! grep -q ' active domain ' "$D2MAP" || fail 'd2: an old domain record stayed active'
d2_lookup 2 'WEXLAB\wexuser3'
d2_lookup 0 alice
echo WEXLAB > "$WB/own-domain"; echo 'S-1-5-21-111-222-333' > "$WB/domain-sid"

# The version 2 parser fails closed (condition 5): each damage makes every lookup fail; fuzz-style.
cp -p "$FIXTURE/d2.good" "$D2MAP"
d2_lookup 0 alice
mutate()   # awk program applied to the good map; then alice must be refused with the status given
{
    local wanted="$1" program="$2"
    awk "$program" "$FIXTURE/d2.good" > "$D2MAP"
    d2_lookup "$wanted" alice
    cp -p "$FIXTURE/d2.good" "$D2MAP"
}
mutate 7 'NR > 1 && $5 == "domain" { $5 = "remote" } { print }'                       # unknown kind
mutate 7 'NR == 1 { NF = 10 } { print }'                                               # no domain name
mutate 7 'NR == 1 { $8 = $5; $9 = $6; $10 = $7 } { print }'                            # domain = machine
mutate 7 'NR > 1 && $1 == "alice" { $7 = 111; $8 = 222; $9 = 333 } { print }'          # local with a domain SID
mutate 7 'NR > 1 && $5 == "domain" { $7 = 0; $8 = 0; $9 = 0 } { print }'                # domain record, zero SID
mutate 7 'NR > 1 && $5 == "domain" { $2 = 900 } { print }'                             # domain UID below 1000
mutate 7 'NR > 1 && $5 == "domain" { sub(/^WEXLAB\\/, "", $1) } { print }'              # domain name without domain
mutate 7 'NR > 1 && $5 == "domain" { $4 = "active"; sub(/^WEXLAB/, "OTHER", $1) } { print }'   # active, other domain
mutate 7 'NR > 1 { NF = 9 } { print }'                                                 # version 1 records in version 2
mutate 7 'NR == 1 { $2 = 3 } { print }'                                                 # unknown version
mutate 4 'NR > 1 && $5 == "domain" { r = $10 } { print } END { print "WEXLAB\\dup 23000 " sprintf("%032x", 7) " tombstone domain 21 111 222 333 " r }'
# random single-field damage: never a crash, never a success with a changed map
for n in $(seq 1 300); do
    awk -v seed="$n" 'BEGIN { srand(seed) } { l[NR] = $0 } END {
        t = int(rand() * NR) + 1; split(l[t], f, " "); k = int(rand() * length(f)) + 1
        junk[1] = ""; junk[2] = "-1"; junk[3] = "4294967296"; junk[4] = "x\\y\\z"; junk[5] = "007"; junk[6] = "active"
        junk[7] = "domain"; junk[8] = "S-1-5-21"; junk[9] = "0"; junk[10] = sprintf("%080d", 1)
        f[k] = junk[int(rand() * 10) + 1]; s = f[1]; for (i = 2; i <= length(f); i++) s = s " " f[i]; l[t] = s
        for (i = 1; i <= NR; i++) print l[i] }' "$FIXTURE/d2.good" > "$D2MAP"
    status=0; "${D2TOOL[@]}" lookup alice > "$FIXTURE/d2.lookup" 2>&1 || status=$?
    [ "$status" -lt 64 ] || { cat "$D2MAP"; fail "fuzz $n: lookup crashed ($status)"; }
    if [ "$status" = 0 ] && ! cmp -s "$D2MAP" "$FIXTURE/d2.good"; then
        # a damaged field may still be a valid map (for example another valid number); it must parse as one
        grep -q 'name=alice uid=1002 ' "$FIXTURE/d2.lookup" || { cat "$D2MAP"; fail "fuzz $n: wrong account"; }
    fi
done
cp -p "$FIXTURE/d2.good" "$D2MAP"
d2_lookup 0 alice
# a version 1 map keeps working byte for byte (the earlier part of this suite) and is not rewritten by sync
cmp "$MAP" "$MAP" >/dev/null
echo 'OK: domain accounts (map version 2, admit, sync, parser)'
echo 'OK: accounts map lifecycle, lookup and protection fixture'
