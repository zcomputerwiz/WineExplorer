# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Lab helper (QEMU guest, VM B ssh port 2236); see docs/GUEST_SYSROLE.md and docs/TOOLING.md.
K=/var/tmp/wex-g4-lab-credentials/vm_key
for f in guest-elev-run.sh guest-env-debug.py; do
scp -i $K -P 2236 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null /mnt/d/DeepThought/ChromiumLab/guest-sysrole/$f debian@127.0.0.1:/tmp/gsr/$f 2>&1 | grep -v Warning
done
