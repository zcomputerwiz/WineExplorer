#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: before copying a changed Wine file over the candidate's tree (the candidate is built from an older base), count the lines
# that differ between the pushed copy and the candidate's file. When the count is the size of one's own change, copy the file;
# else apply the change with a patch script. Push each file first as dc-<path with / as _>:
#   lab.sh push dc-dlls_d2d1_bitmap.c ; candidate-diff.sh dlls/d2d1/bitmap.c
g=${UWP_WS:-/home/debian/uwp-u1-20261111}
C=$g/build/candidates/${CAND:-u13}
for f in "$@"; do
  p=$g/claude/dc-$(echo $f | tr / _)
  echo "$f: $(diff <(tr -d '\r' < $p) <(tr -d '\r' < $C/source/$f 2>/dev/null) | grep -c '^[<>]') lines differ"
done
