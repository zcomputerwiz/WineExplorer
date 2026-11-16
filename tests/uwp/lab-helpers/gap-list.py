# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""The gap list: the Windows Runtime classes an application constructs or calls statics of (winrt-uses.py's TSV), against
the classes Wine registers (its modules' classes.idl) and the XAML classes XamlShim serves (WinmdGen/classes.txt).
usage: gap-list.py USES_TSV WINE_SRC SHIM_CLASSES_TXT [EXTRA_CLASSES_TXT]"""
import os, re, sys

uses_tsv, wine, shim_txt = sys.argv[1:4]
extra = set(open(sys.argv[4]).read().split()) if len(sys.argv) > 4 else set()

wine_classes = {}
for root, dirs, files in os.walk(os.path.join(wine, 'dlls')):
    if 'tests' in root: continue
    for f in files:
        if f != 'classes.idl': continue
        text = open(os.path.join(root, f), encoding='utf-8', errors='replace').read()
        for ns, body in re.findall(r'namespace\s+([\w.]+)\s*\{(.*?)\}', text, re.S):
            for name in re.findall(r'runtimeclass\s+(\w+)\s*;', body):
                wine_classes[f'{ns}.{name}'] = os.path.basename(root)
if shim_txt.endswith('.cs'):
    # the generated ABI: every class with a factory (listed classes and their bases)
    shim = set(re.findall(r'public override string ClassName => "([\w.]+)"', open(shim_txt, encoding='utf-8').read()))
else:
    lines = open(shim_txt, encoding='utf-8').read().split('\n')
    shim = {lines[i + 1].strip() for i, l in enumerate(lines) if l.strip() == '--class' and i + 1 < len(lines)}

rows = [l.rstrip('\n').split('\t') for l in open(uses_tsv, encoding='utf-8')][1:]
print('class\tuse\twhere\tstatics interfaces')
for full, new, statics, named, activatable, ifaces, files in rows:
    if int(new) == 0 and int(statics) == 0: continue
    use = ('new ' if int(new) else '') + ('statics' if int(statics) else '')
    if full.startswith('Windows.UI.Xaml.'):
        where = 'shim' if full in shim else 'MISSING (shim)'
    elif full in wine_classes:
        where = 'wine:' + wine_classes[full]
    elif full in extra:
        where = 'other'
    else:
        where = 'MISSING (wine)'
    print(f'{full}\t{use.strip()}\t{where}\t{ifaces}')
