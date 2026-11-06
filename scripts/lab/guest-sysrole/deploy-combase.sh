# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Lab helper (QEMU guest, VM B ssh port 2236); see docs/GUEST_SYSROLE.md and docs/TOOLING.md.
K=/var/tmp/wex-g4-lab-credentials/vm_key
L=/mnt/d/DeepThought/ChromiumLab/guest-sysrole
SSH="ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1"
SC="scp -i $K -P 2236 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
$SC $L/combase/x86_64-windows/combase.dll debian@127.0.0.1:/tmp/combase-64.dll 2>&1 | grep -v Warning
$SC $L/combase/i386-windows/combase.dll debian@127.0.0.1:/tmp/combase-32.dll 2>&1 | grep -v Warning
$SSH "R=/opt/wex-rt2/build/wow64/dlls/combase; sudo -n pkill -x wineserver; sleep 2; sudo -n cp -n \$R/x86_64-windows/combase.dll \$R/x86_64-windows/combase.dll.orig; sudo -n cp -n \$R/i386-windows/combase.dll \$R/i386-windows/combase.dll.orig; sudo -n install -m 755 /tmp/combase-64.dll \$R/x86_64-windows/combase.dll; sudo -n install -m 755 /tmp/combase-32.dll \$R/i386-windows/combase.dll; ls -la \$R/*/combase.dll" 2>&1 | grep -v Warning
