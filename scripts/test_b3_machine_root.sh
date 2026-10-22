#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Machine root metadata capture, apply, rollback and path/backup safety, in a disposable fixture.
# Run as root. Unix-tier only: group-write enforcement here is not Windows elevation (see
# docs/TOKEN_SECURITY_B_ACCEPTANCE.md).
set -euo pipefail

[ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 2; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SETUP="$SCRIPT_DIR/setup-machine-root.sh"
FIXTURE="/tmp/disposable-machine-root-$$"
SKEL="/tmp/disposable-skel-$$"
BACKUPS="/tmp/disposable-backup-$$"
OUTSIDE="/tmp/disposable-outside-$$"
SHIM="/tmp/disposable-shim-$$"
GROUP="wextestgrp$$"
IN_USER="wextest$$"
OUT_USER="wexout$$"

cleanup()
{
    rm -rf "$FIXTURE" "$SKEL" "$BACKUPS" "$OUTSIDE" "$SHIM"
    userdel "$IN_USER" 2>/dev/null || true
    userdel "$OUT_USER" 2>/dev/null || true
    groupdel "$GROUP" 2>/dev/null || true
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

groupadd "$GROUP"
useradd -M -N -s /bin/bash -G "$GROUP" "$IN_USER"
useradd -M -N -s /bin/bash "$OUT_USER"

run_setup()
{
    WEX_MACHINE_ROOT="$FIXTURE" WEX_SKEL_PFX="$SKEL" WEX_MACHINE_GROUP="$GROUP" \
        WEX_BACKUP_DIR="$BACKUPS" WEX_SKIP_WINEBOOT=1 bash "$SETUP" "$@"
}

# Fixture with non-default owner, mode and ACL state on the root, a nested dir, files with an
# extended ACL and a space in the name, and a symlink.
mkdir -p "$FIXTURE/drive_c/windows/system32" "$FIXTURE/drive_c/Program Files" "$OUTSIDE"
touch "$FIXTURE/system.reg" "$FIXTURE/userdef.reg" "$OUTSIDE/file"
echo "custom content" > "$FIXTURE/drive_c/windows/test file.txt"
chmod 700 "$FIXTURE/drive_c/windows/test file.txt"
chown 0:0 "$FIXTURE"
chmod 750 "$FIXTURE"
chmod 700 "$FIXTURE/drive_c" "$FIXTURE/drive_c/Program Files"
chown "$IN_USER:$GROUP" "$FIXTURE/drive_c/Program Files"
ln -s "$OUTSIDE" "$FIXTURE/link-outside"
if command -v setfacl >/dev/null 2>&1; then
    setfacl -m "u:$OUT_USER:r-x" "$FIXTURE/drive_c/windows/test file.txt"
    setfacl -m "u:$OUT_USER:r-x" "$FIXTURE/drive_c/Program Files"
fi
chmod 600 "$OUTSIDE/file"
OUTSIDE_STAT="$(stat -c '%a %u %g' "$OUTSIDE/file")"

snapshot()
{
    find "$FIXTURE" -exec stat -c "%a %u %g %n" {} + | sort -k4
    if command -v getfacl >/dev/null 2>&1; then
        getfacl -R -p "$FIXTURE" 2>/dev/null
    fi
}
snapshot > /tmp/b3-before-$$.txt

# Compare only entries that existed before: apply may create new directories and links.
same_as_before()
{
    snapshot > /tmp/b3-after-$$.txt
    python3 -I - "/tmp/b3-before-$$.txt" "/tmp/b3-after-$$.txt" <<'PY'
import sys
def parse(path):
    stat, acl, block = {}, {}, []
    for line in open(path).read().split('\n'):
        parts = line.split(' ', 3)
        if len(parts) == 4 and parts[0].isdigit() and parts[1].isdigit() and parts[2].isdigit():
            stat[parts[3]] = ' '.join(parts[:3])
        elif line.startswith('# file: '):
            block = [line]
            acl[line[8:]] = block
        elif line and block:
            block.append(line)
    return stat, acl
bs, ba = parse(sys.argv[1])
as_, aa = parse(sys.argv[2])
bad = [p for p in bs if as_.get(p) != bs[p]]
bad += [p for p in ba if aa.get(p) != ba[p]]
if bad:
    print('restored state differs for:', *sorted(set(bad)), sep='\n  ')
    sys.exit(1)
PY
}

echo "Initial: $(stat -c '%a %u %g' "$FIXTURE")"

# 1. Apply, then check the Unix-tier write behaviour.
run_setup apply
[ "$(stat -c '%a' "$FIXTURE/drive_c/windows/test file.txt")" != "700" ] || fail "apply did not change the file mode"

write_as()
{
    runuser -u "$1" -- bash -c 'touch "$1" 2>&1' _ "$2"
}
SELF_TMP="$(mktemp -d /tmp/b3-selftmp-XXXXXX)"
chmod 1777 "$SELF_TMP"
write_as "$IN_USER" "$SELF_TMP/in" >/dev/null || fail "positive control: $IN_USER cannot write its own temp dir"
write_as "$OUT_USER" "$SELF_TMP/out" >/dev/null || fail "positive control: $OUT_USER cannot write its own temp dir"
rm -rf "$SELF_TMP"
write_as "$IN_USER" "$FIXTURE/drive_c/windows/in_group_write.txt" >/dev/null ||
    fail "machine group member cannot write the machine root"
echo "b3_unix_group_write_allowed=PASS (Unix tier only: group members write without Windows elevation)"
OUT="$(write_as "$OUT_USER" "$FIXTURE/drive_c/windows/outsider_write.txt" || true)"
case "$OUT" in
    *"Permission denied"*) echo "b3_unix_outsider_write_denied=PASS" ;;
    *) fail "outsider write was not denied with Permission denied: $OUT" ;;
esac
[ ! -e "$FIXTURE/drive_c/windows/outsider_write.txt" ] || fail "outsider write succeeded"
OUT="$(write_as "$OUT_USER" "$BACKUPS/injected" || true)"
case "$OUT" in
    *"Permission denied"*) ;;
    *) fail "outsider could write to the backup directory: $OUT" ;;
esac
OUT="$(write_as "$IN_USER" "$BACKUPS/injected" || true)"
case "$OUT" in
    *"Permission denied"*) echo "b3_backup_dir_not_writable_by_group=PASS" ;;
    *) fail "machine group member could write to the backup directory: $OUT" ;;
esac

# 2. Tampered backup entry pointing outside the machine root must be refused.
cp "$BACKUPS"/*/stat /tmp/b3-stat-genuine-$$
BDIR="$(echo "$BACKUPS"/*)"
printf '777 0 0 %s\n' "$OUTSIDE/file" >> "$BDIR/stat"
if run_setup rollback >/tmp/b3-tamper-$$.log 2>&1; then
    fail "rollback accepted a backup entry outside the machine root"
fi
grep -q "outside the machine root" /tmp/b3-tamper-$$.log || fail "tamper refusal has the wrong reason"
[ "$(stat -c '%a %u %g' "$OUTSIDE/file")" = "$OUTSIDE_STAT" ] || fail "outside file was modified by rollback"
echo "b3_tampered_backup_refused=PASS"
cp /tmp/b3-stat-genuine-$$ "$BDIR/stat"

# 3. A failing restore command must fail the rollback and keep the backups.
mkdir -p "$SHIM"
printf '#!/bin/sh\nexit 1\n' > "$SHIM/chmod"
chmod 755 "$SHIM/chmod"
if PATH="$SHIM:$PATH" run_setup rollback >/tmp/b3-shim-$$.log 2>&1; then
    fail "rollback reported success although chmod failed"
fi
grep -q "completed successfully" /tmp/b3-shim-$$.log && fail "rollback printed success after a failure"
[ -f "$BDIR/stat" ] || fail "backups were removed after a failed rollback"
echo "b3_failed_restore_fails_rollback=PASS"

# 4. Real rollback restores every pre-existing entry exactly, including the root directory.
run_setup rollback
if ! same_as_before; then fail "metadata after rollback differs from before apply"; fi
[ ! -e "$BDIR/stat" ] || fail "backups left after a successful rollback"
echo "b3_metadata_capture_and_rollback=PASS"

# 5. Rollback without a backup refuses to guess.
if run_setup rollback >/tmp/b3-nobackup-$$.log 2>&1; then fail "rollback without a backup succeeded"; fi
grep -q "refusing to guess" /tmp/b3-nobackup-$$.log || fail "no-backup refusal has the wrong reason"
echo "b3_rollback_without_backup_refused=PASS"

# 6. Dangerous or non-canonical roots are rejected before anything changes.
for bad in "/" "/home" "/var/" "/var/lib" "relative/path" "/tmp/../etc" "/tmp"; do
    if WEX_MACHINE_ROOT="$bad" WEX_BACKUP_DIR="$BACKUPS" WEX_SKIP_WINEBOOT=1 \
        bash "$SETUP" apply >/tmp/b3-path-$$.log 2>&1; then
        fail "setup-machine-root accepted machine root: $bad"
    fi
    grep -q "^Error:" /tmp/b3-path-$$.log || fail "path '$bad' rejected for the wrong reason: $(cat /tmp/b3-path-$$.log)"
done
echo "b3_path_validation_and_safety=PASS"

rm -f /tmp/b3-*-$$ /tmp/b3-*-$$.txt /tmp/b3-*-$$.log
