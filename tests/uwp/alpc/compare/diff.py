#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""usage: diff.py WINDOWS.txt WINE.txt : print a unified diff (Windows is '-', Wine is '+'); exit status 1 when they differ."""
import difflib
import sys

a = open(sys.argv[1], encoding="utf-8", errors="replace").read().splitlines()
b = open(sys.argv[2], encoding="utf-8", errors="replace").read().splitlines()
d = list(difflib.unified_diff(a, b, "windows", "wine", lineterm="", n=1))
print("\n".join(d))
sys.exit(1 if d else 0)
