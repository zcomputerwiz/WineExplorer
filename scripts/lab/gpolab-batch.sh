#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# gpolab-batch.sh [BRANCH [SHA]] - the gpolab check of a batch head (docs/GPO_LAB.md, "Checking a batch head").
# As root in wexgpo, from the GPO worktree on D: (WEX_GPO_TREE names it; default /mnt/d/DeepThought/Projects/WineExplorer-gpo):
#   1. an ISO of BRANCH (default master) built and installed into /root/vmtest-gpo/wex-gpo (wexgpo-iso.sh), the clone
#      checked to be at SHA when one is given;
#   2. join-ps's extra files (mi and wbemprox dlls, the WexComputer module, join-ps.ps1) taken from that clone and build;
#   3. the three installed-machine guests on a base disk of it (gpo-lab.sh machines).
# Log /var/tmp/gpolab-batch.log; the result lines are printed at the end. About 40 minutes with a warm ccache.
BRANCH=${1:-master} WANT=${2:-}
T=${WEX_GPO_TREE:-/mnt/d/DeepThought/Projects/WineExplorer-gpo}
L=/var/tmp/gpolab-batch.log
echo "started $(date +%T) $BRANCH $WANT" > $L
# the scripts run from an LF copy (a Windows checkout has CRLF)
rm -rf /root/wexgpo-lab && cp -a "$T/scripts/lab" /root/wexgpo-lab && find /root/wexgpo-lab -type f -exec sed -i 's/\r$//' {} +
bash /root/wexgpo-lab/wexgpo-iso.sh "$BRANCH" >> $L 2>&1
rc=$?
HEAD=$(git -C /root/WE-iso rev-parse HEAD)
echo "iso rc=$rc clone=$HEAD $(date +%T)" >> $L
[ $rc = 0 ] || { echo "ISO FAILED"; tail -15 $L; exit 1; }
if [ -n "$WANT" ] && [ "${HEAD#"$WANT"}" = "$HEAD" ]; then echo "the clone is $HEAD, not $WANT"; exit 1; fi
X=/root/lab-extra-batch B=/root/WE-iso/build/wow64
rm -rf $X && mkdir -p $X/dlls/x86_64-windows $X/dlls/i386-windows || exit 1
for a in x86_64-windows i386-windows; do
    for d in mi wbemprox; do cp "$B/dlls/$d/$a/$d.dll" "$X/dlls/$a/" || { echo "no $B/dlls/$d/$a/$d.dll"; exit 1; }; done
done
cp -a /root/WE-iso/src/powershell/WexComputer $X/ && cp /root/WE-iso/installer/tests/ad/join-ps.ps1 $X/ || exit 1
export VMTEST_DIR=/root/vmtest-gpo WEX_LAB_EXTRA=$X
WEX_GPO_TREE=$T bash <(sed 's/\r$//' "$T/scripts/lab/gpo-lab-run.sh") machines wex-gpo >> $L 2>&1
echo "EXIT=$? $(date +%T)" >> $L
grep -E '^=== [a-z]|^iso rc' $L
sed -n '/^=== results/,$p' $L
