#!/bin/bash
# Builds the bootable hybrid ISO for Debian + WineExplorer.
# Run on Debian VM as root (or via scripts/vm.sh ssh "sudo /path/to/build-iso.sh").
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_ISO="${OUT_ISO:-$SCRIPT_DIR/wineexplorer.iso}"
WORK_DIR="${WORK_DIR:-/home/wineuser/iso-build}"

GENUINE=false
UNATTENDED=false
DO_CLEAN=false

for arg in "$@"; do
    case "$arg" in
        --genuine) GENUINE=true ;;
        --unattended) UNATTENDED=true ;;
        --clean) DO_CLEAN=true ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (or via sudo)."
    exit 1
fi

echo "=== WineExplorer ISO Builder ==="
echo "Source installer dir: $SCRIPT_DIR"
echo "Working build dir:    $WORK_DIR"
echo "Output ISO:           $OUT_ISO"
echo "Options: genuine=$GENUINE, unattended=$UNATTENDED, clean=$DO_CLEAN"

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

if [ -d "$WORK_DIR/chroot" ]; then
    echo "=== Syncing includes.chroot into existing chroot ==="
    cp -a "$WORK_DIR/config/includes.chroot"/* "$WORK_DIR/chroot/" 2>/dev/null || true
    rm -f "$WORK_DIR/.build/chroot_includes_after_packages"
    rm -f "$WORK_DIR/.build/chroot_hooks"
fi

# 1. Package Wine and session if needed
if [ -d "$SCRIPT_DIR/live-build/config/packages.chroot" ]; then
    cp -a "$SCRIPT_DIR/live-build/config/packages.chroot"/* "$WORK_DIR/config/packages.chroot/" 2>/dev/null || true
fi
if [ ! -f "$WORK_DIR/config/packages.chroot/wineexplorer_11.19-wineexplorer1_amd64.deb" ]; then
    echo "=== Step 1: Packaging Wine and session ==="
    OUT_DIR="$WORK_DIR/config/packages.chroot" "$SCRIPT_DIR/package-wine.sh"
fi

# 2. Setup genuine or open art
if [ "$GENUINE" = true ]; then
    echo "=== Genuine XP assets requested ==="
    if [ -d "$ROOT_DIR/assets" ]; then
        mkdir -p "$WORK_DIR/config/includes.chroot/setup/art/xp"
        cp -a "$ROOT_DIR/assets"/* "$WORK_DIR/config/includes.chroot/setup/art/xp/" 2>/dev/null || true
    fi
fi

# 3. Setup unattended file if requested
if [ "$UNATTENDED" = true ]; then
    echo "=== Unattended install mode enabled ==="
    if [ -f "$SCRIPT_DIR/setupinfs/unattend.sif" ]; then
        cp "$SCRIPT_DIR/setupinfs/unattend.sif" "$WORK_DIR/config/includes.chroot/setup/unattend.sif"
    else
        cp "$SCRIPT_DIR/setupinfs/unattend.sif.example" "$WORK_DIR/config/includes.chroot/setup/unattend.sif"
    fi
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
    echo "=== Moving generated ISO to $OUT_ISO ==="
    mkdir -p "$(dirname "$OUT_ISO")"
    mv -f "$ISO_BUILT" "$OUT_ISO"
    echo "=== Successfully built $OUT_ISO ==="
    ls -lh "$OUT_ISO"
else
    echo "ERROR: Failed to find generated ISO!"
    exit 1
fi
