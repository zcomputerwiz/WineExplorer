# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Lab helper (QEMU guest, VM B ssh port 2236); see docs/GUEST_SYSROLE.md and docs/TOOLING.md.
K=/var/tmp/wex-g4-lab-credentials/vm_key
SSH="ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1"
scp -i $K -P 2236 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null /mnt/d/DeepThought/ChromiumLab/guest-sysrole/probes/shellexec_probe32.exe debian@127.0.0.1:/tmp/gsr-out/ 2>&1 | grep -v Warning
# arguments after the script name: FILE PARAMS VERB (defaults: cmd /c exit 7)
F=${1:-'C:\windows\system32\cmd.exe'}
P=${2:-'/c exit 7'}
$SSH "rm -f /tmp/gsr-out/probe.txt; chmod 755 /tmp/gsr-out/shellexec_probe32.exe; WEXTIMEOUT=200 sudo -n bash /tmp/gsr/guest-elev-run.sh 'Z:\\tmp\\gsr-out\\shellexec_probe32.exe' '$F' '$P' > /tmp/gsr-out/elev-run-probe.txt 2>&1; cut -c1-250 /tmp/gsr-out/elev-run-probe.txt; echo ---; cat /tmp/gsr-out/probe.txt; grep -a 'trace:exec' /tmp/gsr-out/sdhold.log | cut -c1-300 | head" 2>&1 | grep -v Warning
