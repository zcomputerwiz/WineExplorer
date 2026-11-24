# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the calls a module makes in a +relay,+loaddll log, in order of first appearance: relay-calls-from-module.py LOG MODULE
import re, sys, collections, bisect
log, caller = sys.argv[1], sys.argv[2].lower()
skip = re.compile(r'^(ucrtbase|msvcp|vcruntime)\.|RtlFreeHeap|RtlAllocateHeap|HeapFree|Windows(Get|Delete|Dup|Create)|FlsGet|FlsSet|GetLastError|SetLastWin32|SRWLock|memmove|memcmp|memcpy|memset|RtlLookup|RtlInsert|RtlEnumerate|RtlDelete|RtlNumber|Interlocked|EncodePointer|DecodePointer|QueryPerformance|NtClose|InitializeCriticalSection|EnterCritical|LeaveCritical')
mods = []
first = {}
cnt = collections.Counter()
n = 0
for ln in open(log, errors='replace'):
    n += 1
    m = re.search(r'Loaded L"[^"]*\\([^"\\]+)" at ([0-9A-F]+):', ln)
    if m:
        mods.append((int(m.group(2), 16), m.group(1)))
        mods.sort()
        continue
    m = re.match(r'^([0-9a-f]{4}):Call (\S+)\(.*ret=([0-9a-f]+)', ln)
    if not m or skip.search(m.group(2)):
        continue
    r = int(m.group(3), 16)
    i = bisect.bisect(mods, (r, 'zzz')) - 1
    if i >= 0 and mods[i][1].lower() == caller:
        cnt[m.group(2)] += 1
        first.setdefault(m.group(2), (n, m.group(1)))
for k, v in sorted(cnt.items(), key=lambda kv: first[kv[0]][0]):
    print(first[k][0], first[k][1], v, k)
