#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# usage: pe-imports.sh DIR... : DLL names imported by the PE files in the directories (not found in them, nor among Wine's builtin names)
W=/mnt/c/Users/Owner/.codex/worktrees/uwp-runtime/wine/dlls
ls $W | tr 'A-Z' 'a-z' | sed 's/$/.dll/' > /tmp/wine-names.txt
for d in "$@"; do find "$d" -type f \( -iname '*.dll' -o -iname '*.exe' \) ; done > /tmp/pe-files.txt
for d in "$@"; do ls "$d" "$d"/* 2>/dev/null; done | tr 'A-Z' 'a-z' | sort -u > /tmp/local-names.txt
: > /tmp/imports-all.txt
while read f; do
  objdump -p "$f" 2>/dev/null | awk -v F="$(basename "$f")" '/DLL Name:/{n=$3; print tolower(n), F}' >> /tmp/imports-all.txt
done < /tmp/pe-files.txt
awk '{print $1}' /tmp/imports-all.txt | sort -u | while read n; do
  case "$n" in api-ms-*|ext-ms-*) continue;; esac
  grep -qx "$n" /tmp/wine-names.txt && continue
  grep -qx "$n" /tmp/local-names.txt && continue
  echo "$n: $(grep "^$n " /tmp/imports-all.txt | awk '{print $2}' | sort -u | head -4 | tr '\n' ' ')"
done
