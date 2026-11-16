# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""The Windows Runtime classes a source tree uses (C#, C++/CX, XAML code-behind), by the Windows SDK's IDL: each class with
how it is used — constructed (needs its activation factory), its statics called, or only named (instances come from
elsewhere). usage: winrt-uses.py SDK_WINRT_IDL_DIR SRC_DIR OUT_TSV"""
import os, re, sys
from collections import defaultdict

idl_dir, src_dir, out = sys.argv[1:4]

# ---- the classes of the SDK: namespace, name, activatable (default or factory), statics ----
classes = {}
for f in os.listdir(idl_dir):
    if not f.endswith('.idl') or not f.startswith('windows.'): continue
    text = open(os.path.join(idl_dir, f), encoding='utf-8', errors='replace').read()
    stack, depth_ns = [], []
    attrs = []
    depth = 0
    for line in text.split('\n'):
        s = line.strip()
        m = re.match(r'namespace\s+([\w.]+)', s)
        if m:
            stack.append(m.group(1)); depth_ns.append(depth)
        if s.startswith('['):
            attrs.append(s)
        m = re.match(r'(?:\[[^\]]*\]\s*)*runtimeclass\s+(\w+)\s*(?::\s*[\w.]+\s*)?$', s)
        if m:
            full = '.'.join(stack) + '.' + m.group(1)
            a = ' '.join(attrs)
            classes[full] = {
                'activatable': 'activatable(' in a,
                'default_ctor': bool(re.search(r'activatable\(Windows\.Foundation\.\w+Contract', a)),
                'statics': re.findall(r'static\(([\w.]+)', a),
                'idl': f,
            }
        if s and not s.startswith('['):
            if not s.startswith('runtimeclass') or s.endswith(';'): attrs = [] if not s.startswith('[') else attrs
        depth += line.count('{') - line.count('}')
        while depth_ns and depth <= depth_ns[-1] and '}' in line:
            depth_ns.pop(); stack.pop()

with open(out + '.sdk', 'w', encoding='utf-8', newline='\n') as o:
    o.write('\n'.join(sorted(classes)) + '\n')
by_ns = defaultdict(dict)
for full in classes:
    ns, name = full.rsplit('.', 1)
    by_ns[ns][name] = full

# ---- the sources ----
uses = defaultdict(lambda: defaultdict(set))   # class -> kind -> files
for root, dirs, files in os.walk(src_dir):
    if any(p in root for p in ('Test', 'test')): continue
    for f in files:
        if not f.endswith(('.cs', '.cpp', '.h')): continue
        path = os.path.join(root, f)
        rel = os.path.relpath(path, src_dir).replace(os.sep, '/')
        text = open(path, encoding='utf-8', errors='replace').read()
        text = re.sub(r'//[^\n]*|/\*.*?\*/', ' ', text, flags=re.S)
        cpp = f.endswith(('.cpp', '.h'))
        norm = text.replace('::', '.')
        imported = set(re.findall(r'using\s+namespace\s+(Windows[\w.:]*)\s*;', text))
        imported = {n.replace('::', '.') for n in imported}
        imported |= set(re.findall(r'^\s*using\s+(Windows[\w.]*)\s*;', text, flags=re.M))
        # aliases: using X = Windows.Y.Z;
        aliases = dict(re.findall(r'using\s+(\w+)\s*=\s*(Windows[\w.]+)\s*;', text))

        def resolve(name):
            if name.startswith('Windows.') and name in classes: return name
            if name in aliases and aliases[name] in classes: return aliases[name]
            for ns in imported:
                if name in by_ns.get(ns, {}): return by_ns[ns][name]
            return None

        for m in re.finditer(r'\b((?:Windows\.)?[A-Z][\w.]*?)\b(\s*\.\s*\w+|\s*\^|\s*\()?', norm):
            name = m.group(1).rstrip('.')
            full = resolve(name)
            if not full: continue
            before = norm[max(0, m.start() - 8):m.start()]
            after = m.group(2) or ''
            if re.search(r'\bnew\s*$', before) or re.search(r'\bref new\s*$', before):
                kind = 'constructed'
            elif after.strip().startswith('.') and re.match(r'\s*\.\s*[A-Z]', after):
                kind = 'statics'
            else:
                kind = 'named'
            uses[full][kind].add(rel)

with open(out, 'w', encoding='utf-8', newline='\n') as o:
    o.write('class\tconstructed\tstatics\tnamed\tactivatable\tstatic_ifaces\tfiles\n')
    for full in sorted(uses):
        k = uses[full]
        c = classes[full]
        files = sorted(set().union(*k.values()))
        o.write(f"{full}\t{len(k['constructed'])}\t{len(k['statics'])}\t{len(k['named'])}\t{int(c['activatable'])}\t"
                f"{','.join(s.rsplit('.', 1)[1] for s in c['statics'])}\t{' '.join(files[:4])}\n")
print(len(classes), 'SDK classes;', len(uses), 'used')
