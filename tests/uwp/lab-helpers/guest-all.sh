#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# the regression set on one candidate. usage: guest-all.sh CANDIDATE TAG
root=/home/debian/uwp-u1-20261111
c=$1; t=$2
bash $root/claude/guest-regress-ui.sh $c ui-$t > $root/claude/logs/all-$t.ui 2>&1
bash $root/claude/guest-regress.sh $c regress-$t > $root/claude/logs/all-$t.regress 2>&1
bash $root/claude/guest-u1-run.sh $c u1-run-$t > $root/claude/logs/all-$t.u1 2>&1
bash $root/claude/guest-install-test.sh $c install-$t > $root/claude/logs/all-$t.install 2>&1
echo done > $root/claude/logs/all-$t.done
