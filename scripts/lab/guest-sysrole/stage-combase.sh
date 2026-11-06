# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Lab helper (QEMU guest, VM B ssh port 2236); see docs/GUEST_SYSROLE.md and docs/TOOLING.md.
set -e
B=/root/WE-prefetch/build/wow64/dlls/combase
D=/mnt/d/DeepThought/ChromiumLab/guest-sysrole/combase
mkdir -p $D/x86_64-windows $D/i386-windows
cp $B/x86_64-windows/combase.dll $D/x86_64-windows/combase.dll
cp $B/i386-windows/combase.dll $D/i386-windows/combase.dll
ls -la $D/*/combase.dll
