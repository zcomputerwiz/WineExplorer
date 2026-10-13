#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Exercise assembly preservation and rejection using disposable source trees."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile

spec = importlib.util.spec_from_file_location('assembly', Path(__file__).resolve().parents[1] / 'assemble.py')
assembly = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assembly)


def fixture(root):
    wine = root / 'wine'
    wine.mkdir()
    (wine / 'dlls/stub').mkdir(parents=True)
    (wine / 'include').mkdir()
    (wine / 'programs').mkdir()
    (root / 'src/dlls/stub').mkdir(parents=True)
    (root / 'src/include').mkdir()
    (root / 'src/dlls/stub/Makefile.in').write_text('project implementation\n')
    (root / 'src/stock-replacements.txt').write_text('dlls/stub\n')
    (wine / 'dlls/stub/Makefile.in').write_text('stock stub\n')
    subprocess.run(['git', '-C', str(wine), 'init', '--quiet'], check=True)
    subprocess.run(['git', '-C', str(wine), 'add', '.'], check=True)
    subprocess.run(['git', '-C', str(wine), '-c', 'user.name=Assembly test',
                    '-c', 'user.email=test@localhost', 'commit', '--quiet', '-m', 'Fixture'], check=True)
    return wine


with tempfile.TemporaryDirectory(prefix='wex-assembly-') as temporary:
    for case in ('clean', 'edited', 'extra', 'undeclared'):
        root = Path(temporary) / case
        root.mkdir()
        wine = fixture(root)
        if case == 'edited':
            (wine / 'dlls/stub/Makefile.in').write_text('user edit\n')
        elif case == 'extra':
            (wine / 'dlls/stub/user-file').write_text('user file\n')
        elif case == 'undeclared':
            (root / 'src/stock-replacements.txt').write_text('')
        if case == 'clean':
            assembly.assemble(root, wine)
            assembly.assemble(root, wine)
            assert (wine / 'dlls/stub').is_symlink()
            saved = list((root / 'build/assembly-stock').glob('*/dlls/stub/Makefile.in'))
            assert len(saved) == 1 and saved[0].read_text() == 'stock stub\n'
            print('PASS pristine replacement preserved, linked, and idempotent')
        else:
            try:
                assembly.assemble(root, wine)
            except (RuntimeError, subprocess.CalledProcessError):
                assert not (wine / 'dlls/stub').is_symlink()
                if case == 'edited':
                    assert (wine / 'dlls/stub/Makefile.in').read_text() == 'user edit\n'
                elif case == 'extra':
                    assert (wine / 'dlls/stub/user-file').read_text() == 'user file\n'
                assert not (root / 'build/assembly-stock').exists()
                print('PASS rejected and preserved:', case)
            else:
                raise AssertionError('Unsafe replacement accepted: ' + case)
