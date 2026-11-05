#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Show failing assertions from the newest wexdev test logs: grep-test-fail.sh [module] [pattern]
mod=${1:-oleaut32}
pat=${2:-Test failed}
for f in $(ls -t /root/wexdev/logs/test-"$mod"-*.log | head -2); do
    echo "== $f"
    grep -a -n -- "$pat" "$f" | head -20
done
