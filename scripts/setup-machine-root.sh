#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sets up or updates the shared machine root (/var/lib/wineexplorer/machine)
# and populates/links prefix templates and user prefixes according to the S2 model.
set -euo pipefail

MACHINE_ROOT="${WEX_MACHINE_ROOT:-/var/lib/wineexplorer/machine}"
SKEL_PFX="${WEX_SKEL_PFX:-/etc/skel/.wine}"

ACTION="${1:-apply}"

# Ownership model (docs/ELEVATION_SYSTEM_ROLE_WORKORDER.md): the machine root belongs to root:wexsys.
# wexsys is a system group that no desktop user is a member of, sudo members included. Only processes that the
# setgid wex-sysrun launcher starts have wexsys as their effective group, so a limited process cannot write the
# machine root, even with raw Linux syscalls. WEX_MACHINE_GROUP overrides the name (tests use a throw-away group).
MACHINE_GROUP="${WEX_MACHINE_GROUP:-wexsys}"

die()
{
    echo "Error: $*" >&2
    exit 1
}

# Disposable fixtures synchronize explicitly and must not install/start host
# account watchers. Ordinary setup retains automatic synchronization.
ACCOUNTS_AUTOSYNC=${WEX_ACCOUNTS_AUTOSYNC:-1}
case "$ACCOUNTS_AUTOSYNC" in 0|1) ;; *) die 'WEX_ACCOUNTS_AUTOSYNC must be 0 or 1' ;; esac

# System tier (session 0): dedicated wineserver for the machine registry.
# Opt-in during T1; default off. WEX_SESSION0=1 enables.
SESSION0_ENABLE=${WEX_SESSION0:-0}
case "$SESSION0_ENABLE" in 0|1) ;; *) die 'WEX_SESSION0 must be 0 or 1' ;; esac

# Enforced elevation (docs/SYSTEM_TIER_WORK_ORDERS.md, "Enforced elevation on an installed machine"): opt-in,
# default off. WEX_ELEVATION=1 names the consent helper in the machine hive; with it the servers give limited
# tokens and refuse a limited token's writes to machine state.
ELEVATION_ENABLE=${WEX_ELEVATION:-0}
case "$ELEVATION_ENABLE" in 0|1) ;; *) die 'WEX_ELEVATION must be 0 or 1' ;; esac

case "$ACTION" in
    apply|update|--update) ACTION=apply ;;  # the package postinst passes --update
    rollback|--rollback) ;;
    *) die "unknown action: $ACTION (use apply, --update or rollback)" ;;
esac

[ "$(id -u)" -eq 0 ] || die "must run as root"

# The system group must exist and must not be a group that desktop users belong to: wexsys members would write
# the machine root directly. No user is ever added to it here.
case "$MACHINE_GROUP" in
    sudo|wheel|admin|adm|staff|users|root)
        die "$MACHINE_GROUP is a desktop or administrative group; the machine root needs a dedicated system group (wexsys)" ;;
esac

# Validate the target before anything else. The path must already be canonical (no symlink
# components, no "..", no trailing slash) and must not be a system directory.
[ -n "$MACHINE_ROOT" ] || die "empty machine root"
case "$MACHINE_ROOT" in
    /*) ;;
    *) die "machine root must be an absolute path: $MACHINE_ROOT" ;;
esac
[ "$(realpath -m -- "$MACHINE_ROOT")" = "$MACHINE_ROOT" ] ||
    die "machine root is not a canonical path: $MACHINE_ROOT"
case "$MACHINE_ROOT" in
    /|/bin|/boot|/dev|/etc|/home|/lib|/lib32|/lib64|/opt|/proc|/root|/run|/sbin|/srv|/sys|/tmp|/usr|/var|/var/lib)
        die "dangerous machine root path: $MACHINE_ROOT" ;;
esac

# Metadata backups live outside the machine root: members of the machine group can write there,
# and rollback runs as root and trusts the backup.
BACKUP_ROOT="${WEX_BACKUP_DIR:-/var/lib/wineexplorer/backup}"
[ "$(realpath -m -- "$BACKUP_ROOT")" = "$BACKUP_ROOT" ] || die "backup directory is not a canonical path: $BACKUP_ROOT"
case "$BACKUP_ROOT/" in
    "$MACHINE_ROOT"/*) die "backup directory must be outside the machine root" ;;
esac
BACKUP_DIR="$BACKUP_ROOT/$(printf '%s' "$MACHINE_ROOT" | sha256sum | cut -c1-16)"
ACL_BACKUP="$BACKUP_DIR/acl"
STAT_BACKUP="$BACKUP_DIR/stat"

check_private_dir()
{
    [ -d "$1" ] && [ ! -L "$1" ] || die "not a plain directory: $1"
    [ "$(stat -c '%u' -- "$1")" = "0" ] || die "not owned by root: $1"
    [ $(( 0$(stat -c '%a' -- "$1") & 022 )) -eq 0 ] || die "group or other writable: $1"
}

if [ "$ACTION" = "apply" ] && ! getent group "$MACHINE_GROUP" >/dev/null 2>&1; then
    echo "Creating system group $MACHINE_GROUP..."
    groupadd --system "$MACHINE_GROUP"
fi
if [ "$ACTION" = "apply" ]; then
    mkdir -p -- "$BACKUP_ROOT"
    install -d -m 0700 -o root -g root -- "$BACKUP_DIR"
fi
check_private_dir "$BACKUP_ROOT"
check_private_dir "$BACKUP_DIR"

# A path may only be touched when it is canonical and inside the machine root.
inside_machine_root()
{
    case "$1" in
        "$MACHINE_ROOT"|"$MACHINE_ROOT"/*) ;;
        *) return 1 ;;
    esac
    local parent
    parent="$(dirname -- "$1")"
    [ "$(realpath -e -- "$parent")" = "$parent" ]
}

if [ "$ACTION" = "rollback" ] || [ "$ACTION" = "--rollback" ]; then
    echo "=== Rolling back permissions and ACLs on machine root: $MACHINE_ROOT ==="
    [ -d "$MACHINE_ROOT" ] || die "machine root does not exist: $MACHINE_ROOT"
    [ -f "$STAT_BACKUP" ] || die "no metadata backup in $BACKUP_DIR; refusing to guess original ownership and modes"

    failed=0

    echo "Restoring ownership and mode bits from $STAT_BACKUP..."
    while read -r mode uid gid file; do
        [ -e "$file" ] || [ -L "$file" ] || continue
        if ! inside_machine_root "$file"; then
            echo "Error: backup entry outside the machine root: $file" >&2
            failed=1
            continue
        fi
        chown -h "$uid:$gid" "$file" || failed=1
        [ -L "$file" ] || chmod "$mode" "$file" || failed=1
    done < "$STAT_BACKUP"

    if [ -f "$ACL_BACKUP" ]; then
        command -v setfacl >/dev/null 2>&1 || die "setfacl is required to restore $ACL_BACKUP"
        echo "Restoring exact POSIX ACLs from $ACL_BACKUP..."
        block=""
        restore_acl_block()
        {
            [ -n "$block" ] || return 0
            local path="${block%%$'\n'*}"
            path="${path#\# file: }"
            if [ ! -e "$path" ]; then block=""; return 0; fi
            if [ "$(realpath -e -- "$path" 2>/dev/null)" != "$path" ] || ! inside_machine_root "$path"; then
                echo "Error: ACL backup entry outside the machine root or not canonical: $path" >&2
                failed=1
            else
                printf '%s' "$block" | setfacl --restore=- || failed=1
            fi
            block=""
        }
        while IFS= read -r line || [ -n "$line" ]; do
            if [ -z "$line" ]; then restore_acl_block; else block+="$line"$'\n'; fi
        done < "$ACL_BACKUP"
        restore_acl_block
    fi

    if [ "$failed" -ne 0 ]; then
        echo "=== Rollback FAILED; backups kept in $BACKUP_DIR ===" >&2
        exit 1
    fi
    rm -f -- "$ACL_BACKUP" "$STAT_BACKUP"
    echo "=== Rollback completed successfully ==="
    exit 0
fi

# Capture metadata before ANY change to the machine root (the first backup wins).
if [ -d "$MACHINE_ROOT" ] && [ ! -f "$STAT_BACKUP" ]; then
    echo "Capturing filesystem metadata before permission changes..."
    if [ -n "$(find "$MACHINE_ROOT" \( -name $'*\n*' -o -name '*\\*' \) -print -quit)" ]; then
        die "machine root contains file names with newline or backslash; cannot capture exact metadata"
    fi
    find "$MACHINE_ROOT" -exec stat -c "%a %u %g %n" {} + > "$STAT_BACKUP.tmp"
    mv -- "$STAT_BACKUP.tmp" "$STAT_BACKUP"
    if command -v getfacl >/dev/null 2>&1; then
        echo "Capturing exact POSIX ACLs before permission changes..."
        getfacl -R -p "$MACHINE_ROOT" > "$ACL_BACKUP.tmp"
        mv -- "$ACL_BACKUP.tmp" "$ACL_BACKUP"
    fi
fi
trap 'echo "Setup failed; run \"$0 rollback\" to restore the captured metadata." >&2' ERR

echo "=== WineExplorer S2 Machine Root Setup ==="
echo "Machine root: $MACHINE_ROOT"
echo "Template prefix: $SKEL_PFX"
echo "Machine group: $MACHINE_GROUP"

ensure_machine_prefix_links()
{
    mkdir -p "$MACHINE_ROOT/dosdevices" "$MACHINE_ROOT/drive_c/users/root"
    ln -sfn ../drive_c "$MACHINE_ROOT/dosdevices/c:"
    rm -f "$MACHINE_ROOT/dosdevices/z:"
    ln -s / "$MACHINE_ROOT/dosdevices/z:"
    chown "root:$MACHINE_GROUP" "$MACHINE_ROOT"
}

ensure_common_shell_dirs()
{
    mkdir -p "$MACHINE_ROOT/drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs/StartUp"
    mkdir -p "$MACHINE_ROOT/drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs/Administrative Tools"
    mkdir -p "$MACHINE_ROOT/drive_c/ProgramData/Microsoft/Windows/Templates"
    mkdir -p "$MACHINE_ROOT/drive_c/users/Public/Desktop"
    mkdir -p "$MACHINE_ROOT/drive_c/users/Public/Documents"
    mkdir -p "$MACHINE_ROOT/drive_c/users/Public/Favorites"
    mkdir -p "$MACHINE_ROOT/drive_c/users/Public/Music"
    mkdir -p "$MACHINE_ROOT/drive_c/users/Public/Pictures"
    mkdir -p "$MACHINE_ROOT/drive_c/users/Public/Videos"
    # The machine user.reg is the setup prefix's, so session 0's HKCU\Environment puts TEMP and TMP under the setup
    # account's profile. Without that directory no Wine program of the system group can make a temporary file:
    # ole32's StgCreateDocfile(NULL) fails with STG_E_INSUFFICIENTMEMORY, and msiexec (it opens a transacted copy of
    # the package) ends in its first action with 1627 and an empty log (KNOWN_BUGS B54).
    mkdir -p "$MACHINE_ROOT/drive_c/users/wexsetup/AppData/Local/Temp"
}

WINE_BIN="${WEX_WINE_BIN:-/usr/local/bin/wine}"
WINESERVER_BIN="${WEX_WINESERVER_BIN:-/usr/local/bin/wineserver}"

# KNOWN_BUGS B81: an update (the package postinst) changes the machine registry below (wineboot -u, the .reg imports, the
# elevation value). While session 0 runs, its server holds that hive: a second server of root's on the same root wrote
# the file behind it, session 0 merged the file once at most and then saved its own tree over the changes (measured:
# the update's EnablePrefetcher=3 was gone after session 0's next save). Root's own server also cannot open the
# machine keys at all once session 0 has given them their descriptors. So on a machine where session 0 runs, it is
# restarted first (it saves the hive when it stops, and runs the new Wine afterwards: an upgraded Wine client cannot
# talk to the old server) and every Wine step of the update runs as session 0's account, as a client of session 0,
# as component installs do (MachineWine in wex-components). Its changes are then in session 0's own tree, which saves
# them. Sessions that are logged in need a new logon after a Wine upgrade anyway.
S0_CLIENT=0
S0_WINE_ACCOUNT="${WEX_SESSION0_USER:-wexsys}"
if [ -d "$MACHINE_ROOT/drive_c/windows/system32" ] && [ -d /run/systemd/system ] &&
   systemctl is-active --quiet wex-session0.service 2>/dev/null; then
    id "$S0_WINE_ACCOUNT" >/dev/null 2>&1 || die "session 0 runs, but its account $S0_WINE_ACCOUNT does not exist"
    echo "Restarting session 0 for the update; the machine registry is changed through it..."
    systemctl restart wex-session0.service || die "session 0 did not restart"
    systemctl start wex-session0-services.service || die "session 0's services did not start"
    S0_CLIENT=1
    # a home of its own for the account's Wine steps: caches (Mesa's shader cache, ...) must not land in the machine
    # root, whose owners the permission pass checks below
    S0_WINE_HOME="$(mktemp -d /tmp/wex-update-home.XXXXXX)"
    chown "$S0_WINE_ACCOUNT": "$S0_WINE_HOME"
    trap 'rm -rf "$S0_WINE_HOME"' EXIT
fi

# machine_wine [VAR=VALUE...] PROGRAM [ARGS...]: Wine on the machine root, through session 0 when it runs
machine_wine()
{
    local vars=()
    while [ $# -gt 0 ] && [[ "$1" == *=* ]]; do vars+=("$1"); shift; done
    if [ "$S0_CLIENT" = 1 ]; then
        runuser -u "$S0_WINE_ACCOUNT" -- env -i HOME="$S0_WINE_HOME" WINEPREFIX="$MACHINE_ROOT" PATH=/usr/local/bin:/usr/bin:/bin \
            LANG=C.UTF-8 WINEDEBUG=-all WINEDLLOVERRIDES=winemenubuilder.exe=d "${vars[@]}" "$WINE_BIN" "$@"
    else
        env WINEPREFIX="$MACHINE_ROOT" WINEDLLOVERRIDES=winemenubuilder.exe=d "${vars[@]}" "$WINE_BIN" "$@"
    fi
}

# machine_wine_wait: until the Wine steps are done (root's own server ends; session 0's never does, its clients have)
machine_wine_wait()
{
    [ "$S0_CLIENT" = 1 ] || WINEPREFIX="$MACHINE_ROOT" "$WINESERVER_BIN" -w
}

# 1. Initialize or update machine root
if [ ! -d "$MACHINE_ROOT/drive_c/windows/system32" ]; then
    if [ -d "$SKEL_PFX/drive_c/windows" ] && [ ! -L "$SKEL_PFX/drive_c/windows" ]; then
        echo "Promoting existing prefix $SKEL_PFX into machine root $MACHINE_ROOT..."
        ensure_machine_prefix_links
        mv "$SKEL_PFX/drive_c/windows" "$MACHINE_ROOT/drive_c/"
        mv "$SKEL_PFX/drive_c/Program Files" "$MACHINE_ROOT/drive_c/"
        mv "$SKEL_PFX/drive_c/Program Files (x86)" "$MACHINE_ROOT/drive_c/"
        ensure_common_shell_dirs
        if [ -d "$SKEL_PFX/drive_c/ProgramData" ] && [ ! -L "$SKEL_PFX/drive_c/ProgramData" ]; then
            cp -a "$SKEL_PFX/drive_c/ProgramData"/. "$MACHINE_ROOT/drive_c/ProgramData/"
            rm -rf "$SKEL_PFX/drive_c/ProgramData"
        fi
        if [ -d "$SKEL_PFX/drive_c/users/Public" ] && [ ! -L "$SKEL_PFX/drive_c/users/Public" ]; then
            cp -a "$SKEL_PFX/drive_c/users/Public"/. "$MACHINE_ROOT/drive_c/users/Public/"
            rm -rf "$SKEL_PFX/drive_c/users/Public"
        fi
        [ ! -f "$SKEL_PFX/system.reg" ] || cp "$SKEL_PFX/system.reg" "$MACHINE_ROOT/"
        [ ! -f "$SKEL_PFX/userdef.reg" ] || cp "$SKEL_PFX/userdef.reg" "$MACHINE_ROOT/"
        [ ! -f "$SKEL_PFX/user.reg" ] || cp "$SKEL_PFX/user.reg" "$MACHINE_ROOT/"
        [ ! -f "$SKEL_PFX/.update-timestamp" ] || cp "$SKEL_PFX/.update-timestamp" "$MACHINE_ROOT/"
    else
        echo "Bootstrapping machine root at $MACHINE_ROOT..."
        # never wipe an existing machine root: it holds the programs installed for all users
        if [ -e "$MACHINE_ROOT" ] && [ -n "$(ls -A "$MACHINE_ROOT" 2>/dev/null)" ]; then
            echo "$MACHINE_ROOT exists but has no drive_c/windows/system32; refusing to replace it" >&2
            exit 1
        fi

        if [ "${WEX_SKIP_WINEBOOT:-0}" != "1" ]; then
            WINELINKFAKEDLLS=1 WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all \
                WINEPREFIX="$MACHINE_ROOT" "$WINE_BIN" wineboot.exe -i
            WINEPREFIX="$MACHINE_ROOT" "$WINESERVER_BIN" -w
        fi
        ensure_common_shell_dirs
    fi
else
    echo "Updating existing machine root at $MACHINE_ROOT..."
    ensure_machine_prefix_links
    if [ "${WEX_SKIP_WINEBOOT:-0}" != "1" ]; then
        machine_wine WINELINKFAKEDLLS=1 "WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all \
            wineboot.exe -u
        machine_wine_wait
    fi
    ensure_common_shell_dirs
fi

# Install themes into machine root
THEME_DATA_DIR="${WEX_THEME_DATA_DIR:-/usr/local/share/wineexplorer/themes}"
if [ -d "$THEME_DATA_DIR" ]; then
    mkdir -p "$MACHINE_ROOT/drive_c/windows/Resources/Themes"
    for theme_file in "$THEME_DATA_DIR/"*.theme; do
        [ -f "$theme_file" ] || continue
        cp -f "$theme_file" "$MACHINE_ROOT/drive_c/windows/Resources/Themes/" 2>/dev/null || true
    done
    for theme_dir in "$THEME_DATA_DIR/"*/; do
        [ -d "$theme_dir" ] || continue
        tname="$(basename "$theme_dir")"
        tdest="$MACHINE_ROOT/drive_c/windows/Resources/Themes/$tname"
        # -T: an existing destination is the target itself, else --update copies into $tname/$tname
        cp -rfT "$theme_dir" "$tdest" 2>/dev/null || true
        # remove the copy nested by earlier --update runs
        [ -e "$theme_dir$tname" ] || rm -rf "$tdest/$tname"
    done
fi

# On developer machines a local genuine Luna (WEX_LOCAL_LUNA, never shipped) overrides the open style
if [ -n "${WEX_LOCAL_LUNA:-}" ] && [ -f "$WEX_LOCAL_LUNA" ]; then
    mkdir -p "$MACHINE_ROOT/drive_c/windows/Resources/Themes/Luna"
    cp -f "$WEX_LOCAL_LUNA" "$MACHINE_ROOT/drive_c/windows/Resources/Themes/Luna/Luna.msstyles" 2>/dev/null || true
fi

# Reapply our own font mappings on every update, without changing Wine's Tahoma.
FONT_RES_DIR="${WEX_FONT_RES_DIR:-/usr/local/share/wineexplorer/fonts}"
if [ ! -d "$FONT_RES_DIR" ]; then
    FONT_RES_DIR="$(dirname "${BASH_SOURCE[0]}")/../src/resources/fonts"
fi
[ -f "$FONT_RES_DIR/font-substitutes.reg" ] && [ -f "$FONT_RES_DIR/font-links.txt" ] || {
    echo "Required open font resources missing: $FONT_RES_DIR" >&2
    exit 1
}
if [ "${WEX_SKIP_WINEBOOT:-0}" != "1" ]; then
    machine_wine regedit.exe /S "$FONT_RES_DIR/font-substitutes.reg"
    machine_wine_wait
fi
mkdir -p "$MACHINE_ROOT/drive_c/windows/Fonts"
while read -r font_name font_target; do
    [[ -n "$font_name" && "$font_name" != \#* ]] || continue
    [[ "$font_name" != */* && "$font_target" == /* ]] || { echo "Invalid font mapping" >&2; exit 1; }
    if [ -f "$font_target" ]; then
        if [ ! -f "$MACHINE_ROOT/drive_c/windows/Fonts/$font_name" ] || [ -L "$MACHINE_ROOT/drive_c/windows/Fonts/$font_name" ]; then
            ln -sfn "$font_target" "$MACHINE_ROOT/drive_c/windows/Fonts/$font_name"
        fi
    else
        echo "Missing open font target (skipped): $font_target" >&2
    fi
done < "$FONT_RES_DIR/font-links.txt"
# C:\Windows\Prefetch shows the .pf files wexprefetchd writes (read-only for users), and the XP value that says
# the prefetcher is on is set (3 = applications and boot).
PREFETCH_DIR="${WEX_PREFETCH_DIR:-/var/cache/wineexplorer/prefetch}"
mkdir -p "$PREFETCH_DIR" 2>/dev/null || true
if [ ! -e "$MACHINE_ROOT/drive_c/windows/Prefetch" ] || [ -L "$MACHINE_ROOT/drive_c/windows/Prefetch" ]; then
    ln -sfn "$PREFETCH_DIR" "$MACHINE_ROOT/drive_c/windows/Prefetch"
fi
if [ "${WEX_SKIP_WINEBOOT:-0}" != "1" ]; then
    PREFETCH_REG="$(mktemp)"
    cat > "$PREFETCH_REG" <<'PFREG'
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Memory Management\PrefetchParameters]
"EnablePrefetcher"=dword:00000003
PFREG
    chmod 644 "$PREFETCH_REG"   # session 0's account reads it (it holds nothing secret)
    machine_wine regedit.exe /S "$PREFETCH_REG"
    machine_wine_wait
    rm -f "$PREFETCH_REG"
fi
if [ -f /usr/share/applications/wex-components.desktop ]; then
    install -m644 /usr/share/applications/wex-components.desktop \
        "$MACHINE_ROOT/drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs/Administrative Tools/Windows Components.desktop"
fi

# 2. Ensure ownership, permissions and POSIX default ACLs on machine root. This also migrates an existing
# root:sudo machine root (setup --update): everything below the root is made root:$MACHINE_GROUP, files that a
# desktop administrator created or owns included, and every extended ACL entry (g:sudo:rwX, user entries) is
# dropped before the entries for $MACHINE_GROUP are set. Running it again changes nothing.
# Result: root:$MACHINE_GROUP, setgid 2775 on dirs, 664 on regular files, 775 on .exe/.dll, and default ACL
# g:$MACHINE_GROUP:rwX so files created with the effective group $MACHINE_GROUP (wex-sysrun) stay group-writable
# regardless of the umask. No desktop user is in $MACHINE_GROUP, so desktop users cannot write the machine root.
OLD_GROUP="$(stat -c '%G' -- "$MACHINE_ROOT")"
if [ "$OLD_GROUP" != "$MACHINE_GROUP" ]; then
    echo "Migrating machine root from group $OLD_GROUP to $MACHINE_GROUP..."
fi
# The hives are edited below (sed, the elevation value) and their permissions set by root: a link in their place (a
# member of the system group can write the top of the machine root) is refused, so that root never writes or changes
# the file it points to.
for f in system.reg userdef.reg user.reg; do
    [ ! -L "$MACHINE_ROOT/$f" ] || die "$MACHINE_ROOT/$f is a symbolic link"
done
if [ -f "$MACHINE_ROOT/system.reg" ]; then
    # (an old migration of root's profile path: only while no server holds the hive, B81; session 0 machines are newer)
    [ "$S0_CLIENT" = 1 ] ||
        sed -i 's/\\\\users\\\\root\\\\Favorites/\\\\users\\\\Public\\\\Favorites/g' "$MACHINE_ROOT/system.reg"
    # Enforced elevation: the helper's name in the machine hive is the switch (the servers and ntdll read
    # HKLM\Software\Wine\Elevation, value Helper). The hive file is written only while no server has it: at
    # the first setup none runs yet; later, session 0 holds it and the value is set through session 0 instead.
    if [ "$ELEVATION_ENABLE" = 1 ] && ! grep -q '^\[Software\\\\Wine\\\\Elevation\]' "$MACHINE_ROOT/system.reg"; then
        if [ "$S0_CLIENT" = 1 ]; then
            # B81: through session 0, which holds the hive
            machine_wine reg.exe add 'HKLM\Software\Wine\Elevation' /v Helper /t REG_SZ \
                /d 'C:\windows\system32\consent.exe' /f > /dev/null || die "the elevation value was not set through session 0"
        elif pgrep -f 'wineserver --session0' >/dev/null 2>&1; then
            echo "NOTE: session 0 is running; set HKLM\\Software\\Wine\\Elevation\\Helper through it to enforce elevation" >&2
        else
            cat >> "$MACHINE_ROOT/system.reg" <<'ELEVATION_EOF'

[Software\\Wine\\Elevation]
"Helper"="C:\\windows\\system32\\consent.exe"
ELEVATION_EOF
        fi
    fi
fi
# Root changes owner, mode and ACL of every file below. Users can write parts of the tree while this runs (the shared
# folders are world-writable, the rest is the system group's), so every change is made on a file descriptor opened
# without following links, in directories opened the same way (os.fwalk): a name swapped for a symbolic link between
# a check and a change cannot send root's change to the link's target (the earlier find/setfacl passes could), and
# names are taken as they are (spaces, newlines).
# chown, chmod and ACLs act on the inode, so a second name (hard link) of a file outside the machine root would hand
# that file to root:$MACHINE_GROUP: every name of a file that has a name outside goes (in drive_c and pe_section_cache;
# such a name anywhere else refuses the setup before anything is changed). Files whose names are all inside are left
# as they are. A name added while a file is changed undoes that file's change and stops the setup.
# fs.protected_hardlinks (Debian's default) keeps users from linking files they cannot write; with it off the setup
# only warns.
# The shared document folders (Shared Documents, Music, Video and Pictures of Windows XP) are every user's to
# write, and a user's files there are that user's to delete: world-writable with the sticky bit, like /tmp.
# C:\users\Public itself and the common desktop and favourites stay machine state, as on XP, where only
# administrators change what every user sees. The folders' XP names are set here (desktop.ini, created only where none
# exists, never through a link), because a user's shell cannot be relied on to write them at first logon (they are
# shell32 strings; the shell looks for the same value and leaves an existing one alone).
# user.reg is session 0's own HKCU (the system account's settings, such as its Windows version): session 0 runs as
# the system group and saves it like the machine hives. Read-only, the server dropped every change at the next
# save (B19).
[ "$(cat /proc/sys/fs/protected_hardlinks 2>/dev/null || echo 1)" = 1 ] ||
    echo "Warning: fs.protected_hardlinks is off; users can plant links to any file in the shared folders" >&2
echo "Applying permissions and default ACLs to machine root (root:$MACHINE_GROUP, mode 2775, default ACL g:$MACHINE_GROUP:rwX)..."
python3 -I - "$MACHINE_ROOT" "$MACHINE_GROUP" <<'FIXPERMS' || die "the permissions of the machine root were not set"
import ctypes, errno, grp, os, stat, struct, sys

root, group = sys.argv[1], sys.argv[2]
GID = grp.getgrnam(group).gr_gid
HIVES = ('system.reg', 'userdef.reg', 'user.reg')
TREES = ('drive_c', 'pe_section_cache')
SHARED = {'drive_c/users/Public/' + name: rid for name, rid in
          (('Documents', 21785), ('Music', 29059), ('Videos', 29060), ('Pictures', 29061))}
ACCESS, DEFAULT = 'system.posix_acl_access', 'system.posix_acl_default'
UNDEFINED = 0xFFFFFFFF
acl_ok = True
inside = {}
libc = ctypes.CDLL(None, use_errno=True)


def show(rel):
    path = os.path.join(root, rel) if rel != '.' else root
    return path if path.isprintable() and path == path.strip() else ascii(path)


def die(message):
    print('Error: ' + message, file=sys.stderr)
    sys.exit(1)


def acl(entries):
    # POSIX ACL xattr: version 2, then (tag, permissions, id) sorted by tag (USER_OBJ 1, GROUP_OBJ 4, GROUP 8, MASK 16,
    # OTHER 32)
    return struct.pack('<I', 2) + b''.join(struct.pack('<HHI', *entry) for entry in sorted(entries))


def access_acl(mode, named):
    group_bits = (mode >> 3) & 7
    return acl([(1, (mode >> 6) & 7, UNDEFINED), (4, group_bits, UNDEFINED), (8, named, GID),
                (16, group_bits | named, UNDEFINED), (32, mode & 7, UNDEFINED)])


DEFAULT_ACL = acl([(1, 7, UNDEFINED), (4, 7, UNDEFINED), (8, 7, GID), (16, 7, UNDEFINED), (32, 5, UNDEFINED)])


def strip(path, directory):
    for name in (ACCESS, DEFAULT) if directory else (ACCESS,):
        try:
            os.removexattr(path, name)
        except OSError as error:
            if error.errno not in (errno.ENODATA, errno.EOPNOTSUPP):
                raise


def set_acl(path, data, name=ACCESS):
    global acl_ok
    if not acl_ok:
        return
    try:
        os.setxattr(path, name, data)
    except OSError as error:
        if error.errno != errno.EOPNOTSUPP:
            raise
        acl_ok = False
        print('Warning: the file system of the machine root has no POSIX ACLs', file=sys.stderr)


def fix(dirfd, name, rel, kind):
    """kind: root, hive, dir, file (owner, mode, ACL) or chown (owner only)."""
    fd = os.open(name, os.O_PATH | os.O_NOFOLLOW, dir_fd=dirfd)
    try:
        st = os.fstat(fd)
        if stat.S_ISLNK(st.st_mode):
            # the link itself (as chown -h did; never its target): fchownat(fd, "", AT_EMPTY_PATH)
            if kind != 'hive' and libc.fchownat(fd, b'', 0, GID, 0x1000) != 0:
                error = ctypes.get_errno()
                raise OSError(error, os.strerror(error), show(rel))
            return
        # The descriptor's magic link: chown, chmod and the xattr calls act on the opened inode, whatever the name is now.
        path = '/proc/self/fd/%d' % fd
        regular, directory = stat.S_ISREG(st.st_mode), stat.S_ISDIR(st.st_mode)
        if regular and st.st_nlink > 1 and inside.get((st.st_dev, st.st_ino), 0) < st.st_nlink:
            if kind == 'hive' or rel.split('/')[0] not in TREES:
                die(show(rel) + ' has a name outside the machine root')
            os.unlink(name, dir_fd=dirfd)
            print('  removed (a second name of a file outside the machine root): ' + show(rel))
            return
        if kind == 'chown' or not (regular or directory):
            os.chown(path, 0, GID)
            return
        if directory:
            mode = 0o1777 if rel in SHARED else 0o2775
            named = 7
        elif kind == 'hive':
            mode, named = 0o664, 6
        else:
            mode = 0o775 if name.lower().endswith(('.exe', '.dll')) else 0o664
            named = 7 if mode & 0o111 else 6
        strip(path, directory)
        os.chown(path, 0, GID)
        os.chmod(path, mode)
        set_acl(path, access_acl(mode, named))
        if directory:
            set_acl(path, DEFAULT_ACL, DEFAULT)
        if regular and os.fstat(fd).st_nlink > st.st_nlink:
            os.chown(path, st.st_uid, st.st_gid)
            os.chmod(path, stat.S_IMODE(st.st_mode))
            strip(path, False)
            die(show(rel) + ' got another name while its permissions were set; run the setup again')
    finally:
        os.close(fd)


def desktop_ini(dirfd, rel):
    text = '[.ShellClassInfo]\r\nLocalizedResourceName=@%%SystemRoot%%\\system32\\shell32.dll,-%d\r\n' % SHARED[rel]
    try:
        fd = os.open('desktop.ini', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o664, dir_fd=dirfd)
    except FileExistsError:
        return
    try:
        os.write(fd, text.encode('ascii'))
    finally:
        os.close(fd)
    fix(dirfd, 'desktop.ini', rel + '/desktop.ini', 'file')


rootfd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
# Pass 1, nothing changed: count the names inside of every file with more than one, refuse what cannot be removed.
multiple = []
for dirpath, dirnames, filenames, dirfd in os.fwalk('.', dir_fd=rootfd):
    for name in filenames:
        st = os.stat(name, dir_fd=dirfd, follow_symlinks=False)
        if stat.S_ISREG(st.st_mode) and st.st_nlink > 1:
            key = (st.st_dev, st.st_ino)
            inside[key] = inside.get(key, 0) + 1
            multiple.append((os.path.normpath(os.path.join(dirpath, name)), key, st.st_nlink))
for rel, key, links in multiple:
    if inside[key] < links and (rel.split('/')[0] not in TREES or '/' not in rel):
        die(show(rel) + ' has a name outside the machine root')
for hive in HIVES:
    try:
        st = os.stat(hive, dir_fd=rootfd, follow_symlinks=False)
    except FileNotFoundError:
        continue
    if not stat.S_ISREG(st.st_mode):
        die(show(hive) + ' is not a regular file')
# Pass 2: the changes.
fix(rootfd, '.', '.', 'root')
for name in sorted(os.listdir(rootfd)):
    st = os.stat(name, dir_fd=rootfd, follow_symlinks=False)
    if stat.S_ISLNK(st.st_mode):
        continue
    if name in HIVES:
        fix(rootfd, name, name, 'hive')
    elif name in TREES and stat.S_ISDIR(st.st_mode):
        full = name == 'drive_c'
        for dirpath, dirnames, filenames, dirfd in os.fwalk(name, dir_fd=rootfd):
            rel = os.path.normpath(dirpath)
            fix(dirfd, '.', rel, 'dir' if full else 'chown')
            if full and rel in SHARED:
                desktop_ini(dirfd, rel)
            for entry in dirnames:
                # a link to a directory is listed with the directories and not entered: its owner only
                if stat.S_ISLNK(os.stat(entry, dir_fd=dirfd, follow_symlinks=False).st_mode):
                    fix(dirfd, entry, rel + '/' + entry, 'chown')
            for entry in filenames:
                fix(dirfd, entry, rel + '/' + entry, 'file' if full else 'chown')
    else:
        fix(rootfd, name, name, 'chown')
FIXPERMS
# Nothing may remain that a desktop user owns (an owner can always write), and nothing may have another group.
LEFTOVER="$(find "$MACHINE_ROOT" ! -type l \( ! -user root -o ! -group "$MACHINE_GROUP" \) -print -quit)"
[ -z "$LEFTOVER" ] || die "migration incomplete: $LEFTOVER is not root:$MACHINE_GROUP"

# 2b. Install the launcher wex-sysrun (tools/wex-sysrun) root:$MACHINE_GROUP, mode 2755, in a root-owned
# directory, with its root-owned configuration of fixed paths. ON BY DEFAULT: the constraints of
# ELEVATION_SYSTEM_ROLE_WORKORDER.md (phase C) are met and validated (ELEVATION_LOCAL_VALIDATION_WORKORDER.md).
# WEX_SYSRUN_ENABLE=0 opts out. WEX_SYSRUN_BIN installs a prebuilt binary instead of building from the source next
# to this script; a packaged setup finds the binary wex-sysrun.bin next to itself.
if [ "${WEX_SYSRUN_ENABLE:-1}" != "0" ]; then
    SYSRUN_DIR="${WEX_SYSRUN_DIR:-/usr/local/libexec/wineexplorer}"
    SYSRUN_CONF="${WEX_SYSRUN_CONF:-/etc/wineexplorer/sysrun.conf}"
    SYSRUN_SRC="$(dirname -- "$(readlink -f -- "$0")")/../tools/wex-sysrun/wex-sysrun.c"
    SYSRUN_TMP="$(mktemp -d)"
    SYSRUN_PKG="$(dirname -- "$(readlink -f -- "$0")")/wex-sysrun.bin"
    [ -n "${WEX_SYSRUN_BIN:-}" ] || [ ! -f "$SYSRUN_PKG" ] || WEX_SYSRUN_BIN="$SYSRUN_PKG"
    if [ -n "${WEX_SYSRUN_BIN:-}" ]; then
        cp -- "$WEX_SYSRUN_BIN" "$SYSRUN_TMP/wex-sysrun"
    elif [ -f "$SYSRUN_SRC" ] && command -v cc >/dev/null 2>&1; then
        cc -O2 -Wall -fPIE -pie -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wl,-z,relro,-z,now \
            -DSYSRUN_CONF="\"$SYSRUN_CONF\"" -o "$SYSRUN_TMP/wex-sysrun" "$SYSRUN_SRC"
    else
        echo "Warning: no wex-sysrun source or compiler; the launcher is not installed" >&2
    fi
    if [ -f "$SYSRUN_TMP/wex-sysrun" ]; then
        echo "Installing wex-sysrun to $SYSRUN_DIR (root:$MACHINE_GROUP, mode 2755), configuration $SYSRUN_CONF..."
        install -d -m 0755 -o root -g root -- "$SYSRUN_DIR" "$(dirname -- "$SYSRUN_CONF")"
        # Install the unprivileged policy reader before preserving any old configuration.
        IS_ADMIN_SOURCE="$(dirname -- "$(readlink -f -- "$0")")/../tools/wex-is-admin/wex-is-admin.c"
        IS_ADMIN_PKG="$(dirname -- "$(readlink -f -- "$0")")/wex-is-admin.bin"
        if [ -f "$IS_ADMIN_PKG" ]; then
            install -m 0755 -o root -g root -- "$IS_ADMIN_PKG" "$SYSRUN_DIR/wex-is-admin"
        elif [ -f "$IS_ADMIN_SOURCE" ] && command -v cc >/dev/null 2>&1; then
            cc -O2 -Wall -Wextra -fPIE -pie -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wl,-z,relro,-z,now \
                -o "$SYSRUN_TMP/wex-is-admin" "$IS_ADMIN_SOURCE"
            install -m 0755 -o root -g root -- "$SYSRUN_TMP/wex-is-admin" "$SYSRUN_DIR/wex-is-admin"
        fi
        # Preserve the existing protected administrator-group and built-in Administrator choices on update.
        # WEX_BUILTIN_ADMINISTRATOR=NAME names the account that wex-accounts maps with the reserved RID 500.
        ADMIN_GROUP="${WEX_ADMIN_GROUP:-sudo}"
        # Default: the package's account "administrator" (Safe Mode C4); WEX_BUILTIN_ADMINISTRATOR= (empty) names none.
        # A name that is not a local account maps nothing (docs/E1_S4_ACCOUNTS_MAP.md).
        BUILTIN_ADMIN="${WEX_BUILTIN_ADMINISTRATOR-administrator}"
        if [ -e "$SYSRUN_CONF" ] || [ -L "$SYSRUN_CONF" ]; then
            [ -x "$SYSRUN_DIR/wex-is-admin" ] || die "cannot preserve sysrun config without wex-is-admin"
            existing_admin_group=$("$SYSRUN_DIR/wex-is-admin" --print-group "$SYSRUN_CONF") ||
                die "invalid or unprotected sysrun config"
            [ -n "${WEX_ADMIN_GROUP:-}" ] || ADMIN_GROUP="$existing_admin_group"
            # The shared reader has validated every line and refused all duplicate keys.
            while IFS= read -r config_line; do
                case "$config_line" in
                    builtin_administrator=*) [ -n "${WEX_BUILTIN_ADMINISTRATOR:-}" ] || BUILTIN_ADMIN="${config_line#builtin_administrator=}" ;;
                    session0_socket=*) [ -n "${WEX_SESSION0:-}" ] || SESSION0_ENABLE=1 ;;
                esac
            done < "$SYSRUN_CONF"
        fi
        # First setup has no old configuration to read: use the defaults or explicit root choices above.
        # A coarse check here; wex-accounts and the server apply the exact account-name grammar.
        case "$BUILTIN_ADMIN" in *[!A-Za-z0-9_.\$-]*|-*) die "invalid built-in Administrator account name: $BUILTIN_ADMIN" ;; esac
        install -m 2755 -o root -g "$MACHINE_GROUP" -- "$SYSRUN_TMP/wex-sysrun" "$SYSRUN_DIR/wex-sysrun"
        if [ "$SESSION0_ENABLE" = "1" ] && ! getent passwd wexsys >/dev/null 2>&1; then
            echo "Creating system account wexsys..."
            useradd --system --gid "$MACHINE_GROUP" --no-create-home --home-dir "$MACHINE_ROOT" --shell /usr/sbin/nologin wexsys
        fi
        cat > "$SYSRUN_TMP/sysrun.conf" <<EOF
# Written by setup-machine-root.sh. Read by wex-sysrun; it refuses a file that is not root-owned or that
# group or others can write.
group=$MACHINE_GROUP
loader=$WINE_BIN
ntdll=${WEX_NTDLL:-/usr/local/lib/wine/x86_64-unix/ntdll.so}
wineserver=$WINESERVER_BIN
machine_root=$MACHINE_ROOT
admin_group=$ADMIN_GROUP
EOF
        [ -z "$BUILTIN_ADMIN" ] || printf 'builtin_administrator=%s\n' "$BUILTIN_ADMIN" >> "$SYSRUN_TMP/sysrun.conf"
        if [ "$SESSION0_ENABLE" = "1" ]; then
            cat >> "$SYSRUN_TMP/sysrun.conf" <<EOF
session0_socket=/run/wineexplorer-session0/session0
session0_user=wexsys
EOF
        fi
        install -m 0644 -o root -g root -- "$SYSRUN_TMP/sysrun.conf" "$SYSRUN_CONF"
    fi
    rm -rf -- "$SYSRUN_TMP"
fi

# 2b. The local Group Policy store (docs/GROUP_POLICY.md, "Local policy", "Writing the local files"). Session 0's account
# is its only writer: gpsvc checks the requests that an elevated administrator's process leaves in the spool and
# installs them. The tree is therefore session 0's own and not group-writable, where the machine root's default ACL
# would make it writable by the whole system group, who could then put links in the way of gpsvc. Only the spool is
# group-writable. Runs at every setup, so an upgrade sets an existing tree right: links and special files in it are
# removed (none is legitimate), ACLs dropped, owner and modes set, stale requests removed.
S0_ACCOUNT="${WEX_SESSION0_USER:-wexsys}"   # (the test of this part uses a throw-away account)
if [ "${SESSION0_ENABLE:-0}" = "1" ] && id "$S0_ACCOUNT" >/dev/null 2>&1; then
    echo "Setting the local Group Policy stores (GroupPolicy and GroupPolicyUsers) to session 0's account..."
    # Root changes owner, mode and ACL of everything below the two folders, which sit in system32, where the whole system
    # group can write. So no change is made by a path: the folders and every entry are opened without following links
    # (os.fwalk, O_NOFOLLOW) and changed through the descriptor, as the permission pass above does it. A name that is
    # swapped for a link, or for another tree, while this runs cannot send a change anywhere else: what was opened is what
    # is changed, a link or any other thing that is not a plain file or directory is removed, and so is a file with
    # more than one name (chown and chmod act on the inode, so a second name planted elsewhere would hand that file to
    # session 0's account and make it readable by all). GroupPolicy/Spool is the system group's: emptied, and given
    # its own owner, mode and default ACL.
python3 -I - "$MACHINE_ROOT" "$MACHINE_GROUP" "$S0_ACCOUNT" <<'STORETREE' || die "the local Group Policy stores were not set"
import errno, grp, os, pwd, stat, struct, sys

root, group, account = sys.argv[1:4]
GID = grp.getgrnam(group).gr_gid
UID = pwd.getpwnam(account).pw_uid
ACCESS, DEFAULT = 'system.posix_acl_access', 'system.posix_acl_default'
UNDEFINED = 0xFFFFFFFF
FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC


def die(message):
    print('Error: ' + message, file=sys.stderr)
    sys.exit(1)


def acl(entries):
    # POSIX ACL xattr: version 2, then (tag, permissions, id) sorted by tag (USER_OBJ 1, GROUP_OBJ 4, GROUP 8, MASK 16,
    # OTHER 32)
    return struct.pack('<I', 2) + b''.join(struct.pack('<HHI', *entry) for entry in sorted(entries))


SPOOL_ACL = acl([(1, 7, UNDEFINED), (4, 7, UNDEFINED), (8, 7, GID), (16, 7, UNDEFINED), (32, 0, UNDEFINED)])


def strip(fd, directory):
    path = '/proc/self/fd/%d' % fd   # the descriptor's magic link: the call acts on the opened inode
    for name in (ACCESS, DEFAULT) if directory else (ACCESS,):
        try:
            os.removexattr(path, name)
        except OSError as error:
            if error.errno not in (errno.ENODATA, errno.EOPNOTSUPP):
                raise


def open_dir(name, dirfd, create=False):
    try:
        return os.open(name, FLAGS, dir_fd=dirfd)
    except FileNotFoundError:
        if not create:
            raise
        os.mkdir(name, 0o700, dir_fd=dirfd)
        return os.open(name, FLAGS, dir_fd=dirfd)
    except OSError as error:
        if error.errno in (errno.ELOOP, errno.ENOTDIR):
            die(name + ' is a link or not a directory')
        raise


def set_dir(fd, mode):
    os.fchown(fd, UID, GID)
    os.fchmod(fd, mode)
    strip(fd, True)


def remove_all(dirfd):
    """everything below the directory goes, nothing is followed"""
    for dirpath, dirnames, filenames, fd in os.fwalk('.', dir_fd=dirfd, topdown=False, follow_symlinks=False):
        for name in filenames + dirnames:
            try:
                os.unlink(name, dir_fd=fd)
            except IsADirectoryError:
                os.rmdir(name, dir_fd=fd)
            except PermissionError:
                os.rmdir(name, dir_fd=fd)


def fix_files(dfd, names):
    for name in names:
        try:
            st = os.stat(name, dir_fd=dfd, follow_symlinks=False)
            if stat.S_ISDIR(st.st_mode):
                continue   # fwalk goes into it
            fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC, dir_fd=dfd)
        except FileNotFoundError:
            continue
        except OSError as error:
            if error.errno != errno.ELOOP:
                raise
            os.unlink(name, dir_fd=dfd)   # a link, which O_NOFOLLOW would not open
            print('  removed (not a file or directory, or a file with more than one name): ' + name)
            continue
        try:
            st = os.fstat(fd)
            if not stat.S_ISREG(st.st_mode) or st.st_nlink != 1:
                os.unlink(name, dir_fd=dfd)
                print('  removed (not a file or directory, or a file with more than one name): ' + name)
                continue
            os.fchown(fd, UID, GID)
            os.fchmod(fd, 0o644)
            strip(fd, False)
        finally:
            os.close(fd)


system32 = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
for part in ('drive_c', 'windows', 'system32'):
    next_fd = open_dir(part, system32)
    os.close(system32)
    system32 = next_fd

for top in ('GroupPolicy', 'GroupPolicyUsers'):
    topfd = open_dir(top, system32, create=True)
    for dirpath, dirnames, filenames, dfd in os.fwalk('.', dir_fd=topfd, follow_symlinks=False):
        set_dir(dfd, 0o2755)
        fix_files(dfd, filenames + dirnames)
        if top == 'GroupPolicy' and dirpath == '.' and 'Spool' in dirnames:
            dirnames.remove('Spool')   # the system group's: below
    if top == 'GroupPolicy':
        spool = open_dir('Spool', topfd, create=True)
        remove_all(spool)
        os.fchown(spool, UID, GID)
        os.fchmod(spool, 0o2770)
        strip(spool, True)
        for name, data in ((ACCESS, SPOOL_ACL), (DEFAULT, SPOOL_ACL)):
            try:
                os.setxattr('/proc/self/fd/%d' % spool, name, data)
            except OSError as error:
                if error.errno != errno.EOPNOTSUPP:
                    raise
                print('Warning: the file system of the machine root has no POSIX ACLs', file=sys.stderr)
                break
        os.close(spool)
    os.close(topfd)
os.close(system32)
STORETREE
fi

# 2c. UWP frameworks installed before their records carried the classes of their in-process servers get the class
# lines added (a package that matches its receipt, a record in the old format; anything else is left alone and
# reported). It never stops the setup. The component tool only knows the default machine root.
COMPONENTS_BIN="${WEX_COMPONENTS_BIN:-/usr/local/bin/wex-components}"
if [ "$MACHINE_ROOT" = /var/lib/wineexplorer/machine ] && [ -x "$COMPONENTS_BIN" ]; then
    "$COMPONENTS_BIN" --refresh-uwp-frameworks || echo "Warning: the UWP frameworks records were not all refreshed" >&2
fi

# 3. Create or update template prefix in /etc/skel/.wine
echo "Configuring template prefix in $SKEL_PFX..."
mkdir -p "$SKEL_PFX/dosdevices" "$SKEL_PFX/drive_c/users"
ln -sfn ../drive_c "$SKEL_PFX/dosdevices/c:"
rm -f "$SKEL_PFX/dosdevices/z:"
ln -s / "$SKEL_PFX/dosdevices/z:"

# Symlink machine folders into drive_c
ln -sfn "$MACHINE_ROOT/drive_c/windows" "$SKEL_PFX/drive_c/windows"
ln -sfn "$MACHINE_ROOT/drive_c/Program Files" "$SKEL_PFX/drive_c/Program Files"
ln -sfn "$MACHINE_ROOT/drive_c/Program Files (x86)" "$SKEL_PFX/drive_c/Program Files (x86)"
ln -sfn "$MACHINE_ROOT/drive_c/ProgramData" "$SKEL_PFX/drive_c/ProgramData"
ln -sfn "$MACHINE_ROOT/drive_c/users/Public" "$SKEL_PFX/drive_c/users/Public"

# Link shared machine hives into template prefix (S3 shared HKLM/HKU.DEFAULT)
if [ -f "$MACHINE_ROOT/system.reg" ]; then
    ln -sfn "$MACHINE_ROOT/system.reg" "$SKEL_PFX/system.reg"
fi
if [ -f "$MACHINE_ROOT/userdef.reg" ]; then
    ln -sfn "$MACHINE_ROOT/userdef.reg" "$SKEL_PFX/userdef.reg"
fi
if [ ! -f "$SKEL_PFX/user.reg" ] && [ -f "$MACHINE_ROOT/user.reg" ]; then
    cp "$MACHINE_ROOT/user.reg" "$SKEL_PFX/user.reg"
    sed -i -e 's/\\\\users\\\\root\\\\/\\\\users\\\\wexsetup\\\\/g' \
           -e 's/\\\\users\\\\root"/\\\\users\\\\wexsetup"/g' "$SKEL_PFX/user.reg"
fi
if [ -f "$MACHINE_ROOT/.update-timestamp" ]; then
    cp -a "$MACHINE_ROOT/.update-timestamp" "$SKEL_PFX/.update-timestamp"
fi

# Clean up any leftover profile folders
rm -rf "$SKEL_PFX/drive_c/users/root" "$SKEL_PFX/drive_c/users/wexsetup"
chown -R root:root "$SKEL_PFX"
chmod 755 "$SKEL_PFX" "$SKEL_PFX/dosdevices" "$SKEL_PFX/drive_c" "$SKEL_PFX/drive_c/users"
chmod 644 "$SKEL_PFX/user.reg" "$SKEL_PFX/.update-timestamp" 2>/dev/null || true

# E1-S4: root-only identity authority, beside sysrun.conf. The machine root
# itself is group-writable and therefore cannot protect an authority file.
# This does not enable E1-S, advertise capabilities or change ordinary logon.
if [ "${WEX_SYSRUN_ENABLE:-1}" != "0" ]; then
    ACCOUNTS_SOURCE="$(dirname -- "$(readlink -f -- "$0")")/../tools/wex-accounts/wex-accounts.c"
    ACCOUNTS_PKG="$(dirname -- "$(readlink -f -- "$0")")/wex-accounts.bin"
    ACCOUNTS_TMP="$(mktemp -d)"
    if [ -n "${WEX_ACCOUNTS_BIN:-}" ]; then
        cp -- "$WEX_ACCOUNTS_BIN" "$ACCOUNTS_TMP/wex-accounts"
    elif [ -f "$ACCOUNTS_PKG" ]; then
        cp -- "$ACCOUNTS_PKG" "$ACCOUNTS_TMP/wex-accounts"
    elif [ -f "$ACCOUNTS_SOURCE" ] && command -v cc >/dev/null 2>&1; then
        cc -O2 -Wall -Wextra -fPIE -pie -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wl,-z,relro,-z,now \
            -o "$ACCOUNTS_TMP/wex-accounts" "$ACCOUNTS_SOURCE"
    else
        die "E1-S4 requires a prebuilt wex-accounts binary or its source and cc"
    fi
    install -m 0755 -o root -g root -- "$ACCOUNTS_TMP/wex-accounts" "$SYSRUN_DIR/wex-accounts"
    ACCOUNTS_ARGS=(--directory "$(dirname -- "$SYSRUN_CONF")" sync --initialize)
    # Bind the fixed old local_user_sid only on an explicit provisioning choice.
    # Existing multi-user legacy prefixes need separate migration/review.
    if [ -n "${WEX_ACCOUNTS_LEGACY_USER:-}" ] && [ ! -e "$(dirname -- "$SYSRUN_CONF")/accounts.map" ] && [ ! -e "$(dirname -- "$SYSRUN_CONF")/.accounts.initialized" ]; then
        ACCOUNTS_ARGS+=(--legacy-user "$WEX_ACCOUNTS_LEGACY_USER")
    fi
    if ! "$SYSRUN_DIR/wex-accounts" "${ACCOUNTS_ARGS[@]}"; then
        rm -rf -- "$ACCOUNTS_TMP"
        die "E1-S4 account map sync failed"
    fi
    rm -rf -- "$ACCOUNTS_TMP"
    if [ "$ACCOUNTS_AUTOSYNC" = 1 ] && [ "$SYSRUN_DIR" = /usr/local/libexec/wineexplorer ] && [ "$SYSRUN_CONF" = /etc/wineexplorer/sysrun.conf ] && [ -d /run/systemd/system ]; then
        ACCOUNTS_UNITS="$(dirname -- "$(readlink -f -- "$0")")/../installer/systemd"
        if [ -f "$ACCOUNTS_UNITS/wex-accounts.path" ]; then
            install -m 0644 -o root -g root "$ACCOUNTS_UNITS/wex-accounts.service" "$ACCOUNTS_UNITS/wex-accounts.path" "$ACCOUNTS_UNITS/wex-accounts.timer" /lib/systemd/system/
        fi
        systemctl daemon-reload
        systemctl enable --now wex-accounts.path wex-accounts.timer
    fi
    if [ "$SESSION0_ENABLE" = 1 ] && [ "$SYSRUN_DIR" = /usr/local/libexec/wineexplorer ] && [ "$SYSRUN_CONF" = /etc/wineexplorer/sysrun.conf ]; then
        # From a source checkout the units are installed from it; on an installed system the package has put
        # them in /lib/systemd/system already (this script is /usr/local/bin/wex-setup-machine.sh there).
        SESSION0_UNITS="$(dirname -- "$(readlink -f -- "$0")")/../installer/systemd"
        if [ -f "$SESSION0_UNITS/wex-session0.service" ]; then
            install -d -m 0755 /lib/systemd/system
            install -m 0644 -o root -g root "$SESSION0_UNITS/wex-session0.service" /lib/systemd/system/
            [ ! -f "$SESSION0_UNITS/wex-session0-services.service" ] ||
                install -m 0644 -o root -g root "$SESSION0_UNITS/wex-session0-services.service" /lib/systemd/system/
        fi
        if [ -f /lib/systemd/system/wex-session0.service ] && [ -d /run/systemd/system ]; then
            systemctl daemon-reload
            systemctl enable --now wex-session0.service
            # session 0's own services.exe (T5.3): the sessions forward the machine-wide services to it
            [ ! -f /lib/systemd/system/wex-session0-services.service ] ||
                systemctl enable --now wex-session0-services.service ||
                echo "WARNING: session 0's services did not start (wex-session0-services.service)" >&2
            # domain Group Policy (docs/GROUP_POLICY.md): the refresh does nothing on a machine that is not joined
            [ ! -f /lib/systemd/system/wex-gpupdate.timer ] || systemctl enable --now wex-gpupdate.timer || true
            # gpupdate's requests (gpsvc writes them in session 0's root; the unit names the default root)
            if [ -f /lib/systemd/system/wex-gpupdate.path ]; then
                if [ "$MACHINE_ROOT" != /var/lib/wineexplorer/machine ]; then
                    install -d -m 0755 /etc/systemd/system/wex-gpupdate.path.d
                    printf '[Path]\nPathChanged=\nPathChanged=%s/drive_c/windows/system32/GroupPolicy/Refresh\n' \
                        "$MACHINE_ROOT" > /etc/systemd/system/wex-gpupdate.path.d/machine-root.conf
                    systemctl daemon-reload
                fi
                systemctl enable --now wex-gpupdate.path || true
            fi
            # the domain join engine's socket (docs/DOMAIN_JOIN.md): the system group may connect (the unit names wexsys)
            if [ -f /lib/systemd/system/wex-domain-join.socket ]; then
                if [ "$MACHINE_GROUP" != wexsys ]; then
                    install -d -m 0755 /etc/systemd/system/wex-domain-join.socket.d
                    printf '[Socket]\nSocketGroup=%s\n' "$MACHINE_GROUP" > /etc/systemd/system/wex-domain-join.socket.d/group.conf
                    systemctl daemon-reload
                fi
                systemctl enable --now wex-domain-join.socket || true
            fi
        fi
    fi
fi

echo "=== S3 Machine Root Setup Completed Successfully ==="
