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
# The installed system's boot loader (docs/VINTNER_BOOT.md section 4.2). By default Setup installs Vintner Boot, from
# packages built out of the vintner submodule at the commit this repository pins; --vintner=DIR takes vintner-core
# and vintner-platform from DIR instead. --grub installs GRUB, the default until 2026-11-10, and puts nothing of
# Vintner on the ISO.
LOADER=vintner
VINTNER_DEBS=

for arg in "$@"; do
    case "$arg" in
        --unattended) UNATTENDED=true ;;
        --clean) DO_CLEAN=true ;;
        --vintner=*) VINTNER_DEBS=${arg#--vintner=} ;;
        --grub) LOADER=grub ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done
if [ "$LOADER" = grub ] && [ -n "$VINTNER_DEBS" ]; then
    echo "--grub and --vintner=DIR exclude each other"
    exit 1
fi

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
echo "Options: unattended=$UNATTENDED, clean=$DO_CLEAN, loader=$LOADER${VINTNER_DEBS:+ (packages from $VINTNER_DEBS)}"

# Vintner Boot's packages are built from the submodule unless --vintner=DIR gave them: check that this can work
# before the long steps
VINTNER_SRC="$ROOT_DIR/vintner"
if [ "$LOADER" = vintner ] && [ -z "$VINTNER_DEBS" ]; then
    if [ ! -f "$VINTNER_SRC/ci/package/build-debs.sh" ]; then
        echo "Vintner Boot: the vintner submodule is not checked out (git submodule update --init vintner)."
        echo "Pass --vintner=DIR with vintner-core and vintner-platform packages, or --grub for GRUB."
        exit 1
    fi
    for tool in git dpkg-buildpackage go; do
        if ! command -v "$tool" >/dev/null; then
            echo "Vintner Boot: building its packages needs $tool. Install its build dependencies"
            echo "(vintner/debian/control: debhelper golang-go, plus git dpkg-dev fakeroot curl cpio kmod xz-utils"
            echo "binutils grub-pc-bin systemd-boot-efi python3-pefile), or pass --vintner=DIR or --grub."
            exit 1
        fi
    done
fi

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

if [ "$LOADER" = vintner ] && [ -z "$VINTNER_DEBS" ]; then
    echo "=== Vintner Boot: building its packages from $(git -C "$VINTNER_SRC" log --oneline -1) ==="
    VINTNER_DEBS="$WORK_DIR/vintner-debs"
    bash "$VINTNER_SRC/ci/package/build-debs.sh" "$VINTNER_DEBS"
fi
VINTNER_CORE=
VINTNER_PLATFORM=
if [ -n "$VINTNER_DEBS" ]; then
    for f in "$VINTNER_DEBS"/vintner-core_*_amd64.deb; do [ -f "$f" ] && VINTNER_CORE=$f; done
    for f in "$VINTNER_DEBS"/vintner-platform_*_amd64.deb; do [ -f "$f" ] && VINTNER_PLATFORM=$f; done
    if [ -z "$VINTNER_CORE" ] || [ -z "$VINTNER_PLATFORM" ]; then
        echo "Vintner Boot: no vintner-core or vintner-platform package in $VINTNER_DEBS"
        exit 1
    fi
fi

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

# Vintner Boot: Setup's step and the two packages, which stay files on the ISO (vintner-platform conflicts with the
# live system's grub-pc). vintner-core's dependencies come from Debian with the live system.
rm -rf "$WORK_DIR/config/includes.chroot/setup/vintner" "$WORK_DIR/config/includes.chroot/setup/install-vintner.sh"
rm -f "$WORK_DIR/config/package-lists/vintner.list.chroot"
if [ -n "$VINTNER_DEBS" ]; then
    echo "=== Vintner Boot: $(basename "$VINTNER_CORE"), $(basename "$VINTNER_PLATFORM") ==="
    mkdir -p "$WORK_DIR/config/includes.chroot/setup/vintner"
    cp -f "$VINTNER_CORE" "$VINTNER_PLATFORM" "$WORK_DIR/config/includes.chroot/setup/vintner/"
    install -m755 "$SCRIPT_DIR/textsetup/install-vintner.sh" "$WORK_DIR/config/includes.chroot/setup/install-vintner.sh"
    printf '%s\n' systemd-boot-efi systemd-ukify sbsigntool > "$WORK_DIR/config/package-lists/vintner.list.chroot"
fi

# 2. Package Wine and session
echo "=== Step 2: Packaging Wine and session ==="
OUT_DIR="$WORK_DIR/config/packages.chroot" "$SCRIPT_DIR/package-wine.sh"
OUT_DIR="$WORK_DIR/config/packages.chroot" bash "$SCRIPT_DIR/package-wexcomp.sh"

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
