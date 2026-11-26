#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Normalise a probe's output so that two runs (Windows, Wine, two days) can be compared line by line.

usage: normalize.py [--extra REGEX;REGEX...] < raw > normalised

Replaced: pointer-sized hex values (9 or more digits) and handle-like 0x...(4 digits in a 'window'/'port'/'hwnd'/'handle' context)
by <ADDR>, millisecond timings, process/thread ids, carriage returns, Wine's graphics start-up noise; then the per-probe extra
regular expressions (each match becomes <N>).
"""
import re
import sys

extra = []
args = sys.argv[1:]
if args[:1] == ["--extra"]:
    extra = [re.compile(x.strip()) for x in args[1].split(";") if x.strip()]

noise = re.compile(r"^(libEGL warning|wine: |err:.*mscoree|fixme:)")
for line in sys.stdin:
    line = line.replace("\r", "").rstrip("\n")
    if noise.search(line):
        continue
    line = re.sub(r"0x[0-9a-fA-F]{9,}", "<ADDR>", line)
    line = re.sub(r"(?<!-)\b[0-9a-fA-F]{9,16}\b(?!\})", "<ADDR>", line)   # not the last group of a GUID
    line = re.sub(r"(window|hwnd|port|handle|packet)\s+(0x)?[0-9a-fA-F]{4,}", r"\1 <H>", line)
    line = re.sub(r"\+\d+ ?ms", "+<T>ms", line)
    line = re.sub(r"\b(pid|tid|thread)\s+\d+", r"\1 <ID>", line)
    for rx in extra:
        line = rx.sub("<N>", line)
    print(line)
