# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Lab helper (QEMU guest, VM B ssh port 2236); see docs/GUEST_SYSROLE.md and docs/TOOLING.md.
K=/var/tmp/wex-g4-lab-credentials/vm_key
SSH="ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1"
$SSH "sudo -n iconv -f UTF-16 -t UTF-8 /var/lib/wineexplorer/machine/drive_c/ProgramData/Microsoft/EdgeUpdate/Log/MicrosoftEdgeUpdate.log | grep -a '23:4[89]\|23:5' | grep -a -i -E 'fail|error|0x8|is machine|handoff' | cut -c1-300 | head -30; ls /var/lib/wineexplorer/machine/drive_c/Program\ Files\ \(x86\)/Microsoft/Edge/Application 2>&1 | head -3; grep -a -c 'nodrv' /tmp/gsr-out/sdhold.log; grep -a 'err:' /tmp/gsr-out/sdhold.log | grep -a -v 'libEGL\|systray' | cut -c1-220 | sort | uniq -c | sort -rn | head -12" 2>&1 | grep -v Warning
