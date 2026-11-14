#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
root=/home/debian/uwp-u1-20261111
c=$1; t=$2
bash $root/claude/guest-u1-run.sh $c u1-run-$t > $root/claude/logs/final-$t.u1 2>&1
bash $root/claude/guest-install2.sh $c install2-$t > $root/claude/logs/final-$t.install 2>&1
bash $root/claude/guest-install-test.sh $c install-$t > $root/claude/logs/final-$t.install1 2>&1
echo done > $root/claude/logs/final-$t.done
