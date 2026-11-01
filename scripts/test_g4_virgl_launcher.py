#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Identity validation tests for the owned VirGL launcher stop procedure."""
import ast
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('stop_launcher', Path(__file__).with_name('stop_g4_virgl_launcher.py'))
stop_launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stop_launcher)


def write_proc(proc_root, pid, start_ticks, cmdline):
    entry = proc_root / str(pid)
    entry.mkdir()
    fields = ['S'] + ['0'] * 18 + [str(start_ticks)]
    (entry / 'stat').write_text(f'{pid} (python3 custom name) ' + ' '.join(fields))
    (entry / 'cmdline').write_bytes(b'\0'.join(arg.encode() for arg in cmdline) + b'\0')


class LauncherIdentity(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='wex-launcher-identity-')
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.run_dir = root / 'run-a'
        self.run_dir.mkdir()
        self.launcher = root / 'launch.py'
        self.launcher.write_text('# fake owned launcher\n')
        self.identity_path = self.run_dir / 'launcher-identity.json'
        self.proc_root = root / 'proc'
        self.proc_root.mkdir()
        self.command = ['python3', '-B', str(self.launcher)]
        self.identity = {'pid': 71, 'start_ticks': 991, 'cmdline': self.command,
                         'run_name': 'run-a', 'run_dir': str(self.run_dir.resolve()),
                         'launcher_path': str(self.launcher.resolve()),
                         'identity_record': str(self.identity_path.resolve())}
        self.identity_path.write_text(json.dumps(self.identity))
        write_proc(self.proc_root, 71, 991, self.command)

    def test_exact_pid_ticks_cmdline_and_run_path_are_accepted(self):
        current = stop_launcher.validate_identity(self.identity, self.identity_path, self.proc_root)
        self.assertEqual(current['pid'], 71)
        self.assertEqual(current['start_ticks'], 991)

    def test_pid_reuse_or_cmdline_change_is_refused(self):
        for ticks, command in ((992, self.command), (991, ['python3', '-B', '/other/launch.py'])):
            with self.subTest(ticks=ticks, command=command):
                entry = self.proc_root / '71'
                (entry / 'stat').unlink()
                (entry / 'cmdline').unlink()
                entry.rmdir()
                write_proc(self.proc_root, 71, ticks, command)
                with self.assertRaises(RuntimeError):
                    stop_launcher.validate_identity(self.identity, self.identity_path, self.proc_root)


class StagedLauncherIdentity(unittest.TestCase):
    def test_private_launcher_records_own_file_not_repository_alias(self):
        launcher = Path(__file__).resolve().parents[1]/'build/g4-virgl-20261022/launch.py'
        if not launcher.is_file():
            self.skipTest('Private launcher unavailable')
        # Execute ONLY the actual identity update AST, never the boot script.
        tree = ast.parse(launcher.read_text())
        updates = [node for node in tree.body if isinstance(node, ast.Expr)
                   and isinstance(node.value, ast.Call)
                   and isinstance(node.value.func, ast.Attribute)
                   and isinstance(node.value.func.value, ast.Name)
                   and node.value.func.value.id == 'launcher_identity'
                   and node.value.func.attr == 'update']
        self.assertEqual(len(updates), 1)
        identity = {}
        namespace = dict(Path=Path, __file__=str(launcher), launcher_identity=identity,
                         repo=Path('/wrong/old/checkout'), output=Path('/owned/run'), run_name='owned')
        exec(compile(ast.Module(body=updates, type_ignores=[]), str(launcher), 'exec'), namespace)
        self.assertEqual(identity['launcher_path'], str(launcher.resolve()))


if __name__ == '__main__':
    unittest.main()
