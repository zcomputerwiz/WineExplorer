# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""lab: register the classes of an updated XamlShim (its classes.txt) in a prefix, as setup does (wex-components.py).
usage: quick-reg.py COMPONENTS_PY CLASSES_TXT PREFIX WINE"""
import importlib.util, os, subprocess, sys, tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location('components', sys.argv[1])
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
classes = Path(sys.argv[2]).read_text(encoding='utf-8').split()
prefix, wine = sys.argv[3], sys.argv[4]
with tempfile.NamedTemporaryFile('w', encoding='utf-8', newline='', suffix='.reg', delete=False) as out:
    out.write(c.uwp_xaml_registry_text('x64', classes))
env = dict(os.environ, WINEPREFIX=prefix, WINEDEBUG='-all')
r = subprocess.run([wine, 'reg.exe', 'import', out.name], env=env, capture_output=True)
subprocess.run([os.path.join(os.path.dirname(wine), 'server/wineserver'), '-w'], env=env)
os.unlink(out.name)
print(f'registered {len(classes)} classes: reg import {r.returncode}')
