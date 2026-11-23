import os, re, sys
S = '/home/debian/uwp-u1-20261111/build/candidates/u12/source'
apply = len(sys.argv) > 1 and sys.argv[1] == 'apply'
p = S + '/configure.ac'
t = open(p).read()
lines = t.split('\n')
out = []
declared = set()
removed = []
for ln in lines:
    m = re.match(r'WINE_CONFIG_MAKEFILE\(([^),]+)', ln)
    if m:
        d = m.group(1).strip()
        mk = os.path.join(S, d, 'Makefile.in')
        if not os.path.isfile(mk) and not d.startswith('tools') and d not in ('Makefile', 'include'):
            removed.append(d)
            continue
        declared.add(d)
    out.append(ln)
added = []
first = next(i for i, l in enumerate(out) if l.startswith('WINE_CONFIG_MAKEFILE('))
for kind in ('dlls', 'programs'):
    for name in sorted(os.listdir(os.path.join(S, kind))):
        d = kind + '/' + name
        if os.path.isfile(os.path.join(S, d, 'Makefile.in')) and d not in declared:
            added.append(d)
for d in added:
    out.insert(first, 'WINE_CONFIG_MAKEFILE(' + d + ')')
print('removed', removed)
print('added', added)
if apply:
    open(p, 'w').write('\n'.join(out))
# include list
p = S + '/include/Makefile.in'
t = open(p).read()
miss = []
for m in re.finditer(r'^\t([A-Za-z0-9_.+/-]+) \\$', t, re.M):
    n = m.group(1)
    if os.path.isfile(os.path.join(S, 'include', n)): continue
    if n.endswith('.h') and os.path.isfile(os.path.join(S, 'include', n[:-2] + '.idl')): continue
    if n.endswith('.winmd'): continue
    miss.append(n)
print('include entries without a source', miss)
