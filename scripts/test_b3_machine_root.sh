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
# session 0's own HKCU, read-only as the old setups left it (B19: the system account's settings were never saved)
echo hive > "$FIX2/user.reg"; chown root:sudo "$FIX2/user.reg"; chmod 644 "$FIX2/user.reg"
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
[ "$(stat -c '%a' "$FIX2/user.reg")" = "664" ] || fail "session 0's user.reg is not 664 (its account cannot save it)"
case "$(getfacl -p "$FIX2/user.reg" 2>/dev/null)" in *"group:$GROUP2:rw-"*) ;; *) fail "user.reg has no g:$GROUP2:rw ACL" ;; esac
OUT="$(runuser -u "$DESKTOP_USER" -- bash -c 'echo x >> "$1"' _ "$FIX2/user.reg" 2>&1 || true)"
case "$OUT" in *"Permission denied"*) ;; *) fail "after migration the desktop administrator can write user.reg: $OUT" ;; esac
echo "b3_session0_hkcu_writable_by_system_group=PASS"
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
# Root's whole-tree chown, chmod and setfacl act on inodes: a second name (hard link) of a file outside the machine root,
# planted in a shared folder, goes before anything is changed, and the outside file keeps its owner, mode and ACL; two
# names of one file that are both inside stay; a symbolic link's target is not touched; such a name at the top (where
# the hives are) refuses the setup.
HLO=/tmp/b3-hlo-$$; HLS=/tmp/b3-hls-$$; HLP=/tmp/b3-hlp-$$; DINI=/tmp/b3-dini-$$
printf private > "$HLO"; printf private > "$HLS"; printf private > "$HLP"
chown "$OUT_USER:root" "$HLO" "$HLS" "$HLP"; chmod 600 "$HLO" "$HLS" "$HLP"
mkdir -p "$FIX2/drive_c/users/Public/Documents"; chmod 1777 "$FIX2/drive_c/users/Public/Documents"
ln "$HLO" "$FIX2/drive_c/users/Public/Documents/planted.txt"
# a name ending in a space (the earlier scan lost it and removed nothing), and a dangling link where the setup creates
# desktop.ini (root's write must not create the link's target)
ln "$HLP" "$FIX2/drive_c/users/Public/Documents/secret "
rm -f "$FIX2/drive_c/users/Public/Documents/desktop.ini"; ln -s "$DINI" "$FIX2/drive_c/users/Public/Documents/desktop.ini"
printf dll > "$FIX2/drive_c/windows/system32/a.dll"; ln "$FIX2/drive_c/windows/system32/a.dll" "$FIX2/drive_c/windows/b.dll"
ln -s "$HLS" "$FIX2/drive_c/windows/outside.txt"
run_setup2 --update >/tmp/b3-hl-$$.log 2>&1 || { cat /tmp/b3-hl-$$.log; fail "the run with planted links failed"; }
[ ! -e "$FIX2/drive_c/users/Public/Documents/planted.txt" ] || fail "a second name of an outside file stayed"
grep -q "removed (a second name of a file outside the machine root): .*planted.txt" /tmp/b3-hl-$$.log ||
    fail "the removal was not reported"
[ ! -e "$FIX2/drive_c/users/Public/Documents/secret " ] || fail "a second name ending in a space stayed"
for f in "$HLO" "$HLS" "$HLP"; do
    [ "$(stat -c '%U:%G:%a' "$f")" = "$OUT_USER:root:600" ] || fail "the outside file $f became $(stat -c '%U:%G:%a' "$f")"
    ! getfacl -p "$f" 2>/dev/null | grep -q "^group:$GROUP2" || fail "the outside file $f got an ACL entry"
done
[ ! -e "$DINI" ] && [ -L "$FIX2/drive_c/users/Public/Documents/desktop.ini" ] || fail "desktop.ini was written through a link"
rm -f "$FIX2/drive_c/users/Public/Documents/desktop.ini" "$DINI"
[ "$(stat -c '%h %U:%G' "$FIX2/drive_c/windows/b.dll")" = "2 root:$GROUP2" ] || fail "two names inside were not kept and set"
[ -L "$FIX2/drive_c/windows/outside.txt" ] || fail "the symbolic link went"
ln "$HLO" "$FIX2/hive-link.reg"
if run_setup2 --update >/tmp/b3-hl2-$$.log 2>&1; then fail "a top-level name of an outside file was accepted"; fi
grep -q "has a name outside the machine root" /tmp/b3-hl2-$$.log || fail "the top-level refusal has the wrong reason"
[ "$(stat -c '%U:%G:%a' "$HLO")" = "$OUT_USER:root:600" ] || fail "the refused run changed the outside file"
rm -f "$FIX2/hive-link.reg"
# a hive replaced by a symbolic link: refused, and the link's target keeps its owner, mode and ACL
[ ! -e "$FIX2/userdef.reg" ] || mv "$FIX2/userdef.reg" "$FIX2-userdef.reg"
ln -s "$HLO" "$FIX2/userdef.reg"
if run_setup2 --update >/tmp/b3-hl4-$$.log 2>&1; then fail "a hive that is a symbolic link was accepted"; fi
grep -q "userdef.reg is a symbolic link" /tmp/b3-hl4-$$.log || fail "the hive link refusal has the wrong reason"
[ "$(stat -c '%U:%G:%a' "$HLO")" = "$OUT_USER:root:600" ] || fail "the hive link's target became $(stat -c '%U:%G:%a' "$HLO")"
rm -f "$FIX2/userdef.reg"; [ ! -e "$FIX2-userdef.reg" ] || mv "$FIX2-userdef.reg" "$FIX2/userdef.reg"
rm -f "$FIX2/drive_c/windows/outside.txt" "$FIX2/drive_c/windows/b.dll" "$FIX2/drive_c/windows/system32/a.dll" "$HLO" "$HLS" "$HLP"
run_setup2 --update >/tmp/b3-hl3-$$.log 2>&1 || { cat /tmp/b3-hl3-$$.log; fail "the run after the refusal failed"; }
echo "b3_setup_ignores_planted_hard_links=PASS"
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

# The local Group Policy store (docs/GROUP_POLICY.md, "Local policy"): session 0's account is its only writer. A tree
# that an earlier version left group-writable, with a link, a FIFO and a stale request in it, is set right: owned
# by session 0's account, not group-writable, no ACL entries, the link and the FIFO gone; only the spool is
# group-writable (2770, default ACL for the system group), and empty. A link in place of the directory is refused.
S0USER="wexs0t$$"
useradd --system -g "$GROUP2" -M -s /usr/sbin/nologin "$S0USER"
GPD="$FIX2/drive_c/windows/system32/GroupPolicy"
mkdir -p "$GPD/Machine/Microsoft/Windows NT/SecEdit" "$GPD/Archive" "$GPD/Spool"
printf 'x' > "$GPD/Machine/Registry.pol"; printf 'x' > "$GPD/Refresh"; printf 'stale' > "$GPD/Spool/Machine.Registry.pol"
ln -s /etc/passwd "$GPD/Machine/Registry.pol.new"; mkfifo "$GPD/Machine/pipe"
chown -R "root:$GROUP2" "$GPD"; chmod -R 2775 "$GPD"; chmod 664 "$GPD/Machine/Registry.pol"
setfacl -R -m "g:$GROUP2:rwX" "$GPD"; setfacl -R -d -m "g:$GROUP2:rwX" "$GPD"
# a second name for a file outside the store: chown and chmod act on the inode, so the name must go (the earlier whole-tree
# passes of the setup act on such an inode as well; fs.protected_hardlinks is what keeps a group member from making one)
HLT=/tmp/b3-hl-$$; printf private > "$HLT"; ln "$HLT" "$GPD/Machine/hardlink.pol"; HLT_CLEAN=1
# the local objects of the users (GroupPolicyUsers beside GroupPolicy): the same rules, and the directory is made when missing
GUD="$FIX2/drive_c/windows/system32/GroupPolicyUsers"
mkdir -p "$GUD/S-1-5-32-544/User"; printf 'x' > "$GUD/S-1-5-32-544/User/Registry.pol"; ln -s /etc/passwd "$GUD/S-1-5-32-544/User/Registry.pol.new"
chown -R "root:$GROUP2" "$GUD"; chmod -R 2775 "$GUD"; setfacl -R -m "g:$GROUP2:rwX" "$GUD"
# a link in the stores, to a file only root may touch (root changes the stores by descriptor, never by path): the link goes
# and the file it pointed to keeps its owner and mode; a link in the place of a store directory goes too
SECRET=/tmp/b3-secret-$$; printf secret > "$SECRET"; chmod 0600 "$SECRET"; chown root:root "$SECRET"
ln -s "$SECRET" "$GUD/S-1-5-32-544/User/Registry.pol.link"; ln -s "$SECRET" "$GPD/Machine/secret.pol"
ln -s /etc "$GUD/S-1-5-32-544/User/linkdir"
WEX_SESSION0=1 WEX_SESSION0_USER="$S0USER" run_setup2 --update >/tmp/b3-gp-$$.log 2>&1 || { cat /tmp/b3-gp-$$.log; fail "the store run failed"; }
for d in "$GPD" "$GPD/Machine" "$GPD/Machine/Microsoft/Windows NT/SecEdit" "$GPD/Archive"; do
    [ "$(stat -c '%U:%G:%a' "$d")" = "$S0USER:$GROUP2:2755" ] || fail "store directory $d is $(stat -c '%U:%G:%a' "$d")"
done
[ "$(stat -c '%U:%G:%a' "$GPD/Machine/Registry.pol")" = "$S0USER:$GROUP2:644" ] || fail "store file is $(stat -c '%U:%G:%a' "$GPD/Machine/Registry.pol")"
[ ! -e "$GPD/Machine/Registry.pol.new" ] && [ ! -L "$GPD/Machine/Registry.pol.new" ] && [ ! -e "$GPD/Machine/pipe" ] || fail "a link or a FIFO stayed in the store"
[ ! -e "$GPD/Machine/hardlink.pol" ] || fail "a file with a second name stayed in the store" ; rm -f "$HLT"
! getfacl -R -p "$GPD/Machine" "$GPD/Archive" 2>/dev/null | grep -q '^\(default:\)\?group:[^:]' || fail "the store tree has ACL entries for groups"
[ "$(stat -c '%U:%G:%a' "$GPD/Spool")" = "$S0USER:$GROUP2:2770" ] || fail "the spool is $(stat -c '%U:%G:%a' "$GPD/Spool")"
for d in "$GUD" "$GUD/S-1-5-32-544" "$GUD/S-1-5-32-544/User"; do
    [ "$(stat -c '%U:%G:%a' "$d")" = "$S0USER:$GROUP2:2755" ] || fail "the users' store directory $d is $(stat -c '%U:%G:%a' "$d")"
done
[ ! -e "$GUD/S-1-5-32-544/User/Registry.pol.new" ] && [ ! -L "$GUD/S-1-5-32-544/User/Registry.pol.new" ] || fail "a link stayed in the users' store"
[ "$(stat -c '%U:%G:%a' "$GUD/S-1-5-32-544/User/Registry.pol")" = "$S0USER:$GROUP2:644" ] || fail "the users' store file is not session 0's"
[ "$(stat -c '%U:%a' "$SECRET")" = "root:600" ] || fail "a link in a store sent root's change to the file it pointed to: $(stat -c '%U:%a' "$SECRET")"
[ ! -e "$GUD/S-1-5-32-544/User/Registry.pol.link" ] && [ ! -L "$GUD/S-1-5-32-544/User/Registry.pol.link" ] && [ ! -L "$GPD/Machine/secret.pol" ] &&
    [ ! -L "$GUD/S-1-5-32-544/User/linkdir" ] || fail "a link stayed in a store"
[ "$(stat -c '%a' /etc)" = "755" ] || fail "a link to a directory sent root's change to /etc"
rm -f "$SECRET"
getfacl -p "$GPD/Spool" 2>/dev/null | grep -q "^default:group:$GROUP2:rwx" || fail "the spool has no default ACL for the system group"
[ -z "$(ls -A "$GPD/Spool")" ] || fail "a stale request stayed in the spool"
[ -z "$(find "$GPD" -type d -perm -g+w ! -path "$GPD/Spool" ! -path "$GPD/Spool/*")" ] ||
    fail "a directory of the store is group-writable: $(find "$GPD" -type d -perm -g+w ! -path "$GPD/Spool")"
rm -rf "$GUD"; ln -s /etc "$GUD"
if WEX_SESSION0=1 WEX_SESSION0_USER="$S0USER" run_setup2 --update >/tmp/b3-gu2-$$.log 2>&1; then fail "a link in place of the users' store directory was accepted"; fi
rm -f "$GUD"
rm -rf "$GPD"; ln -s /etc "$GPD"
if WEX_SESSION0=1 WEX_SESSION0_USER="$S0USER" run_setup2 --update >/tmp/b3-gp2-$$.log 2>&1; then fail "a link in place of the store directory was accepted"; fi
rm -f "$GPD"
userdel "$S0USER" 2>/dev/null || true
echo "b3_local_policy_store_is_session_0s_and_only_the_spool_is_group_writable=PASS"

rm -f /tmp/b3-*-$$ /tmp/b3-*-$$.txt /tmp/b3-*-$$.log
