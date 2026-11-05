#!/bin/bash
# sync-wine.sh : copy every changed/untracked file of the D: wine tree to the wexbuild clone, then build
set -e
SRC=/mnt/d/DeepThought/Projects/WineExplorer/wine
DST=/root/WE-prefetch/wine
cd $SRC
FILES=$( (git diff --name-only; git ls-files --others --exclude-standard) | sort -u )
for f in $FILES; do [ "$f" = include/Makefile.in ] && continue; mkdir -p $DST/$(dirname $f); cp $f $DST/$f; echo sync $f; done
