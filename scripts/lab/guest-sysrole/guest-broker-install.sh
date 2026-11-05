#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# LAB ONLY. Root on the guest: install the TEST elevation broker (compile-time approval macro, never shipped) into the
# machine root, create the filtered administrator test user wexadm, and the policy directory with approve.all.
set -eu
M=/var/lib/wineexplorer/machine
SYS32=$M/drive_c/windows/system32
P=/tmp/gsr/probes
GRP=wexsys
[ -d $P ] || { echo "no $P"; exit 1; }
[ -e $M/system.reg.bak-broker ] || cp -p $M/system.reg $M/system.reg.bak-broker
install -m 775 -o root -g $GRP $P/wexelevate_test.exe $SYS32/wexelevate_test.exe
install -m 775 -o root -g $GRP $P/wexelevate_stub.exe $SYS32/wexelevate_stub.exe
if ! grep -q 'Services\\\\wexelevate\]' $M/system.reg; then
  printf '\n[System\\\\ControlSet001\\\\Services\\\\wexelevate] 1700000000\n#time=1d9a0000000000\n"ImagePath"="C:\\\\windows\\\\system32\\\\wexelevate_test.exe"\n' >> $M/system.reg
fi
grep -n -A3 'Services\\\\wexelevate\]' $M/system.reg | head -5
install -d -m 1777 /tmp/wexelev
touch /tmp/wexelev/approve.all; chmod 666 /tmp/wexelev/approve.all
install -m 755 $P/sdprobe64.exe /opt/wex-lab-sdprobe64.exe
install -m 755 $P/runas_wait.exe /opt/wex-lab-runas_wait.exe
install -m 755 $P/runas_wait32.exe /opt/wex-lab-runas_wait32.exe
id wexadm >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo wexadm
id wexadm
echo BROKER_INSTALLED
