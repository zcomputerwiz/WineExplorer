#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# apply the delta and build only the application data DLL for a quick compile check; prints the diagnostics
set -u
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
tar -xf $root/claude/u14.tar -C $c/source
cd $c/build
make dlls/windows.storage.applicationdata/all dlls/windows.storage.applicationdata/tests/all 2>&1 | grep -E 'error|warning' | head -60 | cut -c1-260
echo make-done
