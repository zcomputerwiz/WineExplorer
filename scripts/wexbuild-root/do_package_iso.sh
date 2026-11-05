#!/bin/bash
set -euo pipefail
ROOT_DIR=/root/WineExplorer
cd "$ROOT_DIR"

echo "=== 1. Packaging Wine (package-wine.sh) ==="
P_START=$(date +%s)
BUILD_DIR="$ROOT_DIR/build/wow64" bash "$ROOT_DIR/installer/package-wine.sh"
P_END=$(date +%s)
P_DUR=$(( P_END - P_START ))
echo "=== Packaging completed in ${P_DUR}s ==="

echo "=== Package sizes ==="
ls -lh "$ROOT_DIR/installer/live-build/config/packages.chroot/"

echo "=== 2. Building ISO (build-iso.sh --clean) ==="
ISO_START=$(date +%s)
export WORK_DIR=/root/iso-work
export OUT_ISO=/root/wineexplorer-wow64.iso
export BUILD_DIR="$ROOT_DIR/build/wow64"
bash "$ROOT_DIR/installer/build-iso.sh" --clean
ISO_END=$(date +%s)
ISO_DUR=$(( ISO_END - ISO_START ))
echo "=== ISO build completed in ${ISO_DUR}s ==="

echo "=== ISO details ==="
ls -lh "$OUT_ISO"
sha256sum "$OUT_ISO"
