#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sets up or updates the shared machine root (/var/lib/wineexplorer/machine)
# and populates/links prefix templates and user prefixes according to the S2 model.
set -euo pipefail

MACHINE_ROOT="${WEX_MACHINE_ROOT:-/var/lib/wineexplorer/machine}"
SKEL_PFX="${WEX_SKEL_PFX:-/etc/skel/.wine}"

echo "=== WineExplorer S2 Machine Root Setup ==="
echo "Machine root: $MACHINE_ROOT"
echo "Template prefix: $SKEL_PFX"

ensure_machine_prefix_links()
{
    mkdir -p "$MACHINE_ROOT/dosdevices" "$MACHINE_ROOT/drive_c/users/root"
    ln -sfn ../drive_c "$MACHINE_ROOT/dosdevices/c:"
    rm -f "$MACHINE_ROOT/dosdevices/z:"
    ln -s / "$MACHINE_ROOT/dosdevices/z:"
    chown root:sudo "$MACHINE_ROOT"
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
            cp -a "$SKEL_PFX/drive_c/ProgramData"/. "$MACHINE_ROOT/drive_c/ProgramData/" 2>/dev/null || true
            rm -rf "$SKEL_PFX/drive_c/ProgramData"
        fi
        if [ -d "$SKEL_PFX/drive_c/users/Public" ] && [ ! -L "$SKEL_PFX/drive_c/users/Public" ]; then
            cp -a "$SKEL_PFX/drive_c/users/Public"/. "$MACHINE_ROOT/drive_c/users/Public/" 2>/dev/null || true
            rm -rf "$SKEL_PFX/drive_c/users/Public"
        fi
        cp "$SKEL_PFX/system.reg" "$MACHINE_ROOT/" 2>/dev/null || true
        cp "$SKEL_PFX/userdef.reg" "$MACHINE_ROOT/" 2>/dev/null || true
        cp "$SKEL_PFX/user.reg" "$MACHINE_ROOT/" 2>/dev/null || true
        cp "$SKEL_PFX/.update-timestamp" "$MACHINE_ROOT/" 2>/dev/null || true
    else
        echo "Bootstrapping machine root at $MACHINE_ROOT..."
        # never wipe an existing machine root: it holds the programs installed for all users
        if [ -e "$MACHINE_ROOT" ] && [ -n "$(ls -A "$MACHINE_ROOT" 2>/dev/null)" ]; then
            echo "$MACHINE_ROOT exists but has no drive_c/windows/system32; refusing to replace it" >&2
            exit 1
        fi
        ensure_machine_prefix_links

        WINELINKFAKEDLLS=1 WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all \
            WINEPREFIX="$MACHINE_ROOT" /usr/local/bin/wine wineboot.exe -i
        WINEPREFIX="$MACHINE_ROOT" /usr/local/bin/wineserver -w
        ensure_common_shell_dirs
    fi
else
    echo "Updating existing machine root at $MACHINE_ROOT..."
    ensure_machine_prefix_links
    WINELINKFAKEDLLS=1 WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all \
        WINEPREFIX="$MACHINE_ROOT" /usr/local/bin/wine wineboot.exe -u
    WINEPREFIX="$MACHINE_ROOT" /usr/local/bin/wineserver -w
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

# 2. Ensure permissions and POSIX default ACLs on machine root:
# Owned by root:sudo (preserving admin owners in Program Files), setgid 2775 on dirs,
# 664 on regular files, 775 on .exe/.dll, and default ACL g:sudo:rwX so files created
# by members of sudo remain group-writable regardless of process umask.
echo "Applying permissions and default ACLs to machine root (root:sudo, mode 2775, default ACL g:sudo:rwX)..."
chown root:sudo "$MACHINE_ROOT" "$MACHINE_ROOT/drive_c"
chown -R root:sudo "$MACHINE_ROOT/drive_c/windows"
chgrp -R sudo "$MACHINE_ROOT/drive_c"
chmod 2775 "$MACHINE_ROOT" "$MACHINE_ROOT/drive_c"
find "$MACHINE_ROOT/drive_c" -type d -exec chmod 2775 {} +
find "$MACHINE_ROOT/drive_c" -type f -exec chmod 664 {} +
find "$MACHINE_ROOT/drive_c" -type f \( -iname "*.exe" -o -iname "*.dll" \) -exec chmod 775 {} +
if [ -f "$MACHINE_ROOT/system.reg" ]; then
    chown root:sudo "$MACHINE_ROOT/system.reg"
    chmod 664 "$MACHINE_ROOT/system.reg"
    sed -i 's/\\\\users\\\\root\\\\Favorites/\\\\users\\\\Public\\\\Favorites/g' "$MACHINE_ROOT/system.reg"
fi
if [ -f "$MACHINE_ROOT/userdef.reg" ]; then
    chown root:sudo "$MACHINE_ROOT/userdef.reg"
    chmod 664 "$MACHINE_ROOT/userdef.reg"
fi
if command -v setfacl >/dev/null 2>&1; then
    setfacl -m g:sudo:rwX "$MACHINE_ROOT"
    setfacl -d -m u::rwX,g::rwX,g:sudo:rwX,o::r-x "$MACHINE_ROOT"
    setfacl -R -m g:sudo:rwX "$MACHINE_ROOT/drive_c"
    setfacl -R -d -m u::rwX,g::rwX,g:sudo:rwX,o::r-x "$MACHINE_ROOT/drive_c"
    if [ -f "$MACHINE_ROOT/system.reg" ]; then
        setfacl -m g:sudo:rw "$MACHINE_ROOT/system.reg"
    fi
    if [ -f "$MACHINE_ROOT/userdef.reg" ]; then
        setfacl -m g:sudo:rw "$MACHINE_ROOT/userdef.reg"
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
