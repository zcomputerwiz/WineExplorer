#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Machine root metadata capture, apply, rollback and path/backup safety, in a disposable fixture.
# Run as root. Unix-tier only: group-write enforcement here is not Windows elevation (see
# docs/TOKEN_SECURITY_B_ACCEPTANCE.md). The throw-away group stands for wexsys: its member stands for a process
# that has wexsys as its effective group (started by wex-sysrun); no desktop user is a member of the real wexsys
# (docs/ELEVATION_SYSTEM_ROLE_WORKORDER.md, phase A). DESKTOP_USER stands for a desktop administrator in sudo.
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
DESKTOP_USER="wexdesk$$"
GROUP2="wextestgrpn$$"
FIX2="/tmp/disposable-migrate-$$"

cleanup()
{
    rm -rf "$FIXTURE" "$SKEL" "$BACKUPS" "$OUTSIDE" "$SHIM" "$FIX2" "$FIX2-backup" "$FIX2-themes"
    userdel "$IN_USER" 2>/dev/null || true
    userdel "$DESKTOP_USER" 2>/dev/null || true
    groupdel "$GROUP2" 2>/dev/null || true
    userdel "$OUT_USER" 2>/dev/null || true
    groupdel "$GROUP" 2>/dev/null || true
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

groupadd "$GROUP"
useradd -M -N -s /bin/bash -G "$GROUP" "$IN_USER"
useradd -M -N -s /bin/bash "$OUT_USER"
useradd -M -N -s /bin/bash -G sudo "$DESKTOP_USER"

run_setup()
{
    WEX_MACHINE_ROOT="$FIXTURE" WEX_SKEL_PFX="$SKEL" WEX_MACHINE_GROUP="$GROUP" \
        WEX_BACKUP_DIR="$BACKUPS" WEX_SKIP_WINEBOOT=1 WEX_SYSRUN_ENABLE=0 bash "$SETUP" "$@"
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
    fail "a process with the machine group cannot write the machine root"
echo "b3_unix_group_write_allowed=PASS (Unix tier only: a process with the machine group writes without Windows elevation)"
# Phase A: a desktop administrator (member of sudo, not of the machine group) cannot write it directly.
OUT="$(write_as "$DESKTOP_USER" "$FIXTURE/drive_c/windows/desktop_write.txt" || true)"
case "$OUT" in
    *"Permission denied"*) echo "b3_desktop_admin_write_denied=PASS" ;;
    *) fail "desktop administrator (sudo, not in the machine group) was not denied: $OUT" ;;
esac
[ ! -e "$FIXTURE/drive_c/windows/desktop_write.txt" ] || fail "desktop administrator write succeeded"
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
OUT="$(write_as "$DESKTOP_USER" "$BACKUPS/injected" || true)"
case "$OUT" in
    *"Permission denied"*) ;;
    *) fail "desktop administrator could write to the backup directory: $OUT" ;;
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
    if WEX_MACHINE_ROOT="$bad" WEX_BACKUP_DIR="$BACKUPS" WEX_SKIP_WINEBOOT=1 WEX_SYSRUN_ENABLE=0 \
        bash "$SETUP" apply >/tmp/b3-path-$$.log 2>&1; then
        fail "setup-machine-root accepted machine root: $bad"
    fi
    grep -q "^Error:" /tmp/b3-path-$$.log || fail "path '$bad' rejected for the wrong reason: $(cat /tmp/b3-path-$$.log)"
done
echo "b3_path_validation_and_safety=PASS"

# 7. Migration from root:sudo (--update) and creation of the system group. FIX2 is an old-style machine root:
# group sudo, default ACL g:sudo:rwX, files owned by a desktop administrator, wrong hive modes.
mkdir -p "$FIX2/drive_c/windows/system32" "$FIX2/drive_c/Program Files/OldApp" "$FIX2/dosdevices"
echo hive > "$FIX2/system.reg"; echo hive > "$FIX2/userdef.reg"
ln -s /usr/bin/true "$FIX2/drive_c/windows/system32/link.dll"
echo app > "$FIX2/drive_c/Program Files/OldApp/app.exe"
chown -R "$DESKTOP_USER:sudo" "$FIX2/drive_c/Program Files"
chown root:sudo "$FIX2" "$FIX2/drive_c" "$FIX2/drive_c/windows" "$FIX2/system.reg" "$FIX2/userdef.reg"
chmod 2775 "$FIX2" "$FIX2/drive_c" "$FIX2/drive_c/windows"
chmod 664 "$FIX2/system.reg" "$FIX2/userdef.reg"
setfacl -R -m g:sudo:rwX "$FIX2/drive_c"
setfacl -R -d -m g:sudo:rwX "$FIX2/drive_c"
setfacl -d -m g:sudo:rwX "$FIX2"
run_setup2()
{
    WEX_MACHINE_ROOT="$FIX2" WEX_SKEL_PFX="$SKEL" WEX_MACHINE_GROUP="$GROUP2" \
        WEX_BACKUP_DIR="$FIX2-backup" WEX_SKIP_WINEBOOT=1 WEX_SYSRUN_ENABLE=0 bash "$SETUP" "$@"
}
! getent group "$GROUP2" >/dev/null || fail "test group $GROUP2 exists before the run"
run_setup2 --update >/tmp/b3-mig-$$.log 2>&1 || { cat /tmp/b3-mig-$$.log; fail "migration failed"; }
getent group "$GROUP2" >/dev/null || fail "setup did not create the system group"
[ "$(getent group "$GROUP2" | cut -d: -f4)" = "" ] || fail "setup added members to the system group"
[ "$(getent group "$GROUP2" | cut -d: -f3)" -lt 1000 ] || fail "the system group is not a system group (gid >= 1000)"
echo "b3_group_created_without_members=PASS"
BAD="$(find "$FIX2" ! -type l \( ! -user root -o ! -group "$GROUP2" \) -print)"
[ -z "$BAD" ] || fail "migration left entries not root:$GROUP2: $BAD"
ACLS="$(getfacl -R -p "$FIX2" 2>/dev/null)"
case "$ACLS" in *sudo*) fail "migration left g:sudo ACL entries" ;; esac
case "$ACLS" in *"default:group:$GROUP2:rwx"*) ;; *) fail "default ACL for $GROUP2 missing" ;; esac
[ "$(stat -c '%a' "$FIX2/system.reg")" = "664" ] && [ "$(stat -c '%a' "$FIX2/userdef.reg")" = "664" ] ||
    fail "hive modes are not 664"
[ "$(stat -c '%a' "$FIX2/drive_c/Program Files/OldApp/app.exe")" = "775" ] || fail "app.exe mode is not 775"
OUT="$(write_as "$DESKTOP_USER" "$FIX2/drive_c/Program Files/desktop_write.txt" || true)"
case "$OUT" in *"Permission denied"*) ;; *) fail "after migration the desktop administrator can still write: $OUT" ;; esac
OUT="$(runuser -u "$DESKTOP_USER" -- bash -c 'echo x >> "$1"' _ "$FIX2/system.reg" 2>&1 || true)"
case "$OUT" in *"Permission denied"*) ;; *) fail "after migration the desktop administrator can write system.reg: $OUT" ;; esac
OUT="$(runuser -u "$DESKTOP_USER" -- bash -c 'echo x >> "$1"' _ "$FIX2/drive_c/Program Files/OldApp/app.exe" 2>&1 || true)"
case "$OUT" in *"Permission denied"*) ;; *) fail "after migration the desktop administrator still owns app.exe: $OUT" ;; esac
write_as "$IN_USER" "$FIX2/probe" >/dev/null 2>&1 && fail "a user outside $GROUP2 wrote the machine root"
echo "b3_migration_from_sudo=PASS"
snapshot_of()
{
    find "$1" -exec stat -c "%a %u %g %n" {} + | sort -k4
    getfacl -R -p "$1" 2>/dev/null
}
snapshot_of "$FIX2" > /tmp/b3-mig1-$$.txt
run_setup2 --update >/tmp/b3-mig-$$.log 2>&1 || { cat /tmp/b3-mig-$$.log; fail "second migration run failed"; }
snapshot_of "$FIX2" > /tmp/b3-mig2-$$.txt
cmp -s /tmp/b3-mig1-$$.txt /tmp/b3-mig2-$$.txt || { diff /tmp/b3-mig1-$$.txt /tmp/b3-mig2-$$.txt || true; fail "migration is not idempotent"; }
echo "b3_migration_idempotent=PASS"
# The system group may not be sudo (desktop users would write the machine root).
if WEX_MACHINE_ROOT="$FIX2" WEX_BACKUP_DIR="$FIX2-backup" WEX_SKIP_WINEBOOT=1 WEX_SYSRUN_ENABLE=0 WEX_MACHINE_GROUP=sudo \
    bash "$SETUP" --update >/tmp/b3-sudo-$$.log 2>&1; then
    fail "setup accepted sudo as the machine group"
fi
grep -q "dedicated system group" /tmp/b3-sudo-$$.log || fail "sudo refusal has the wrong reason"
echo "b3_sudo_group_refused=PASS"
# --update must not nest the theme directories (S2 Test 6 compares the machine root before and after).
mkdir -p "$FIX2-themes/Luna"
echo style > "$FIX2-themes/Luna/Luna.msstyles"
echo theme > "$FIX2-themes/Luna.theme"
THEME_ROOT="$FIX2/drive_c/windows/Resources/Themes"
mkdir -p "$THEME_ROOT/Luna/Luna"   # the nesting left behind by an earlier --update
WEX_THEME_DATA_DIR="$FIX2-themes" run_setup2 --update >/tmp/b3-th1-$$.log 2>&1 || { cat /tmp/b3-th1-$$.log; fail "theme run 1 failed"; }
find "$THEME_ROOT" | sort > /tmp/b3-th1-$$.txt
WEX_THEME_DATA_DIR="$FIX2-themes" run_setup2 --update >/tmp/b3-th2-$$.log 2>&1 || { cat /tmp/b3-th2-$$.log; fail "theme run 2 failed"; }
find "$THEME_ROOT" | sort > /tmp/b3-th2-$$.txt
cmp -s /tmp/b3-th1-$$.txt /tmp/b3-th2-$$.txt || { diff /tmp/b3-th1-$$.txt /tmp/b3-th2-$$.txt || true; fail "--update changes the themes"; }
[ -f "$THEME_ROOT/Luna/Luna.msstyles" ] && [ ! -e "$THEME_ROOT/Luna/Luna" ] || fail "theme directory nested: $(cat /tmp/b3-th2-$$.txt)"
echo "b3_update_themes_not_nested=PASS"

rm -f /tmp/b3-*-$$ /tmp/b3-*-$$.txt /tmp/b3-*-$$.log
