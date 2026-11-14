#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
root=/home/debian/uwp-u1-20261111
b=$root/build/candidates/u11-2/build
r=$root/build/candidates/u11-2/install2-1
export WINEPREFIX=$r/prefix WINESERVER=$b/server/wineserver WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=
timeout 120 $b/wine reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f > /dev/null 2>&1
timeout 120 $b/wine wexappx install --development "Z:$r/pkg/u11-x64.appx" 2>&1 | tr -d '\r'
ls $WINEPREFIX/appx-catalog/
iconv -f UTF-16LE -t UTF-8 $WINEPREFIX/appx-catalog/*U1View*.wexappx | cat -A | head
timeout 60 $WINESERVER -k; timeout 60 $WINESERVER -w
