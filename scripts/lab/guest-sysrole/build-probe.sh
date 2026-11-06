# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Lab helper (QEMU guest, VM B ssh port 2236); see docs/GUEST_SYSROLE.md and docs/TOOLING.md.
set -e
i686-w64-mingw32-gcc -municode -o /mnt/d/DeepThought/ChromiumLab/guest-sysrole/probes/shellexec_probe32.exe /mnt/d/DeepThought/Projects/WineExplorer/scripts/shellexec_probe.c -lshell32
ls -la /mnt/d/DeepThought/ChromiumLab/guest-sysrole/probes/shellexec_probe32.exe
