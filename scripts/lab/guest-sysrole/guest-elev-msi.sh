#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# LAB ONLY. Root on the guest: elevated MSI install as wexadm through the test broker. Args: msi file name, label.
# WINEDEBUG for the elevated process comes from the machine-wide Environment (guest-env-debug.py).
MSI=${1:-googlechromestandaloneenterprise64.msi}
LABEL=${2:-chrome}
cp /home/debian/development-validation-20261101/$MSI /tmp/gsr-out/$MSI 2>/dev/null; chmod 644 /tmp/gsr-out/$MSI
rm -f /tmp/gsr-out/$LABEL-elev.log
export WEXTIMEOUT=900
bash /tmp/gsr/guest-elev-run.sh msiexec.exe /i 'Z:\tmp\gsr-out\'"$MSI" /qn /norestart /l*v 'Z:\tmp\gsr-out\'"$LABEL"'-elev.log'
ls -la /tmp/gsr-out
