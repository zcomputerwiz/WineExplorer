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
# The copy is /root/wex-gpo-lab-src/<worktree> (installer/tests/ad and scripts/lab), made again at every call.
set -euo pipefail
D=${WEX_GPO_TREE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}
C=/root/wex-gpo-lab-src/$(basename "$D")
rm -rf "$C"
mkdir -p "$C/installer/tests" "$C/scripts"
cp -a "$D/installer/tests/ad" "$C/installer/tests/ad"
cp -a "$D/scripts/lab" "$C/scripts/lab"
find "$C" -type f \( -name '*.sh' -o -name '*.py' \) -exec sed -i 's/\r$//' {} +
exec bash "$C/installer/tests/ad/gpo-lab.sh" "$@"
