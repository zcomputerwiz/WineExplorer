#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: make the "not implemented, returning E_NOINTERFACE" messages of iertutil and wintypes print the caller (guest sources only)
C=/home/debian/uwp-u1-20261111/build/candidates/u12
S=$C/source/dlls
for f in $S/iertutil/main.c $S/wintypes/main.c $S/wintypes/diagnostics.c $S/wintypes/buffer.c; do
  sed -i 's|FIXME(\s*"%s not implemented, returning E_NOINTERFACE\.\\n",\s*debugstr_guid(\s*iid\s*)\s*);|FIXME( "%s not implemented, returning E_NOINTERFACE (caller %p).\\n", debugstr_guid( iid ), __builtin_return_address(0) );|' $f
  grep -c "caller %p" $f
done
cd $C/build
make -j6 dlls/iertutil/all dlls/wintypes/all 2>&1 | grep -E " error|Error" | head
echo built
