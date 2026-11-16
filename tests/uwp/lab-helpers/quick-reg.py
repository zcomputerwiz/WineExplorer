# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""lab: registrations for a quick run on a copied prefix.
usage: quick-reg.py COMPONENTS_PY CLASSES_TXT PREFIX WINE        the classes of an updated XamlShim (its classes.txt), as
                                                                  setup registers them (wex-components.py)
       quick-reg.py --winrt PREFIX WINE DLL=CLASS,CLASS ...       Wine's own WinRT classes (what wineboot registers from a
                                                                  module's registry resource): ActivatableClassId DllPath,
                                                                  64-bit and 32-bit views"""
import importlib.util, os, subprocess, sys, tempfile
from pathlib import Path


def reg_import(text, prefix, wine):
    with tempfile.NamedTemporaryFile('w', encoding='utf-8', newline='', suffix='.reg', delete=False) as out:
        out.write(text)
    env = dict(os.environ, WINEPREFIX=prefix, WINEDEBUG='-all')
    r = subprocess.run([wine, 'reg.exe', 'import', out.name], env=env, capture_output=True)
    subprocess.run([os.path.join(os.path.dirname(wine), 'server/wineserver'), '-w'], env=env)
    os.unlink(out.name)
    return r.returncode


if sys.argv[1] == '--winrt':
    prefix, wine, specs = sys.argv[2], sys.argv[3], sys.argv[4:]
    lines, count = ['Windows Registry Editor Version 5.00', ''], 0
    for spec in specs:
        dll, classes = spec.split('=', 1)
        path = 'C:\\\\windows\\\\system32\\\\' + dll
        for cls in classes.split(','):
            for view in ('Software\\Microsoft', 'Software\\Wow6432Node\\Microsoft'):
                lines += [f'[HKEY_LOCAL_MACHINE\\{view}\\WindowsRuntime\\ActivatableClassId\\{cls}]', f'"DllPath"="{path}"', '']
            count += 1
    crlf = chr(13) + chr(10)
    rc = reg_import(crlf.join(lines) + crlf, prefix, wine)
    print(f'registered {count} Wine classes: reg import {rc}')
else:
    spec = importlib.util.spec_from_file_location('components', sys.argv[1])
    c = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(c)
    classes = Path(sys.argv[2]).read_text(encoding='utf-8').split()
    rc = reg_import(c.uwp_xaml_registry_text('x64', classes), sys.argv[3], sys.argv[4])
    print(f'registered {len(classes)} classes: reg import {rc}')
