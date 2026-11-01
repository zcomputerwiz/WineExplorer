#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Unit checks for bounded PE execution and diagnostic JSON handling; no Wine needed."""
import importlib.util
import os
from pathlib import Path
import subprocess
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location('graphics_probes', Path(__file__).with_name('test_graphics_probes.py'))
probes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probes)


class ProbeExecution(unittest.TestCase):
    def launch(self, stdout='{"value": 7}', code=0, timeout=False):
        self.process = mock.Mock()
        self.process.wait.side_effect = [subprocess.TimeoutExpired(['wine'], 90), code] if timeout else [code]

        def start(command, **kwargs):
            self.command, self.options = command, kwargs
            self.assertNotEqual(kwargs['stdout'], subprocess.PIPE)
            self.assertNotEqual(kwargs['stderr'], subprocess.PIPE)
            kwargs['stdout'].write(stdout)
            kwargs['stdout'].flush()
            kwargs['stderr'].write('retained diagnostic')
            kwargs['stderr'].flush()
            return self.process

        return mock.patch.object(probes.subprocess, 'Popen', side_effect=start)

    @mock.patch.object(probes.shutil, 'which', return_value='/owned/wine')
    def test_json_and_explicit_stack_are_preserved(self, unused):
        with self.launch(), mock.patch.dict(os.environ, {'WINEDLLOVERRIDES': 'd3d11,dxgi,wined3d,dcomp=b'}):
            self.assertEqual(probes.run_probe(Path('/owned/probe.exe'), '/owned/wine'), {'value': 7})
        self.assertEqual(self.options['env']['WINEDLLOVERRIDES'], 'd3d11,dxgi,wined3d,dcomp=b')
        self.process.wait.assert_called_once_with(timeout=90)
        self.process.kill.assert_not_called()

    @mock.patch.object(probes.shutil, 'which', return_value='/owned/wine')
    def test_nonzero_exit_is_not_accepted(self, unused):
        with self.launch(code=5), self.assertRaisesRegex(RuntimeError, 'exit code 5'):
            probes.run_probe(Path('/owned/probe.exe'), '/owned/wine')

    @mock.patch.object(probes.shutil, 'which', return_value='/owned/wine')
    def test_timeout_kills_only_the_owned_child_and_retains_output(self, unused):
        with self.launch(code=-9, timeout=True), self.assertRaisesRegex(RuntimeError, 'retained diagnostic'):
            probes.run_probe(Path('/owned/probe.exe'), '/owned/wine')
        self.process.kill.assert_called_once_with()
        self.assertEqual(self.process.wait.call_args_list, [mock.call(timeout=90), mock.call(timeout=5)])

    @mock.patch.object(probes.shutil, 'which', return_value='/owned/wine')
    def test_missing_json_is_not_an_empty_pass(self, unused):
        with self.launch(stdout='no results'), self.assertRaisesRegex(ValueError, 'No valid JSON'):
            probes.run_probe(Path('/owned/probe.exe'), '/owned/wine')


if __name__ == '__main__':
    unittest.main()
