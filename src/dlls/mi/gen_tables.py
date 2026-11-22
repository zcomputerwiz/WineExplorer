#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#!/usr/bin/env python3
"""Generate mi_tables.c: one tracing stub per entry of the MI client function tables.
Entries listed in implemented.txt (as STRUCT_Name) use impl_STRUCT_Name instead."""
import os, re, sys
hdr = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'MI.h'), encoding='utf-8', errors='replace').read()
impl = set(l.strip() for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'implemented.txt')) if l.strip() and not l.startswith('#'))
structs = ['MI_ApplicationFT', 'MI_SessionFT', 'MI_OperationFT', 'MI_HostedProviderFT', 'MI_SerializerFT',
           'MI_DeserializerFT', 'MI_SubscriptionDeliveryOptionsFT', 'MI_DestinationOptionsFT',
           'MI_OperationOptionsFT', 'MI_UtilitiesFT', 'MI_InstanceFT', 'MI_ClassFT', 'MI_ParameterSetFT',
           'MI_PropertySetFT', 'MI_QualifierSetFT', 'MI_ParameterSetFT']
BANNER = '/*\n * mi.dll: the function tables, made by gen_tables.py from MI.h (OMI, MIT licence); do not edit\n *\n * Copyright 2026 WineExplorer contributors\n *\n * This program is free software; you can redistribute it and/or modify\n * it under the terms of the GNU General Public License as published by\n * the Free Software Foundation; either version 2 of the License, or\n * (at your option) any later version.\n *\n * This program is distributed in the hope that it will be useful,\n * but WITHOUT ANY WARRANTY; without even the implied warranty of\n * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n * GNU General Public License for more details.\n *\n * You should have received a copy of the GNU General Public License\n * along with this program; if not, see <https://www.gnu.org/licenses/>.\n */\n\n'
seen = set()
out = [BANNER, '#include "mi_priv.h"', '']
decl = []
tables = []


def body(name):
    m = re.search(r'^(?:typedef\s+)?struct _' + name + r'\s*\{', hdr, re.M)
    if not m:
        return None
    i = m.end(); depth = 1
    while depth:
        c = hdr[i]
        depth += (c == '{') - (c == '}')
        i += 1
    return hdr[m.end():i - 1]


def balanced(s, i):
    depth = 1; j = i
    while depth:
        c = s[j]
        depth += (c == '(') - (c == ')')
        j += 1
    return s[i:j - 1], j


for st in structs:
    if st in seen:
        continue
    seen.add(st)
    b = body(st)
    if b is None:
        print('no struct', st, file=sys.stderr); continue
    # remove comments
    b = re.sub(r'/\*.*?\*/', '', b, flags=re.S)
    b = re.sub(r'//[^\n]*', '', b)
    entries = []
    pos = 0
    while True:
        m = re.compile(r'([A-Za-z_][\w \t\*]*?)\s*\(\s*MI_CALL\s*\*\s*(\w+)\s*\)\s*\(').search(b, pos)
        if not m:
            break
        params, end = balanced(b, m.end())
        ret = m.group(1).strip()
        entries.append((ret, m.group(2), ' '.join(params.split())))
        pos = end
    # non-function members?  report count of fields for sanity
    fields = [x for x in b.split(';') if x.strip()]
    if len(fields) != len(entries):
        print('WARNING %s: %d fields, %d fn entries' % (st, len(fields), len(entries)), file=sys.stderr)
    members = []
    for ret, name, params in entries:
        key = st + '_' + name
        if key in impl:
            decl.append('%s MI_CALL impl_%s(%s);' % (ret, key, params))
            members.append('impl_' + key)
        else:
            dflt = {'MI_Result': 'MI_RESULT_NOT_SUPPORTED', 'void': ''}.get(ret, '0')
            if ret == 'MI_Result' and st.endswith('OptionsFT'):
                if name.startswith(('Set', 'Delete', 'Close', 'Add', 'Clone')):
                    dflt = 'MI_RESULT_OK'
                elif name.startswith('Get'):
                    dflt = 'MI_RESULT_NOT_FOUND'
            decl.append('static %s MI_CALL stub_%s(%s) { mi_trace("%s"); %s }' %
                        (ret, key, params, key, ('return %s;' % dflt) if ret != 'void' else ''))
            members.append('stub_' + key)
    tables.append('const %s mi_tbl_%s = {\n    %s\n};' % (st, st, ',\n    '.join(members)))
out += decl + [''] + tables
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mi_tables.c'), 'w', newline='\n').write('\n'.join(out) + '\n')
print('ok', len(decl), 'entries')
