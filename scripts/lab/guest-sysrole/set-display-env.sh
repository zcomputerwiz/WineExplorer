# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Lab helper (QEMU guest, VM B ssh port 2236); see docs/GUEST_SYSROLE.md and docs/TOOLING.md.
K=/var/tmp/wex-g4-lab-credentials/vm_key
SSH="ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1"
$SSH "sudo -n pkill -x wineserver; sleep 2; for kv in WAYLAND_DISPLAY=wex-shell-0 XDG_RUNTIME_DIR=/run/user/1002 WEX_WAYLAND_NATIVE_WINDOWS=1; do sudo -n python3 /tmp/gsr/guest-env-debug.py \${kv#*=} \${kv%%=*}; done; sudo -n grep -a -c 'WAYLAND_DISPLAY' /var/lib/wineexplorer/machine/system.reg" 2>&1 | grep -v Warning
