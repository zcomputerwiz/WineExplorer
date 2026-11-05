#!/bin/bash
# p15-iso.sh: P15 release build from a fresh clone of master (WoW64), package, ISO. Log: /root/p15-iso.log
set -euo pipefail
R=/root/WE-rel
apt-get install -y zstd msitools cabextract >/dev/null 2>&1 || true
rm -rf "$R"
echo "=== clone $(date)"
timeout 1200 git clone -q --recurse-submodules https://github.com/zcomputerwiz/WineExplorer.git "$R"
cd "$R"
git log --oneline -1; git -C wine log --oneline -1
echo "=== assemble $(date)"; timeout 600 bash scripts/assemble.sh
mkdir -p build/wow64 && cd build/wow64
echo "=== configure $(date)"; timeout 1200 ../../wine/configure --enable-archs=i386,x86_64 --prefix="$PWD/install" >/root/p15-configure.log 2>&1
echo "=== make $(date)"; timeout 3600 make -j16 >/root/p15-make.log 2>&1 || { tail -30 /root/p15-make.log; exit 1; }
cd "$R"
echo "=== package $(date)"; timeout 2400 env BUILD_DIR="$R/build/wow64" bash installer/package-wine.sh >/root/p15-package.log 2>&1 || { tail -30 /root/p15-package.log; exit 1; }
grep -a -i "aero\|warning" /root/p15-package.log | head -10
ls -lh installer/live-build/config/packages.chroot/
echo "=== iso $(date)"
timeout 3600 env WORK_DIR=/root/iso-work-rel OUT_ISO=/root/wineexplorer-rel.iso BUILD_DIR="$R/build/wow64" bash installer/build-iso.sh --clean >/root/p15-isobuild.log 2>&1 || { tail -30 /root/p15-isobuild.log; exit 1; }
ls -lh /root/wineexplorer-rel.iso; sha256sum /root/wineexplorer-rel.iso
echo "=== done $(date)"
