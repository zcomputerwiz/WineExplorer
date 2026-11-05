#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Step 2 (root on the guest): elevation Helper value in the machine root, and a normal test user with a fresh prefix.
set -eu
M=/var/lib/wineexplorer/machine
R=/home/debian/development-validation-20261101/memory-runtime2/build/wow64
if ! grep -q 'Software\\\\Wine\\\\Elevation' $M/system.reg; then
  cp -p $M/system.reg $M/system.reg.bak-gsr
  printf '\n[Software\\\\Wine\\\\Elevation] 1700000000\n#time=1d9a0000000000\n"Helper"="C:\\\\windows\\\\system32\\\\consent.exe"\n' >> $M/system.reg
fi
grep -n -A3 'Software\\\\Wine\\\\Elevation' $M/system.reg | head -6
id wextest >/dev/null 2>&1 || useradd -m -s /bin/bash wextest
id wextest
ls -la /home/wextest/.wine 2>&1 | head -5
