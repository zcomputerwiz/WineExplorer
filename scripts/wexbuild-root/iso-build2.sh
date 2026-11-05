#!/bin/bash
set -euo pipefail
apt-get install -y zstd msitools cabextract >/dev/null 2>&1 || true
R=/root/WE-prefetch; git -C $R pull -q --ff-only
cd "$R"
echo "=== package $(date)"; BUILD_DIR="$R/build/wow64" bash installer/package-wine.sh
ls -lh installer/live-build/config/packages.chroot/
echo "=== iso $(date)"
WORK_DIR=/root/iso-work-prefetch OUT_ISO=/root/wineexplorer-prefetch.iso BUILD_DIR="$R/build/wow64" bash installer/build-iso.sh --clean
ls -lh /root/wineexplorer-prefetch.iso; sha256sum /root/wineexplorer-prefetch.iso
echo "=== done $(date)"
