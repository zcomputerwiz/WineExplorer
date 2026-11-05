#!/bin/bash
# Build the WoW64 tree, package it and build the ISO from a fresh clone (run inside WSL wexbuild as root).
set -euo pipefail
R=/root/WE-prefetch
cd "$R"
echo "=== assemble $(date)"; bash scripts/assemble.sh
mkdir -p build/wow64 && cd build/wow64
echo "=== configure $(date)"; ../../wine/configure --enable-archs=i386,x86_64 --prefix="$PWD/install"
echo "=== make $(date)"; make -j16
cd "$R"
echo "=== package $(date)"; BUILD_DIR="$R/build/wow64" bash installer/package-wine.sh
ls -lh installer/live-build/config/packages.chroot/
echo "=== iso $(date)"
WORK_DIR=/root/iso-work-prefetch OUT_ISO=/root/wineexplorer-prefetch.iso BUILD_DIR="$R/build/wow64" bash installer/build-iso.sh --clean
ls -lh /root/wineexplorer-prefetch.iso; sha256sum /root/wineexplorer-prefetch.iso
echo "=== done $(date)"
