# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Lab helper (QEMU guest, VM B ssh port 2236); see docs/GUEST_SYSROLE.md and docs/TOOLING.md.
K=/var/tmp/wex-g4-lab-credentials/vm_key
SSH="ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1"
for i in $(seq 1 95); do
  if $SSH "grep -a -q '^exit ' /tmp/gsr-out/elev-run-edge.txt" 2>/dev/null; then break; fi
  sleep 6
done
$SSH "grep -a '^exit \|runas failed' /tmp/gsr-out/elev-run-edge.txt; grep -a 'exit code' /tmp/gsr-out/elev.log | tail -1; ls '/var/lib/wineexplorer/machine/drive_c/Program Files (x86)/Microsoft/Edge/Application' 2>&1 | head -4; sudo -n iconv -f UTF-16 -t UTF-8 /var/lib/wineexplorer/machine/drive_c/ProgramData/Microsoft/EdgeUpdate/Log/MicrosoftEdgeUpdate.log | grep -a -i -E 'Failed to|hand ?off exit|install(ation)? (complete|succe)|error code' | cut -c1-240 | tail -12; grep -a 'err:' /tmp/gsr-out/sdhold.log | grep -a -v 'libEGL\|systray\|waylanddrv' | cut -c1-200 | sort | uniq -c | sort -rn | head -10" 2>&1 | grep -v Warning
