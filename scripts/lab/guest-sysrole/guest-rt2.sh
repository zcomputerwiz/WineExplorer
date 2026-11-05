#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root on the guest: root-owned copy of the CURRENT wexbuild runtime (optionally with a replaced server/wineserver from /tmp/wineserver-instr).
set -eu
D=/opt/wex-rt2/build/wow64
rm -rf /opt/wex-rt2; mkdir -p $D
tar xf /tmp/build-wow64.tar -C $D
[ ! -f /tmp/wineserver-instr ] || { cp /tmp/wineserver-instr $D/server/wineserver; chmod 755 $D/server/wineserver; }
chown -R root:root /opt/wex-rt2; chmod -R go-w /opt/wex-rt2
rm -f /tmp/build-wow64.tar
df -h / | tail -1
ls -la $D/loader/wine $D/server/wineserver
