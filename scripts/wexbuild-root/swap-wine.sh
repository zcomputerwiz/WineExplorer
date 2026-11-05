#!/bin/bash
# swap-wine.sh STAGE_DIR : replace the Wine runtime on the prefetch-fresh2 disk with STAGE_DIR/usr/local
set -e
S=$1
cd /root/WE-prefetch && export VMTEST_DIR=/root/vmtest
python3 scripts/vmtest.py stop prefetch-fresh2 >/dev/null 2>&1 || true
sleep 3
qemu-nbd -c /dev/nbd0 /root/vmtest/prefetch-fresh2/disk.qcow2; sleep 2
mount /dev/nbd0p1 /mnt/new
rm -rf /mnt/new/usr/local/lib/wine.cur
mv /mnt/new/usr/local/lib/wine /mnt/new/usr/local/lib/wine.cur
cp -a $S/usr/local/lib/wine /mnt/new/usr/local/lib/wine
for f in wine wine64 wineserver wine-preloader wineboot; do [ -e $S/usr/local/bin/$f ] && cp -a $S/usr/local/bin/$f /mnt/new/usr/local/bin/$f; done
ls -la /mnt/new/usr/local/bin/wineserver
cd /; sync; umount /mnt/new; qemu-nbd -d /dev/nbd0 >/dev/null
echo swapped
