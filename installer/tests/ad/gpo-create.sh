#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Lab domain (docs/GROUP_POLICY.md, "The lab domain"): makes the test Group Policy object "WexLab" on the
# controller, linked to the domain, with a machine Registry.pol that sets
#   HKLM\Software\Policies\WexLab  Limit = 7 (REG_DWORD)
# and a user Registry.pol that sets
#   HKCU\Software\Policies\WexLab  UserLimit = 3 (REG_DWORD)
# The controller has to be running (dc-create.sh). Running it again replaces the two files and raises the
# object's version.
#
#   gpo-create.sh [MACHINE_NUMBER [USER_NUMBER]]     other numbers for the two values (to test a change)
set -euo pipefail

LAB=${WEX_AD_LAB:-/var/tmp/wex-ad-lab}
HERE=$(cd "$(dirname "$0")" && pwd)
MACHINE=${1:-7}
USER_VALUE=${2:-3}

lab_scp() { scp -q -i "$LAB/lab_key" -P 2241 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR "$@"; }

python3 -I "$HERE/../../../scripts/lab/make-preg.py" "$LAB/gpo-machine.pol" 'Software\Policies\WexLab' Limit "$MACHINE"
python3 -I "$HERE/../../../scripts/lab/make-preg.py" "$LAB/gpo-user.pol" 'Software\Policies\WexLab' UserLimit "$USER_VALUE"
lab_scp "$LAB/gpo-machine.pol" "$LAB/gpo-user.pol" "$HERE/gpo-guest.sh" root@127.0.0.1:/root/
bash "$HERE/dc-create.sh" ssh bash /root/gpo-guest.sh
