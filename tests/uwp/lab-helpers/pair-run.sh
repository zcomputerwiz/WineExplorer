#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: alpc-pair.exe on candidate u12, server and client as two processes of one token
C=/home/debian/uwp-u1-20261111/build/candidates/u12
export WINEPREFIX=$C/wasdk-run/prefix WINEDEBUG=-all DISPLAY=:99
cd /home/debian/uwp-u1-20261111/claude
for l in 1 2; do
  ( $C/build/wine alpc-pair.exe server pr$l 20 > /tmp/pair-srv.txt 2>&1 & )
  sleep 4
  timeout 30 $C/build/wine alpc-pair.exe client pr$l $l 2>&1 | tr -d '\r'
  sleep 3
  echo "Wine level $l:"; tr -d '\r' < /tmp/pair-srv.txt
done
