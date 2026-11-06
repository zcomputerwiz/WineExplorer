# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Lab helper (QEMU guest, VM B ssh port 2236); see docs/GUEST_SYSROLE.md and docs/TOOLING.md.
K=/var/tmp/wex-g4-lab-credentials/vm_key
SSH="ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1"
$SSH "sudo -n rm -f /var/lib/wineexplorer/machine/drive_c/windows/Installer/*.msi /tmp/gsr-out/elev-run-edge.txt; (WEXTIMEOUT=600 WEXDISPLAY=1 nohup sudo -n --preserve-env=WEXDISPLAY,WEXTIMEOUT bash /tmp/gsr/guest-elev-msi.sh MicrosoftEdgeEnterpriseX64.msi edge > /tmp/gsr-out/elev-run-edge.txt 2>&1 &); echo started" 2>&1 | grep -v Warning
