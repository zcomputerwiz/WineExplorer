#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: full rebuild of candidate u12 (every module that uses the server protocol must be rebuilt after the protocol changed)
C=/home/debian/uwp-u1-20261111/build/candidates/u12
cp /home/debian/uwp-u1-20261111/claude/exception-clean.c $C/source/dlls/ntdll/exception.c
cd $C/build
date > /tmp/full-build.log
make -j8 >> /tmp/full-build.log 2>&1
echo "make exit $?" >> /tmp/full-build.log
date >> /tmp/full-build.log
tail -5 /tmp/full-build.log
