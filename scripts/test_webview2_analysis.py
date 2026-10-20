#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A renderer restart/fallback may produce animation; it must not hide a crash."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('analysis', Path(__file__).with_name('analyze-webview2-run.py'))
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)
runner_spec = importlib.util.spec_from_file_location('runner', Path(__file__).with_name('check-g4-webview2.py'))
runner = importlib.util.module_from_spec(runner_spec)
runner_spec.loader.exec_module(runner)


class AnalysisTests(unittest.TestCase):
    def assess(self, chromium='', progressing=True, navigation=True, log_present=True):
        with tempfile.TemporaryDirectory(prefix='wex-webview2-analysis-') as temporary:
            root = Path(temporary)
            host = ['100 ms navigation completed hr=0x00000000 success=1 web_error=0'] if navigation else []
            host += [str(1000 * i) + ' ms sample hr=0x00000000 ' + json.dumps({'frames': 60 * i if progressing else 0})
                     for i in range(1, 9)]
            (root / 'host.log').write_text('\n'.join(host))
            if log_present:
                (root / 'chromium.log').write_text(chromium)
            return analysis.analyze(root)

    def test_clean_progress_is_diagnostic_only(self):
        result = self.assess()
        self.assertEqual(result['state'], 'animation_progressing')
        self.assertTrue(result['diagnostic_passed'])
        self.assertIn('not proof', result['note'])

    def test_gpu_crash_with_later_frames_is_failure(self):
        result = self.assess('[1:2:FATAL:skia.cc:1285] invalid format\nGPU process exited unexpectedly')
        self.assertEqual(result['state'], 'animation_progressing')
        self.assertFalse(result['diagnostic_passed'])
        self.assertEqual(result['errors']['gpu_process_crash'], 1)
        self.assertEqual(result['errors']['fatal_log'], 1)

    def test_missing_chromium_log_is_not_a_clean_run(self):
        self.assertFalse(self.assess(log_present=False)['diagnostic_passed'])

    def test_navigation_and_animation_are_both_required(self):
        self.assertFalse(self.assess(navigation=False)['diagnostic_passed'])
        self.assertFalse(self.assess(progressing=False)['diagnostic_passed'])

    def test_shared_image_and_context_errors_are_failures(self):
        for message in ('Unable to initialize SkSurface', 'Failed to create shared context for virtualization'):
            self.assertFalse(self.assess(message)['diagnostic_passed'])

    def test_lifecycle_requires_each_check(self):
        checks = dict.fromkeys(('input', 'scroll', 'resize', 'minimized', 'restored', 'reload', 'resize_back'), True)
        log = lambda values: '20000 ms lifecycle result ' + json.dumps({'passed': True, 'checks': values})
        self.assertTrue(runner.lifecycle_result(log(checks))['passed'])
        for key in checks:
            bad = checks.copy()
            del bad[key]
            self.assertFalse(runner.lifecycle_result(log(bad))['passed'])
            bad[key] = False
            self.assertFalse(runner.lifecycle_result(log(bad))['passed'])
            bad[key] = 1
            self.assertFalse(runner.lifecycle_result(log(bad))['passed'])
        self.assertFalse(runner.lifecycle_result(log(checks) + '\n' + log(checks))['passed'])
        for bad in ('', '100 ms lifecycle result {}', '100 ms lifecycle result not-json',
                    '100 ms lifecycle result []'):
            self.assertFalse(runner.lifecycle_result(bad)['passed'])


if __name__ == '__main__':
    unittest.main()
