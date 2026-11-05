# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
K=/var/tmp/wex-g4-lab-credentials/vm_key
scp -i $K -P 2236 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null /mnt/d/DeepThought/ChromiumLab/guest-sysrole/guest-env-debug.py /mnt/d/DeepThought/ChromiumLab/guest-sysrole/guest-elev-msi.sh /mnt/d/DeepThought/ChromiumLab/guest-sysrole/guest-elev-run.sh debian@127.0.0.1:/tmp/gsr/ 2>&1 | grep -v Warning
ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1 "sudo -n python3 -I /tmp/gsr/guest-env-debug.py $1; sudo -n bash /tmp/gsr/guest-elev-msi.sh googlechromestandaloneenterprise64.msi chrome > /tmp/gsr-out/elev-run-chrome.txt 2>&1; grep -a -c '' /tmp/gsr-out/sdhold.log" 2>&1 | grep -v Warning
