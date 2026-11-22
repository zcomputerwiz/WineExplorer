#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
exec > $root/claude/logs/mach-run.log 2>&1
$c/build/server/wineserver -k 2>/dev/null; sleep 2
rm -rf $c/mach-run1 $c/mach-run2
cd $root/claude
WEX_MACHINE=1 bash test-wexappx.sh $c/build $root/claude/fixture $c/mach-run1 > logs/mach-test.out 2>&1
echo "machine mode:"; grep -v "malformed archive refused" logs/mach-test.out | grep -v "^PASS" | tail -20; grep -c "^PASS" logs/mach-test.out
$c/build/server/wineserver -k 2>/dev/null; sleep 2
bash test-wexappx.sh $c/build $root/claude/fixture $c/mach-run2 > logs/user-test.out 2>&1
echo "user mode:"; grep -v "malformed archive refused" logs/user-test.out | grep -v "^PASS" | tail -20; grep -c "^PASS" logs/user-test.out
echo mach-run-done
