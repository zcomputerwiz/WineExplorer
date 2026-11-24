import re, sys, collections, bisect
log = sys.argv[1]
pats = sys.argv[2:]
mods = []
hits = collections.Counter()
for ln in open(log, errors='replace'):
    m = re.search(r'Loaded L"[^"]*\\([^"\\]+)" at ([0-9A-F]+):', ln)
    if m:
        mods.append((int(m.group(2), 16), m.group(1)))
        mods.sort()
        continue
    m = re.match(r'^[0-9a-f]{4}:Call (\S+)\(.*ret=([0-9a-f]+)', ln)
    if m and any(re.search(p, m.group(1)) for p in pats):
        r = int(m.group(2), 16)
        i = bisect.bisect(mods, (r, 'zzz')) - 1
        name = mods[i][1] if i >= 0 else '?'
        hits[(m.group(1), name)] += 1
for (f, mname), v in hits.most_common(60):
    print(v, f, '<-', mname)
