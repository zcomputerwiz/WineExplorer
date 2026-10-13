#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Select project and stock working changes without traversing assembled aliases."""
import argparse
from pathlib import Path
import subprocess
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('base')
parser.add_argument('--deleted', action='store_true')
parser.add_argument('--full', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
wine = root / 'wine'


def git(*params, cwd=root):
    return subprocess.check_output(['git', *params], cwd=cwd).decode().splitlines()


kind = 'D' if args.deleted else 'AM'
paths = set(git('diff', '--no-renames', '--name-only', '--diff-filter=' + kind, args.base, '--', 'src'))
if not args.deleted:
    if args.full:
        paths.update(git('ls-files', '--', 'src'))
    paths.update(git('ls-files', '--others', '--exclude-standard', '--', 'src'))
    paths.update(('scripts/assemble.sh', 'scripts/assemble.py', 'scripts/prepare-wine.sh',
                  'scripts/source_layout.py', 'src/stock-replacements.txt'))
if git('rev-parse', '--show-toplevel', cwd=wine) != [str(wine).replace('\\', '/')]:
    # Normalize native Windows path formatting as well as Linux paths.
    if Path(git('rev-parse', '--show-toplevel', cwd=wine)[0]).resolve() != wine.resolve():
        raise RuntimeError('Wine is not an initialized standalone submodule')
owned = {kind + '/' + module.name
         for kind in ('dlls', 'programs')
         for module in (root / 'src' / kind).iterdir() if (module / 'Makefile.in').is_file()}
headers = {'include/' + p.name for p in (root / 'src/include').iterdir() if p.is_file()}


def stock(path):
    return path != 'configure' and path not in headers and not path.startswith('resources/themes/') and \
        not any(path == module or path.startswith(module + '/') for module in owned)


paths.update('wine/' + p for p in git('diff', '--no-renames', '--name-only', '--diff-filter=' + kind,
                                    'HEAD', cwd=wine) if stock(p))
if not args.deleted:
    paths.update('wine/' + p for p in git('ls-files', '--others', '--exclude-standard', cwd=wine) if stock(p))
for path in sorted(paths):
    if args.deleted or (root / path).is_file():
        print(path)
