#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Text-mode Setup's start-up step with Vintner Boot (docs/VINTNER_BOOT.md section 4.2), on an ISO built with
# build-iso.sh --vintner. textsetup runs it in the live system as root once the files are copied to TARGET and the
# live packages are purged from it, with the live system's /dev, /dev/pts, /proc, /sys and /run bound into TARGET.
#
#   install-vintner.sh TARGET DISK ROOT_PART BOOT_STORAGE_PART ESP_PART BIOS_BOOT_PART
#
# ESP_PART or BIOS_BOOT_PART may be empty; the disk is made to start on each firmware it has a partition for.
# The packages are read from /setup/vintner (WEX_VINTNER_DEBS). Everything goes to TARGET/var/log/vintner-install.log.
# Exit status 0 only when the disk now starts through Vintner Boot and the stock loader is gone.
set -eu

TARGET=$1
DISK=$2
ROOT_PART=$3
BS_PART=$4
ESP_PART=${5:-}
BIOS_PART=${6:-}
DEBS=${WEX_VINTNER_DEBS:-/setup/vintner}
WORK=/var/tmp/wex-vintner
BS_DIR=/mnt/wex-bootstorage

mkdir -p "$TARGET/var/log"
exec >>"$TARGET/var/log/vintner-install.log" 2>&1
echo "=== $(date -u '+%Y-%m-%d %H:%M:%S') Vintner Boot: disk $DISK, root $ROOT_PART, boot storage $BS_PART," \
     "ESP ${ESP_PART:-none}, BIOS boot ${BIOS_PART:-none}"

in_target() {
    chroot "$TARGET" /usr/bin/env PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin LC_ALL=C \
        DEBIAN_FRONTEND=noninteractive "$@"
}

ESP_MOUNTED=false
EFIVARS_MOUNTED=false
cleanup() {
    status=$?
    mountpoint -q "$TARGET$BS_DIR" && umount "$TARGET$BS_DIR"
    rmdir "$TARGET$BS_DIR" 2>/dev/null || true
    if $ESP_MOUNTED; then umount "$TARGET/boot/efi" || true; fi
    if $EFIVARS_MOUNTED; then umount "$TARGET/sys/firmware/efi/efivars" || true; fi
    rm -rf "$TARGET$WORK"
    if [ "$status" -eq 0 ]; then echo "=== done"; else echo "=== FAILED (exit $status)"; fi
}
trap cleanup EXIT

core=
platform=
for f in "$DEBS"/vintner-core_*.deb; do [ -f "$f" ] && core=$f; done
for f in "$DEBS"/vintner-platform_*.deb; do [ -f "$f" ] && platform=$f; done
if [ -z "$core" ] || [ -z "$platform" ]; then
    echo "vintner-core or vintner-platform is missing from $DEBS"
    exit 1
fi

if [ -n "$ESP_PART" ] && [ -n "$BIOS_PART" ]; then
    firmware=both
elif [ -n "$ESP_PART" ]; then
    firmware=uefi
elif [ -n "$BIOS_PART" ]; then
    firmware=bios
else
    echo "the disk has neither an EFI system partition nor a BIOS boot partition"
    exit 1
fi

partuuid() {
    value=$(blkid -s PARTUUID -o value "$1" || true)
    [ -n "$value" ] || { echo "no PARTUUID for $1" >&2; exit 1; }
    echo "$value"
}
ROOT_PARTUUID=$(partuuid "$ROOT_PART")
BS_PARTUUID=$(partuuid "$BS_PART")
set -- --root-partuuid "$ROOT_PARTUUID" --boot-storage-partuuid "$BS_PARTUUID"
if [ -n "$ESP_PART" ]; then
    ESP_PARTUUID=$(partuuid "$ESP_PART")
    set -- "$@" --esp-partuuid "$ESP_PARTUUID"
fi

# 1. vintner-core. Its kernel and initramfs hooks do nothing here: the command line in the chroot is the live
#    system's, which has no vintner.bootstorage=.
mkdir -p "$TARGET$WORK"
cp "$core" "$TARGET$WORK/vintner-core.deb"
cp "$platform" "$TARGET$WORK/vintner-platform.deb"
echo "--- installing vintner-core"
in_target dpkg -i "$WORK/vintner-core.deb"

# 2. vintner-platform conflicts with grub-pc, so it cannot be installed yet: vintner install reads the payloads from
#    the unpacked package, removes the stock loader, and the package is installed afterwards (as in Vintner Boot's
#    ci/install row).
dpkg-deb -x "$TARGET$WORK/vintner-platform.deb" "$TARGET$WORK/platform"

# 3. The kernel options of every generation (vintner install adds root=, ro and vintner.bootstorage=).
mkdir -p "$TARGET/etc/kernel"
if [ ! -s "$TARGET/etc/kernel/cmdline" ]; then
    echo "quiet" > "$TARGET/etc/kernel/cmdline"
fi
echo "--- kernel options: $(cat "$TARGET/etc/kernel/cmdline")"

# 4. The copied initramfs is the live system's (live-boot is purged now): build the installed system's own.
echo "--- update-initramfs"
in_target update-initramfs -u -k all

# 5. Mounts: boot storage, the ESP (a BIOS install does not mount it), and efivarfs for the NVRAM boot entry (the
#    bind of /sys does not carry the live system's efivarfs mount).
mkdir -p "$TARGET$BS_DIR"
mount -t vfat "$BS_PART" "$TARGET$BS_DIR"
if [ -n "$ESP_PART" ] && ! mountpoint -q "$TARGET/boot/efi"; then
    mkdir -p "$TARGET/boot/efi"
    mount -t vfat "$ESP_PART" "$TARGET/boot/efi"
    ESP_MOUNTED=true
fi
if [ -d /sys/firmware/efi ] && [ -d "$TARGET/sys/firmware/efi/efivars" ] && \
   ! mountpoint -q "$TARGET/sys/firmware/efi/efivars"; then
    mount -t efivarfs efivarfs "$TARGET/sys/firmware/efi/efivars"
    EFIVARS_MOUNTED=true
fi

# 6. vintner install: stage, publish generation 1, take booting over, remove the stock loader, verify.
echo "--- vintner install --disk $DISK --firmware $firmware $*"
in_target vintner install --disk "$DISK" --firmware "$firmware" --target / --esp /boot/efi --boot-storage "$BS_DIR" \
    --payload-dir "$WORK/platform/usr/lib/vintner/payloads" --update-initramfs "$@"

# 7. The stock loader is gone, so vintner-platform installs now.
if in_target dpkg-query -W -f='${Status}\n' grub-pc 2>/dev/null | grep -q 'install ok installed'; then
    echo "grub-pc is still installed"
    exit 1
fi
echo "--- installing vintner-platform"
in_target dpkg -i "$WORK/vintner-platform.deb"

# GRUB's settings for WineExplorer (live-build include) have nothing to configure now.
rm -f "$TARGET/etc/default/grub.d/wineexplorer.cfg"
