#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
root=/home/debian/uwp-u1-20261111
cp $root/claude/register-wine.py $root/claude/fixture/register-wine.py
cp $root/claude/u11fix/u1_0/x64/launcher.exe $root/claude/fixture/x64/launcher.exe 2>/dev/null
WEX_U11_DIR=$root/claude/u11fix/u1_1 bash $root/claude/test-wexappx.sh $root/build/candidates/$1/build $root/claude/fixture $root/build/candidates/$1/$2
