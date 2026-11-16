#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# gpo-lab-run.sh ARGS - run installer/tests/ad/gpo-lab.sh from a copy with Unix line ends (docs/GPO_LAB.md).
#
# A Windows checkout has CRLF line ends; bash and the guests need LF. As root in the lab's WSL distribution
# (this file itself may have CRLF there, so it is read through sed, and the tree is named in WEX_GPO_TREE):
#   T=/mnt/d/DeepThought/Projects/<worktree>
#   WEX_GPO_TREE=$T bash <(sed 's/\r$//' $T/scripts/lab/gpo-lab-run.sh) up
# The copy is /root/wex-gpo-lab-src/<worktree> (installer/tests/ad, scripts/lab and two tools' sources), made again
# at every call.
set -euo pipefail
D=${WEX_GPO_TREE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
C=/root/wex-gpo-lab-src/$(basename "$D")
rm -rf "$C"
mkdir -p "$C/installer/tests" "$C/scripts"
cp -a "$D/installer/tests/ad" "$C/installer/tests/ad"
cp -a "$D/installer/scripts" "$C/installer/scripts"
cp -a "$D/scripts/lab" "$C/scripts/lab"
cp -a "$D/scripts/vmtest.py" "$C/scripts/vmtest.py"   # machine.sh drives the installed VM with it
cp -a "$D/scripts/wex-domain-admit-pam.sh" "$C/scripts/wex-domain-admit-pam.sh"   # (the PAM step's script, likewise)
mkdir -p "$C/tools"
cp -a "$D/tools/wex-sysrun" "$D/tools/wex-accounts" "$C/tools/"   # machine.sh with WEX_LAB_TREE_TOOLS=1 builds them
mkdir -p "$C/wine/server"                                         # (wex-accounts includes the server's account code)
cp -a "$D/wine/server/accounts.c" "$D/wine/server/accounts.h" "$D/wine/server/sysrun.h" "$C/wine/server/"
find "$C" -type f \( -name '*.sh' -o -name '*.py' \) -exec sed -i 's/\r$//' {} +
exec bash "$C/installer/tests/ad/gpo-lab.sh" "$@"
