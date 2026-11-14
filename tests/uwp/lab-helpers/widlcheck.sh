root=/home/debian/uwp-u1-20261111; c=$root/build/candidates/u11-2
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
tar -xf $root/claude/idlonly.tar -C $c/source
cd $c/build && (timeout 50 make include/windows.storage.winmd include/windows.storage.h > /tmp/widl.log 2>&1; echo rc=$? >> /tmp/widl.log)
tail -5 /tmp/widl.log | cut -c1-220
grep -c "IFileIOStatics" include/windows.storage.h
true
