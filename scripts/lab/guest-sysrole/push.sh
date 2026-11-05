# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
K=/var/tmp/wex-g4-lab-credentials/vm_key
SSH="ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1"
SCP="scp -r -i $K -P 2236 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
P=/mnt/d/DeepThought/Projects/WineExplorer
L=/mnt/d/DeepThought/ChromiumLab/guest-sysrole
$SSH 'rm -rf /tmp/gsr; mkdir -p /tmp/gsr/repo/scripts /tmp/gsr/repo/tools' 2>&1 | grep -v Warning
$SCP $P/scripts/setup-machine-root.sh debian@127.0.0.1:/tmp/gsr/repo/scripts/ 2>/dev/null
$SCP $P/tools/wex-sysrun debian@127.0.0.1:/tmp/gsr/repo/tools/ 2>/dev/null
$SSH "mkdir -p /tmp/gsr/repo/src/resources"; $SCP $P/src/resources/fonts debian@127.0.0.1:/tmp/gsr/repo/src/resources/ 2>/dev/null
$SCP $L/wex-sysrun $L/guest-sysrole-provision.sh debian@127.0.0.1:/tmp/gsr/ 2>/dev/null
$SSH 'chmod +x /tmp/gsr/wex-sysrun /tmp/gsr/guest-sysrole-provision.sh; sudo -n bash /tmp/gsr/guest-sysrole-provision.sh' 2>&1 | grep -v Warning
