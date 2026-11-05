#!/bin/bash
# Fresh-clone ISO build (WSL wexbuild, root): clone, assemble, WoW64 build, package, ISO.
set -euo pipefail
apt-get install -y zstd msitools cabextract >/dev/null 2>&1 || true
R=/root/WE-iso
rm -rf "$R" /root/iso-work-fresh
git clone -q --recurse-submodules https://github.com/zcomputerwiz/WineExplorer.git "$R"
cd "$R"; git log --oneline -1
echo "=== assemble $(date)"; bash scripts/assemble.sh
mkdir -p build/wow64 && cd build/wow64
echo "=== configure $(date)"; ../../wine/configure --enable-archs=i386,x86_64 --prefix="$PWD/install" >/dev/null
echo "=== make $(date)"; make -j16 >/root/iso-fresh-make.log 2>&1
cd "$R"
echo "=== package $(date)"; BUILD_DIR="$R/build/wow64" bash installer/package-wine.sh
echo "=== iso $(date)"
WORK_DIR=/root/iso-work-fresh OUT_ISO=/root/wineexplorer-fresh.iso BUILD_DIR="$R/build/wow64" bash installer/build-iso.sh --clean
ls -lh /root/wineexplorer-fresh.iso; sha256sum /root/wineexplorer-fresh.iso
echo "=== done $(date)"
