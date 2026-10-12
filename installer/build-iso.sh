#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the bootable hybrid ISO for Debian + WineExplorer.
# Run on Debian VM as root (or via scripts/vm.sh ssh "sudo /path/to/build-iso.sh").
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_ISO="${OUT_ISO:-$SCRIPT_DIR/wineexplorer.iso}"
WORK_DIR="${WORK_DIR:-/home/wineuser/iso-build}"

UNATTENDED=false
DO_CLEAN=false

for arg in "$@"; do
    case "$arg" in
        --unattended) UNATTENDED=true ;;
        --clean) DO_CLEAN=true ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (or via sudo)."
    exit 1
fi

# live-build can restore a bootstrap chroot while retaining completed package
# stage markers. Reusing it then skips kernel installation and produces an
# incomplete image. Require a fresh workspace rather than patching that state.
if [ "$DO_CLEAN" != true ] && { [ -d "$WORK_DIR/chroot" ] || [ -d "$WORK_DIR/.build" ]; }; then
    echo "Existing live-build workspace requires --clean: $WORK_DIR"
    exit 1
fi

echo "=== WineExplorer ISO Builder ==="
echo "Source installer dir: $SCRIPT_DIR"
echo "Working build dir:    $WORK_DIR"
echo "Output ISO:           $OUT_ISO"
echo "Options: unattended=$UNATTENDED, clean=$DO_CLEAN"

# Clean if requested
if [ "$DO_CLEAN" = true ]; then
    echo "=== Cleaning previous build artifacts in $WORK_DIR ==="
    if [ -d "$WORK_DIR" ]; then
        cd "$WORK_DIR"
        lb clean --purge 2>/dev/null || true
        cd /
        rm -rf "$WORK_DIR"
    fi
fi

# Prepare work directory on native ext4 filesystem
mkdir -p "$WORK_DIR"
mkdir -p "$WORK_DIR/config"

echo "=== Copying live-build configuration to $WORK_DIR ==="
cp -a "$SCRIPT_DIR/live-build/config"/* "$WORK_DIR/config/" 2>/dev/null || true
cp -a "$SCRIPT_DIR/tools/mklayout.py" "$WORK_DIR/config/" 2>/dev/null || true
cp -a "$SCRIPT_DIR/setupinfs/txtsetup.sif.in" "$WORK_DIR/config/" 2>/dev/null || true
mkdir -p "$WORK_DIR/config/packages.chroot"
mkdir -p "$WORK_DIR/config/includes.chroot/setup/art"

# 1. Compile textsetup binary into work directory
echo "=== Step 1: Compiling textsetup ==="
make -C "$SCRIPT_DIR/textsetup" clean
make -C "$SCRIPT_DIR/textsetup"
mkdir -p "$WORK_DIR/config/includes.chroot/setup"
cp -f "$SCRIPT_DIR/textsetup/textsetup" "$WORK_DIR/config/includes.chroot/setup/textsetup"
chmod 755 "$WORK_DIR/config/includes.chroot/setup/textsetup"
make -C "$SCRIPT_DIR/textsetup" clean

# 2. Package Wine and session
echo "=== Step 2: Packaging Wine and session ==="
OUT_DIR="$WORK_DIR/config/packages.chroot" "$SCRIPT_DIR/package-wine.sh"

# 3. The ISO carries the open pictures of the Wine build only: never genuine XP files (docs/ASSETS.md). Build it from a
#    tree without the local overlay (vm.sh sync puts genuine pictures into ~/src on the dev VM).

# 4. Setup unattended file if requested
if [ "$UNATTENDED" = true ]; then
    echo "=== Unattended install mode enabled ==="
    if [ -f "$SCRIPT_DIR/setupinfs/unattend.sif" ]; then
        cp "$SCRIPT_DIR/setupinfs/unattend.sif" "$WORK_DIR/config/includes.chroot/setup/unattend.sif"
    else
        cp "$SCRIPT_DIR/setupinfs/unattend.sif.example" "$WORK_DIR/config/includes.chroot/setup/unattend.sif"
    fi
    sed -i 's/^set default=.*/set default=2/' "$WORK_DIR/config/hooks/live/9000-grub-menu.binary" 2>/dev/null || true
else
    rm -f "$WORK_DIR/config/includes.chroot/setup/unattend.sif"
    sed -i 's/^set default=.*/set default=0/' "$WORK_DIR/config/hooks/live/9000-grub-menu.binary" 2>/dev/null || true
fi

# 4. Configure live-build
cd "$WORK_DIR"
echo "=== Step 2: Configuring live-build ==="
lb config \
    --mode debian \
    --distribution trixie \
    --architectures amd64 \
    --binary-images iso-hybrid \
    --system live \
    --archive-areas "main contrib non-free non-free-firmware" \
    --security true \
    --updates true \
    --bootloaders "grub-efi,grub-pc" \
    --memtest none \
    --win32-loader false \
    --iso-application "WineExplorer" \
    --iso-publisher "WineExplorer Project" \
    --iso-volume "WEX_INSTALL"

# Clean any stale lockfile and ensure executable hooks
rm -f "$WORK_DIR/.lock"
chmod +x config/hooks/live/*.binary config/hooks/normal/*.chroot 2>/dev/null || true
if [ -d chroot/usr ] && [ ! -f .build/chroot ]; then
    touch .build/chroot
fi

# 5. Build ISO
echo "=== Step 3: Running lb build ==="
lb clean --binary 2>/dev/null || true
lb build

ISO_BUILT=$(ls -t live-image-amd64.hybrid.iso live-image-amd64.iso 2>/dev/null | head -1 || true)
if [ -n "$ISO_BUILT" ] && [ -f "$ISO_BUILT" ]; then
    echo "=== Copying generated ISO to $OUT_ISO ==="
    mkdir -p "$(dirname "$OUT_ISO")"
    cp -f "$ISO_BUILT" "$OUT_ISO"
    rm -f "$ISO_BUILT"
    echo "=== Successfully built $OUT_ISO ==="
    ls -lh "$OUT_ISO"
else
    echo "ERROR: Failed to find generated ISO!"
    exit 1
fi
