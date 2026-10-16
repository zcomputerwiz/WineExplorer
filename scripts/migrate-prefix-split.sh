#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Manual prefix migration script: converts an existing full standalone Wine prefix (~1.7 GB)
# into a lightweight split prefix (< 10 MB) referencing the shared machine root
# (/var/lib/wineexplorer/machine).
#
# NOTE: This script does NOT run automatically. It must be invoked explicitly by
# an administrator or user wishing to migrate an existing monolithic prefix.
#
# Usage:
#   migrate-prefix-split.sh [PREFIX_PATH]
#
# If PREFIX_PATH is omitted, $WINEPREFIX or $HOME/.wine is used.
set -euo pipefail

MACHINE_ROOT="${WEX_MACHINE_ROOT:-/var/lib/wineexplorer/machine}"
TARGET_PFX="${1:-${WINEPREFIX:-$HOME/.wine}}"
TARGET_PFX="$(realpath "$TARGET_PFX" 2>/dev/null || echo "$TARGET_PFX")"

echo "=== WineExplorer Prefix Split Migration ==="
echo "Target prefix: $TARGET_PFX"
echo "Machine root:  $MACHINE_ROOT"

# Precondition checks
if [ ! -d "$TARGET_PFX" ] || [ ! -d "$TARGET_PFX/drive_c" ]; then
    echo "ERROR: Target prefix $TARGET_PFX does not exist or is not a valid Wine prefix." >&2
    exit 1
fi

if [ ! -d "$MACHINE_ROOT/drive_c/windows/system32" ]; then
    echo "ERROR: Shared machine root at $MACHINE_ROOT does not exist or is incomplete." >&2
    echo "Please run /usr/local/bin/wex-setup-machine.sh as root first." >&2
    exit 1
fi

# Check if already migrated
if [ -L "$TARGET_PFX/drive_c/windows" ]; then
    CURRENT_LINK="$(readlink "$TARGET_PFX/drive_c/windows" || true)"
    echo "Target prefix is already a split prefix (drive_c/windows -> $CURRENT_LINK)."
    exit 0
fi

# Ensure wineserver is not running for this prefix
if command -v wineserver >/dev/null 2>&1; then
    WINEPREFIX="$TARGET_PFX" wineserver -k 2>/dev/null || true
    sleep 1
fi

# Measure original size
ORIG_SIZE_MB=$(du -sm "$TARGET_PFX" 2>/dev/null | awk '{print $1}' || echo "unknown")
echo "Original prefix size: ${ORIG_SIZE_MB} MB"

# Create a safety backup of user registry and private profile before modification
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
BACKUP_DIR="${TARGET_PFX}.pre-split-backup-${TIMESTAMP}"
echo "Backing up user registry and configuration to $BACKUP_DIR..."
mkdir -p "$BACKUP_DIR"
[ -f "$TARGET_PFX/user.reg" ] && cp -a "$TARGET_PFX/user.reg" "$BACKUP_DIR/"
[ -f "$TARGET_PFX/system.reg" ] && cp -a "$TARGET_PFX/system.reg" "$BACKUP_DIR/"
[ -f "$TARGET_PFX/userdef.reg" ] && cp -a "$TARGET_PFX/userdef.reg" "$BACKUP_DIR/"
if [ -d "$TARGET_PFX/drive_c/users" ]; then
    mkdir -p "$BACKUP_DIR/users"
    for user_dir in "$TARGET_PFX/drive_c/users/"*; do
        [ -d "$user_dir" ] || continue
        base_name="$(basename "$user_dir")"
        [ "$base_name" = "Public" ] && continue
        cp -a "$user_dir" "$BACKUP_DIR/users/" 2>/dev/null || true
    done
fi

# Remove monolithic machine directories and create symlinks into machine root
echo "Converting machine folders to shared symlinks..."
rm -rf "$TARGET_PFX/drive_c/windows"
ln -sfn "$MACHINE_ROOT/drive_c/windows" "$TARGET_PFX/drive_c/windows"

rm -rf "$TARGET_PFX/drive_c/Program Files"
ln -sfn "$MACHINE_ROOT/drive_c/Program Files" "$TARGET_PFX/drive_c/Program Files"

if [ -d "$MACHINE_ROOT/drive_c/Program Files (x86)" ]; then
    rm -rf "$TARGET_PFX/drive_c/Program Files (x86)"
    ln -sfn "$MACHINE_ROOT/drive_c/Program Files (x86)" "$TARGET_PFX/drive_c/Program Files (x86)"
fi

if [ -d "$TARGET_PFX/drive_c/ProgramData" ] && [ ! -L "$TARGET_PFX/drive_c/ProgramData" ]; then
    rm -rf "$TARGET_PFX/drive_c/ProgramData"
fi
ln -sfn "$MACHINE_ROOT/drive_c/ProgramData" "$TARGET_PFX/drive_c/ProgramData"

if [ -d "$TARGET_PFX/drive_c/users/Public" ] && [ ! -L "$TARGET_PFX/drive_c/users/Public" ]; then
    rm -rf "$TARGET_PFX/drive_c/users/Public"
fi
ln -sfn "$MACHINE_ROOT/drive_c/users/Public" "$TARGET_PFX/drive_c/users/Public"

# Convert shared hives (system.reg and userdef.reg) to symlinks
echo "Linking shared machine registry hives..."
rm -f "$TARGET_PFX/system.reg"
ln -sfn "$MACHINE_ROOT/system.reg" "$TARGET_PFX/system.reg"

rm -f "$TARGET_PFX/userdef.reg"
ln -sfn "$MACHINE_ROOT/userdef.reg" "$TARGET_PFX/userdef.reg"

# Synchronize .update-timestamp
if [ -f "$MACHINE_ROOT/.update-timestamp" ]; then
    cp -a "$MACHINE_ROOT/.update-timestamp" "$TARGET_PFX/.update-timestamp"
fi

# Measure new prefix size
NEW_SIZE_MB=$(du -sm "$TARGET_PFX" 2>/dev/null | awk '{print $1}' || echo "unknown")
echo "Migration complete."
echo "Prefix size reduced from ${ORIG_SIZE_MB} MB to ${NEW_SIZE_MB} MB."
echo "Backup preserved at: $BACKUP_DIR"
