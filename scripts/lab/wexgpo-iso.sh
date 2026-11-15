#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wexgpo-iso.sh BRANCH - on the build machine wexgpo (docs/GPO_LAB.md): a session 0 ISO of BRANCH, installed
# unattended into /root/vmtest-gpo/wex-gpo, the disk that "gpo-lab.sh machine" takes. As root. Steps, each
# logged in /root/gpo-iso-*.log:
#   deps     the ISO's build dependencies (live-build's packages are there from wexgpo-provision.sh; labwc's
#            build dependencies need the source lists)
#   clone    /root/WE-iso, BRANCH with its submodules, from GitHub (the branch must be pushed)
#   package  WEX_SESSION0=1 package-wine.sh with the build in /root/WE-prefetch/build/wow64, which wex-run.sh made
#            from the same commits (checked: the two wine commits must match)
#   iso      WEX_SESSION0=1 build-iso.sh --clean --unattended -> /root/out/wex-gpo.iso
#   install  install-unattended.sh -> /root/vmtest-gpo/wex-gpo (the VM ends stopped)
# package-wine.sh fetches third-party assets (agreed by the owner on 2026-11-09). About 1.5 hours.
set -uo pipefail
BRANCH=${1:?branch}
R=/root/WE-iso
B=/root/WE-prefetch/build/wow64
L=/root/gpo-iso
step() { echo "=== $1 $(date +%H:%M)"; }

step deps
if ! grep -q '^Types:.*deb-src' /etc/apt/sources.list.d/debian.sources; then
    sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources
fi
export DEBIAN_FRONTEND=noninteractive
{ apt-get update -q && apt-get build-dep -y -q labwc &&
  apt-get install -y -q live-build debootstrap squashfs-tools xorriso mtools msitools zstd unzip golang-go \
      debhelper grub-pc-bin systemd-boot-efi python3-pefile python3-pil tesseract-ocr imagemagick; } > $L-deps.log 2>&1 ||
    { echo "DEPS FAILED"; tail -5 $L-deps.log; exit 1; }

step clone
rm -rf "$R"
git clone -q --recurse-submodules -b "$BRANCH" https://github.com/zcomputerwiz/WineExplorer.git "$R" > $L-clone.log 2>&1 ||
    { echo "CLONE FAILED"; tail -5 $L-clone.log; exit 1; }
git -C "$R" log --oneline -1
W1=$(git -C "$R/wine" rev-parse HEAD); W2=$(git -C /root/WE-prefetch/wine rev-parse HEAD)
[ "$W1" = "$W2" ] || { echo "the build in $B is of wine $W2, the branch pins $W1: run wex-run.sh build first"; exit 1; }

step package
cd "$R" || exit 1
WEX_SESSION0=1 BUILD_DIR="$B" timeout 3000 bash installer/package-wine.sh > $L-package.log 2>&1 ||
    { echo "PACKAGE FAILED"; tail -5 $L-package.log; exit 1; }

step iso
mkdir -p /root/out
WEX_SESSION0=1 WORK_DIR=/root/iso-build-gpo OUT_ISO=/root/out/wex-gpo.iso BUILD_DIR="$B" timeout 4200 \
    bash installer/build-iso.sh --clean --unattended > $L-iso.log 2>&1 || { echo "ISO FAILED"; tail -8 $L-iso.log; exit 1; }
ls -lh /root/out/wex-gpo.iso
rm -rf /root/iso-build-gpo

step install
export VMTEST_DIR=/root/vmtest-gpo
mkdir -p "$VMTEST_DIR"
timeout 3300 bash installer/tests/install-unattended.sh /root/out/wex-gpo.iso wex-gpo > $L-install.log 2>&1 ||
    { echo "INSTALL FAILED"; tail -8 $L-install.log; exit 1; }
tail -3 $L-install.log
step done
echo GPO-ISO-DONE
