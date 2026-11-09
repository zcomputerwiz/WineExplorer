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
}

WINE_BIN="${WEX_WINE_BIN:-/usr/local/bin/wine}"
WINESERVER_BIN="${WEX_WINESERVER_BIN:-/usr/local/bin/wineserver}"

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
        WINELINKFAKEDLLS=1 WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all \
            WINEPREFIX="$MACHINE_ROOT" "$WINE_BIN" wineboot.exe -u
        WINEPREFIX="$MACHINE_ROOT" "$WINESERVER_BIN" -w
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
    WINEPREFIX="$MACHINE_ROOT" WINEDLLOVERRIDES=winemenubuilder.exe=d \
        "$WINE_BIN" regedit.exe /S "$FONT_RES_DIR/font-substitutes.reg"
    WINEPREFIX="$MACHINE_ROOT" "$WINESERVER_BIN" -w
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
    WINEPREFIX="$MACHINE_ROOT" WINEDLLOVERRIDES=winemenubuilder.exe=d \
        "$WINE_BIN" regedit.exe /S "$PREFETCH_REG"
    WINEPREFIX="$MACHINE_ROOT" "$WINESERVER_BIN" -w
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
echo "Applying permissions and default ACLs to machine root (root:$MACHINE_GROUP, mode 2775, default ACL g:$MACHINE_GROUP:rwX)..."
if command -v setfacl >/dev/null 2>&1; then
    setfacl -b "$MACHINE_ROOT"
    setfacl -R -b "$MACHINE_ROOT/drive_c"
    for f in system.reg userdef.reg; do
        if [ -f "$MACHINE_ROOT/$f" ]; then setfacl -b "$MACHINE_ROOT/$f"; fi
    done
fi
find "$MACHINE_ROOT" -mindepth 1 -maxdepth 1 ! -type l -exec chown -h "root:$MACHINE_GROUP" {} +
chown -h "root:$MACHINE_GROUP" "$MACHINE_ROOT"
chown -R -h "root:$MACHINE_GROUP" "$MACHINE_ROOT/drive_c"
# The bootstrap wineboot (run as root) leaves pe_section_cache/*.cache as root:root. They are not under drive_c, so the
# chown above missed them and the "migration incomplete" check below failed on the second pass (found on the lab guest).
[ ! -d "$MACHINE_ROOT/pe_section_cache" ] || chown -R -h "root:$MACHINE_GROUP" "$MACHINE_ROOT/pe_section_cache"
chmod 2775 "$MACHINE_ROOT" "$MACHINE_ROOT/drive_c"
find "$MACHINE_ROOT/drive_c" -type d -exec chmod 2775 {} +
find "$MACHINE_ROOT/drive_c" -type f -exec chmod 664 {} +
find "$MACHINE_ROOT/drive_c" -type f \( -iname "*.exe" -o -iname "*.dll" \) -exec chmod 775 {} +
if [ -f "$MACHINE_ROOT/system.reg" ]; then
    chmod 664 "$MACHINE_ROOT/system.reg"
    sed -i 's/\\\\users\\\\root\\\\Favorites/\\\\users\\\\Public\\\\Favorites/g' "$MACHINE_ROOT/system.reg"
fi
if [ -f "$MACHINE_ROOT/userdef.reg" ]; then
    chmod 664 "$MACHINE_ROOT/userdef.reg"
fi
if command -v setfacl >/dev/null 2>&1; then
    setfacl -m "g:$MACHINE_GROUP:rwX" "$MACHINE_ROOT"
    setfacl -d -m "u::rwX,g::rwX,g:$MACHINE_GROUP:rwX,o::r-x" "$MACHINE_ROOT"
    setfacl -R -m "g:$MACHINE_GROUP:rwX" "$MACHINE_ROOT/drive_c"
    setfacl -R -d -m "u::rwX,g::rwX,g:$MACHINE_GROUP:rwX,o::r-x" "$MACHINE_ROOT/drive_c"
    if [ -f "$MACHINE_ROOT/system.reg" ]; then
        setfacl -m "g:$MACHINE_GROUP:rw" "$MACHINE_ROOT/system.reg"
    fi
    if [ -f "$MACHINE_ROOT/userdef.reg" ]; then
        setfacl -m "g:$MACHINE_GROUP:rw" "$MACHINE_ROOT/userdef.reg"
    fi
fi
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
        # Preserve the existing protected administrator-group and built-in Administrator choices on update.
        # WEX_BUILTIN_ADMINISTRATOR=NAME names the account that wex-accounts maps with the reserved RID 500.
        ADMIN_GROUP="${WEX_ADMIN_GROUP:-sudo}"
        BUILTIN_ADMIN="${WEX_BUILTIN_ADMINISTRATOR:-}"
        if [ -f "$SYSRUN_CONF" ] && [ ! -L "$SYSRUN_CONF" ] && { [ -z "${WEX_ADMIN_GROUP:-}" ] || [ -z "$BUILTIN_ADMIN" ]; }; then
            [ "$(stat -c '%u' -- "$SYSRUN_CONF")" = 0 ] || die "sysrun config is not root-owned"
            [ $(( 0$(stat -c '%a' -- "$SYSRUN_CONF") & 022 )) -eq 0 ] || die "sysrun config is writable by others"
            seen_admin=0 seen_builtin=0
            while IFS= read -r config_line; do
                case "$config_line" in
                    admin_group=*) [ -n "${WEX_ADMIN_GROUP:-}" ] || [ "$seen_admin" = 1 ] || ADMIN_GROUP="${config_line#admin_group=}"; seen_admin=1 ;;
                    builtin_administrator=*) [ -n "${WEX_BUILTIN_ADMINISTRATOR:-}" ] || [ "$seen_builtin" = 1 ] || BUILTIN_ADMIN="${config_line#builtin_administrator=}"; seen_builtin=1 ;;
                esac
            done < "$SYSRUN_CONF"
        fi
        # A coarse check here; wex-accounts and the server apply the exact account-name grammar.
        case "$BUILTIN_ADMIN" in *[!A-Za-z0-9_.\$-]*|-*) die "invalid built-in Administrator account name: $BUILTIN_ADMIN" ;; esac
        install -m 2755 -o root -g "$MACHINE_GROUP" -- "$SYSRUN_TMP/wex-sysrun" "$SYSRUN_DIR/wex-sysrun"
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
        install -m 0644 -o root -g root -- "$SYSRUN_TMP/sysrun.conf" "$SYSRUN_CONF"
    fi
    rm -rf -- "$SYSRUN_TMP"
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
    # wex-is-admin USER: the administrator rule of the server, for scripts (Safe Mode). Unprivileged.
    IS_ADMIN_SOURCE="$(dirname -- "$(readlink -f -- "$0")")/../tools/wex-is-admin/wex-is-admin.c"
    IS_ADMIN_PKG="$(dirname -- "$(readlink -f -- "$0")")/wex-is-admin.bin"
    if [ -f "$IS_ADMIN_PKG" ]; then
        install -m 0755 -o root -g root -- "$IS_ADMIN_PKG" "$SYSRUN_DIR/wex-is-admin"
    elif [ -f "$IS_ADMIN_SOURCE" ] && command -v cc >/dev/null 2>&1; then
        cc -O2 -Wall -Wextra -fPIE -pie -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wl,-z,relro,-z,now \
            -o "$ACCOUNTS_TMP/wex-is-admin" "$IS_ADMIN_SOURCE"
        install -m 0755 -o root -g root -- "$ACCOUNTS_TMP/wex-is-admin" "$SYSRUN_DIR/wex-is-admin"
    else
        echo "Warning: no wex-is-admin binary or source and compiler; the helper is not installed" >&2
    fi
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
fi

echo "=== S3 Machine Root Setup Completed Successfully ==="
