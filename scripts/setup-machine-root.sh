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

# 1. Initialize machine root directory structure if not already bootstrapped
if [ ! -d "$MACHINE_ROOT/drive_c/windows/system32" ]; then
    if [ -d "$SKEL_PFX/drive_c/windows" ] && [ ! -L "$SKEL_PFX/drive_c/windows" ]; then
        echo "Promoting existing prefix $SKEL_PFX into machine root $MACHINE_ROOT..."
        mkdir -p "$MACHINE_ROOT/drive_c"
        mv "$SKEL_PFX/drive_c/windows" "$MACHINE_ROOT/drive_c/"
        mv "$SKEL_PFX/drive_c/Program Files" "$MACHINE_ROOT/drive_c/"
        mv "$SKEL_PFX/drive_c/Program Files (x86)" "$MACHINE_ROOT/drive_c/"
        mkdir -p "$MACHINE_ROOT/drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs/StartUp"
        mkdir -p "$MACHINE_ROOT/drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs/Administrative Tools"
        mkdir -p "$MACHINE_ROOT/drive_c/ProgramData/Microsoft/Windows/Templates"
        mkdir -p "$MACHINE_ROOT/drive_c/users/Public/Desktop"
        mkdir -p "$MACHINE_ROOT/drive_c/users/Public/Documents"
        if [ -d "$SKEL_PFX/drive_c/ProgramData" ] && [ ! -L "$SKEL_PFX/drive_c/ProgramData" ]; then
            cp -a "$SKEL_PFX/drive_c/ProgramData"/* "$MACHINE_ROOT/drive_c/ProgramData/" 2>/dev/null || true
            rm -rf "$SKEL_PFX/drive_c/ProgramData"
        fi
        if [ -d "$SKEL_PFX/drive_c/users/Public" ] && [ ! -L "$SKEL_PFX/drive_c/users/Public" ]; then
            cp -a "$SKEL_PFX/drive_c/users/Public"/* "$MACHINE_ROOT/drive_c/users/Public/" 2>/dev/null || true
            rm -rf "$SKEL_PFX/drive_c/users/Public"
        fi
        cp "$SKEL_PFX/system.reg" "$MACHINE_ROOT/" 2>/dev/null || true
        cp "$SKEL_PFX/userdef.reg" "$MACHINE_ROOT/" 2>/dev/null || true
        cp "$SKEL_PFX/.update-timestamp" "$MACHINE_ROOT/" 2>/dev/null || true
    else
        echo "Bootstrapping machine root at $MACHINE_ROOT..."
        TMP_BOOTSTRAP=$(mktemp -d /tmp/wex_machine_boot.XXXXXX)
        trap 'rm -rf "$TMP_BOOTSTRAP"' EXIT

        WINELINKFAKEDLLS=1 WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all \
            WINEPREFIX="$TMP_BOOTSTRAP" /usr/local/bin/wine wineboot.exe -i
        WINEPREFIX="$TMP_BOOTSTRAP" /usr/local/bin/wineserver -k
        sleep 1

        mkdir -p "$MACHINE_ROOT/drive_c"
        mv "$TMP_BOOTSTRAP/drive_c/windows" "$MACHINE_ROOT/drive_c/"
        mv "$TMP_BOOTSTRAP/drive_c/Program Files" "$MACHINE_ROOT/drive_c/"
        mv "$TMP_BOOTSTRAP/drive_c/Program Files (x86)" "$MACHINE_ROOT/drive_c/"
        mkdir -p "$MACHINE_ROOT/drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs/StartUp"
        mkdir -p "$MACHINE_ROOT/drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs/Administrative Tools"
        mkdir -p "$MACHINE_ROOT/drive_c/ProgramData/Microsoft/Windows/Templates"
        mkdir -p "$MACHINE_ROOT/drive_c/users/Public/Desktop"
        mkdir -p "$MACHINE_ROOT/drive_c/users/Public/Documents"
        if [ -d "$TMP_BOOTSTRAP/drive_c/users/Public" ]; then
            cp -a "$TMP_BOOTSTRAP/drive_c/users/Public"/* "$MACHINE_ROOT/drive_c/users/Public/" 2>/dev/null || true
        fi

        # Save reference registry hives and update timestamp
        cp "$TMP_BOOTSTRAP/system.reg" "$MACHINE_ROOT/"
        cp "$TMP_BOOTSTRAP/userdef.reg" "$MACHINE_ROOT/"
        cp "$TMP_BOOTSTRAP/.update-timestamp" "$MACHINE_ROOT/"
        rm -rf "$TMP_BOOTSTRAP"
        trap - EXIT
    fi
fi

# Install themes into machine root
THEME_DATA_DIR="${WEX_THEME_DATA_DIR:-/usr/local/share/wineexplorer/themes}"
if [ -d "$THEME_DATA_DIR" ]; then
    mkdir -p "$MACHINE_ROOT/drive_c/windows/Resources/Themes"
    for theme_file in "$THEME_DATA_DIR/"*.theme; do
        [ -f "$theme_file" ] || continue
        cp -f "$theme_file" "$MACHINE_ROOT/drive_c/windows/Resources/Themes/" 2>/dev/null || true
    done
fi

# 2. Ensure permissions on machine root: owned by root:sudo, setgid 2775 on dirs, 664/775 on files
echo "Applying permissions to machine root (root:sudo, mode 2775)..."
chown -R root:sudo "$MACHINE_ROOT"
chmod 2775 "$MACHINE_ROOT"
chmod 2775 "$MACHINE_ROOT/drive_c"
find "$MACHINE_ROOT/drive_c" -type d -exec chmod 2775 {} +
find "$MACHINE_ROOT/drive_c" -type f -exec chmod 664 {} +
find "$MACHINE_ROOT/drive_c" -name "*.exe" -o -name "*.dll" -exec chmod 775 {} +

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

# Ensure template has initial registry hives and update timestamp
if [ ! -f "$SKEL_PFX/system.reg" ] && [ -f "$MACHINE_ROOT/system.reg" ]; then
    cp "$MACHINE_ROOT/system.reg" "$SKEL_PFX/system.reg"
fi
if [ ! -f "$SKEL_PFX/userdef.reg" ] && [ -f "$MACHINE_ROOT/userdef.reg" ]; then
    cp "$MACHINE_ROOT/userdef.reg" "$SKEL_PFX/userdef.reg"
fi
if [ ! -f "$SKEL_PFX/user.reg" ]; then
    # Generate minimal user.reg if not present
    cat << 'EOF' > "$SKEL_PFX/user.reg"
WINE REGISTRY Version 2
;; All keys relative to HKCU

[Environment]
"TEMP"="C:\\windows\\temp"
"TMP"="C:\\windows\\temp"
EOF
fi
if [ -f "$MACHINE_ROOT/.update-timestamp" ]; then
    cp -a "$MACHINE_ROOT/.update-timestamp" "$SKEL_PFX/.update-timestamp"
fi

# Clean up any leftover profile folders
rm -rf "$SKEL_PFX/drive_c/users/root" "$SKEL_PFX/drive_c/users/wexsetup"
chmod 755 "$SKEL_PFX"
chmod 755 "$SKEL_PFX/dosdevices" "$SKEL_PFX/drive_c" "$SKEL_PFX/drive_c/users"
chmod 644 "$SKEL_PFX"/*.reg "$SKEL_PFX/.update-timestamp" 2>/dev/null || true

echo "=== S2 Machine Root Setup Completed Successfully ==="
