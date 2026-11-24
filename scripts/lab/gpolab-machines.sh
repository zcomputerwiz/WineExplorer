#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# gpolab-machines.sh [GUEST...] - the installed-machine guests on the existing disk /root/vmtest-gpo/wex-gpo
# (gpo-lab.sh machines; docs/GPO_LAB.md), without building a new image. As root in wexgpo. Guests: machine, join,
# join-ps (default all). WEX_LAB_TREE_TOOLS=1 and WEX_LAB_TREE_GPUPDATE=1 put this tree's tools and gpupdate script
# into the overlays. Log /var/tmp/gpolab-machines.log.
T=${WEX_GPO_TREE:-/mnt/d/DeepThought/Projects/WineExplorer-gpo}
export VMTEST_DIR=/root/vmtest-gpo WEX_LAB_EXTRA=${WEX_LAB_EXTRA:-/root/lab-extra} WEX_LAB_TREE_TOOLS=1 WEX_LAB_TREE_GPUPDATE=1
echo "started $(date +%T)" > /var/tmp/gpolab-machines.log
WEX_GPO_TREE=$T bash <(sed 's/\r$//' "$T/scripts/lab/gpo-lab-run.sh") machines wex-gpo "$@" >> /var/tmp/gpolab-machines.log 2>&1
echo "EXIT=$? $(date +%T)" >> /var/tmp/gpolab-machines.log
sed -n '/^=== results/,$p' /var/tmp/gpolab-machines.log
