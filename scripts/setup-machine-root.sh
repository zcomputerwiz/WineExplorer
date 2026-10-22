#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sets up or updates the shared machine root (/var/lib/wineexplorer/machine)
# and populates/links prefix templates and user prefixes according to the S2 model.
set -euo pipefail

MACHINE_ROOT="${WEX_MACHINE_ROOT:-/var/lib/wineexplorer/machine}"
SKEL_PFX="${WEX_SKEL_PFX:-/etc/skel/.wine}"

ACTION="${1:-apply}"

# Ownership Model:
# On production multi-user systems, 'wexsys' is the dedicated system group owning
# shared machine prefixes and hives (/var/lib/wineexplorer/machine), isolating
# administrative assets from interactive sudo accounts. On standard single-user or
# desktop setups without wexsys, 'sudo' is used as the administrative fallback group.
MACHINE_GROUP="${WEX_MACHINE_GROUP:-}"
if [ -z "$MACHINE_GROUP" ]; then
    if getent group wexsys >/dev/null 2>&1; then
        MACHINE_GROUP="wexsys"
    else
        MACHINE_GROUP="sudo"
    fi
fi

die()
{
    echo "Error: $*" >&2
    exit 1
}

case "$ACTION" in
    apply|rollback|--rollback) ;;
    *) die "unknown action: $ACTION (use apply or rollback)" ;;
esac

[ "$(id -u)" -eq 0 ] || die "must run as root"

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
        cp -rf "$theme_dir" "$MACHINE_ROOT/drive_c/windows/Resources/Themes/$tname" 2>/dev/null || true
    done
fi

# On developer machines a local genuine Luna (WEX_LOCAL_LUNA, never shipped) overrides the open style
if [ -n "${WEX_LOCAL_LUNA:-}" ] && [ -f "$WEX_LOCAL_LUNA" ]; then
    mkdir -p "$MACHINE_ROOT/drive_c/windows/Resources/Themes/Luna"
    cp -f "$WEX_LOCAL_LUNA" "$MACHINE_ROOT/drive_c/windows/Resources/Themes/Luna/Luna.msstyles" 2>/dev/null || true
fi

# 2. Ensure permissions and POSIX default ACLs on machine root:
# Owned by root:$MACHINE_GROUP (preserving admin owners in Program Files), setgid 2775 on dirs,
# 664 on regular files, 775 on .exe/.dll, and default ACL g:$MACHINE_GROUP:rwX so files created
# by members of $MACHINE_GROUP remain group-writable regardless of process umask.
echo "Applying permissions and default ACLs to machine root (root:$MACHINE_GROUP, mode 2775, default ACL g:$MACHINE_GROUP:rwX)..."
chown "root:$MACHINE_GROUP" "$MACHINE_ROOT" "$MACHINE_ROOT/drive_c"
chown -R "root:$MACHINE_GROUP" "$MACHINE_ROOT/drive_c/windows"
chgrp -R "$MACHINE_GROUP" "$MACHINE_ROOT/drive_c"
chmod 2775 "$MACHINE_ROOT" "$MACHINE_ROOT/drive_c"
find "$MACHINE_ROOT/drive_c" -type d -exec chmod 2775 {} +
find "$MACHINE_ROOT/drive_c" -type f -exec chmod 664 {} +
find "$MACHINE_ROOT/drive_c" -type f \( -iname "*.exe" -o -iname "*.dll" \) -exec chmod 775 {} +
if [ -f "$MACHINE_ROOT/system.reg" ]; then
    chown "root:$MACHINE_GROUP" "$MACHINE_ROOT/system.reg"
    chmod 664 "$MACHINE_ROOT/system.reg"
    sed -i 's/\\\\users\\\\root\\\\Favorites/\\\\users\\\\Public\\\\Favorites/g' "$MACHINE_ROOT/system.reg"
fi
if [ -f "$MACHINE_ROOT/userdef.reg" ]; then
    chown "root:$MACHINE_GROUP" "$MACHINE_ROOT/userdef.reg"
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

echo "=== S3 Machine Root Setup Completed Successfully ==="
