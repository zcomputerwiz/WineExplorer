# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
K=/var/tmp/wex-g4-lab-credentials/vm_key
F=$1
scp -i $K -P 2236 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null /mnt/d/DeepThought/ChromiumLab/guest-sysrole/$F debian@127.0.0.1:/tmp/gsr/ 2>&1 | grep -v Warning
ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1 "sudo -n bash /tmp/gsr/$F" 2>&1 | grep -v Warning
