#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Wine-free fail-closed restart/profile parser tests; uses only owned temp state."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('consumer', Path(__file__).with_name('check-g4-webview2.py'))
consumer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(consumer)


class ProfileProof(unittest.TestCase):
    def log(self, **changes):
        value = dict(passed=True, expected='old', previous='old', written='new', flushed=True)
        value.update(changes)
        return '12 ms profile persistence ' + json.dumps(value) + '\n'

    def test_exact_cookie_proof(self):
        self.assertTrue(consumer.profile_result(self.log(), 'old', 'new')['passed'])
        self.assertTrue(consumer.profile_result(self.log(expected=None, previous=None), None, 'new')['passed'])

    def test_wrong_missing_duplicate_or_unflushed_proof(self):
        for log in ('', '12 ms profile persistence garbage\n', '12 ms profile persistence []\n',
                    self.log() * 2, self.log(previous='other'), self.log(written='other'),
                    self.log(expected=None), self.log(flushed=False), self.log(passed=False)):
            with self.subTest(log=log):
                self.assertFalse(consumer.profile_result(log, 'old', 'new')['passed'])


class RestartOwnership(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='wex-consumer-unit-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        (self.root / 'prefix').mkdir()
        (self.root / 'profile').mkdir()
        self.build = Path('/owned/build')
        self.prior = dict(diagnostic_exit=0, cleanup={'remaining_servers': []},
                          consumer='Electron original fixture', wine_build=str(self.build),
                          renderer='gl', sha256={'runtime': 'a'}, stack_sha256={'d3d11': 'b'},
                          state_root=str(self.root), restart_from=None,
                          profile_diagnostic={'passed': True}, profile_token='a' * 32)

    def read(self, value):
        (self.root / 'results.json').write_text(json.dumps(value))
        return consumer.restart_state(self.root, self.build, 'gl', {'runtime': 'a'}, {'d3d11': 'b'})

    def test_only_stopped_successful_matching_fresh_run(self):
        self.assertEqual(self.read(self.prior), (self.root, 'a' * 32))

    def test_failed_running_mismatched_or_chained_runs_rejected(self):
        for change in ({'diagnostic_exit': 4}, {'cleanup_error': 'failed'},
                       {'cleanup': {'remaining_servers': [123]}}, {'cleanup': {}},
                       {'consumer': 'WebView2'}, {'wine_build': '/other'}, {'renderer': 'vulkan'},
                       {'sha256': {}}, {'stack_sha256': {}}, {'vulkan_icd_sha256': 'other'}, {'state_root': '/other'},
                       {'restart_from': 'already-restarted'}, {'profile_diagnostic': {'passed': False}},
                       {'profile_token': ''}):
            value = copy.deepcopy(self.prior)
            value.update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.read(value)

    def test_missing_private_profile_rejected(self):
        (self.root / 'profile').rmdir()
        with self.assertRaises(ValueError):
            self.read(self.prior)

    def test_symlink_private_profile_rejected(self):
        (self.root / 'profile').rmdir()
        (self.root / 'other').mkdir()
        (self.root / 'profile').symlink_to(self.root / 'other', target_is_directory=True)
        with self.assertRaises(ValueError):
            self.read(self.prior)


if __name__ == '__main__':
    unittest.main()
