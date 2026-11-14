#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# finish the build of candidate $1 (make -j2) in the background
root=/home/debian/uwp-u1-20261111
cd $root/build/candidates/$1/build && make -j2 > $root/claude/logs/guest-make.log 2>&1; echo "rc=$?" >> $root/claude/logs/guest-make.log
