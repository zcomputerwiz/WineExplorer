#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Wine-free fail-closed restart/profile parser tests; uses only owned temp state."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('consumer', Path(__file__).with_name('check-g4-webview2.py'))
consumer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(consumer)


class DependencyPreflight(unittest.TestCase):
    def test_missing_resize_analyzer_fails_before_launch(self):
        arguments = ['check-g4-webview2.py', '--wine-build', '/not-started/build',
                     '--runtime', '/not-started/runtime', '--host', '/not-started/host.exe',
                     '--output-dir', '/not-started/output', '--resize-border']
        with mock.patch('sys.argv', arguments), \
                mock.patch.dict('sys.modules', {'graphics_frame_pixels': None}), \
                mock.patch.object(consumer.subprocess, 'Popen') as launch:
            with self.assertRaises(ModuleNotFoundError):
                consumer.main()
            launch.assert_not_called()


class NativeScale(unittest.TestCase):
    def test_exact_published_scale(self):
        text = 'NATIVE_WINDOW fixed=81920 visible=1 x=25 y=25 width=740 height=520\n'
        self.assertEqual(consumer.native_scale_state(text, 1.25)['width'], 740)
        for invalid in ('', text * 2, text.replace('81920', '65536'), text.replace('visible=1', 'visible=0'),
                        text.replace('width=740', 'width=0')):
            with self.subTest(invalid=invalid), self.assertRaises(RuntimeError):
                consumer.native_scale_state(invalid, 1.25)


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
                       {'gl_driver': 'd3d12'},
                       {'desktop': {'mode': 'composited'}},
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


class GPURecovery(unittest.TestCase):
    def result(self):
        pixel = dict(available=True, lost=False, defaultError=0, textureError=0,
                     framebufferStatus=36053, defaultPixel=[255, 0, 0, 255],
                     texturePixel=[0, 255, 0, 255], renderer='ANGLE D3D11 lab adapter')
        before = dict(contexts={name: copy.deepcopy(pixel) for name in ('webgl', 'webgl2')},
                      gpu={'pid': 100, 'creationTime': 1}, rendererPid=200,
                      features={'gpu_compositing': 'enabled'}, frames=10)
        after = copy.deepcopy(before)
        after.update(gpu={'pid': 101, 'creationTime': 2}, laterFrames=20,
                     oldLost={name: {'events': 1, 'lost': True} for name in ('webgl', 'webgl2')})
        return dict(passed=True, before=before, after=after,
                    fault={'pid': 100, 'creationTime': 1, 'requested': True}, rendererExits=[],
                    exits=[{'type': 'GPU', 'reason': 'killed', 'exitCode': 1}])

    def parse(self, result):
        return consumer.recovery_result(self.log(result))

    def log(self, result):
        return ('10 ms gpu recovery fault ' + json.dumps(result['fault']) + '\n'
                + ''.join('11 ms child process gone ' + json.dumps(item) + '\n' for item in result['exits'])
                + '12 ms gpu recovery result ' + json.dumps(result) + '\n')

    def test_complete_recovery(self):
        self.assertTrue(self.parse(self.result())['passed'])
        log = self.log(self.result())
        for suffix in ('13 ms renderer gone {}\n',
                       '13 ms child process gone {"type":"GPU","reason":"crashed","exitCode":1}\n'):
            self.assertFalse(consumer.recovery_result(log + suffix)['passed'])

    def test_reject_missing_fault_wrong_pixels_fallback_or_unproven_recovery(self):
        changes = [lambda r: r.update(passed=False), lambda r: r.update(fault=None),
                   lambda r: r['fault'].update(pid=999), lambda r: r['after']['gpu'].update(pid=100),
                   lambda r: r['after'].update(rendererPid=201), lambda r: r.update(rendererExits=[{}]),
                   lambda r: r['after'].update(laterFrames=10), lambda r: r.update(exits=[]),
                   lambda r: r['exits'].append(copy.deepcopy(r['exits'][0])),
                   lambda r: r['exits'][0].update(reason='clean-exit'),
                   lambda r: r['exits'][0].update(exitCode=0),
                   lambda r: r['after']['oldLost']['webgl'].update(events=0),
                   lambda r: r['after']['contexts']['webgl2'].update(defaultPixel=[0, 0, 0, 0]),
                   lambda r: r['after']['contexts']['webgl'].update(renderer='SwiftShader'),
                   lambda r: r['after']['features'].update(gpu_compositing='disabled_software')]
        for change in changes:
            result = self.result()
            change(result)
            with self.subTest(result=result):
                self.assertFalse(self.parse(result)['passed'])
        for log in ('', '12 ms gpu recovery result null\n', '12 ms gpu recovery result []\n'):
            self.assertFalse(consumer.recovery_result(log)['passed'])

    def test_only_exact_single_injected_crash_is_allowlisted(self):
        animation = dict(state='animation_progressing', navigation_succeeded=True,
                         chromium_log_present=True, errors={name: 0 for name in
                         ('gl_allowlist', 'warp_initialization', 'd3d11_output',
                          'directcomposition_output', 'gpu_process_crash', 'fatal_log',
                          'skia_surface', 'shared_context')})
        animation['errors']['gpu_process_crash'] = 1
        self.assertTrue(consumer.recovery_acceptance(animation, {'passed': True}))
        for count in (0, 2):
            value = copy.deepcopy(animation)
            value['errors']['gpu_process_crash'] = count
            self.assertFalse(consumer.recovery_acceptance(value, {'passed': True}))
        value = copy.deepcopy(animation)
        value['errors']['fatal_log'] = 1
        self.assertFalse(consumer.recovery_acceptance(value, {'passed': True}))
        value = copy.deepcopy(animation)
        del value['errors']['fatal_log']
        value['errors']['unknown'] = 0
        self.assertFalse(consumer.recovery_acceptance(value, {'passed': True}))
        self.assertFalse(consumer.recovery_acceptance(animation, {'passed': False}))


class VisualCapture(unittest.TestCase):
    def test_all_stages_must_be_captured_and_acknowledged_in_order(self):
        text = ''.join('1 ms visual checkpoint ' + json.dumps({'stage': s}, separators=(',', ':')) + '\n'
                       + '2 ms visual checkpoint captured ' + json.dumps({'stage': s}, separators=(',', ':')) + '\n'
                       for s in consumer.VISUAL_STAGES)
        captures = dict.fromkeys(consumer.VISUAL_STAGES, {})
        self.assertTrue(consumer.visual_capture_complete(text, captures))
        self.assertFalse(consumer.visual_capture_complete(text, captures, 'capture failed'))
        self.assertFalse(consumer.visual_capture_complete(text + text, captures))
        self.assertFalse(consumer.visual_capture_complete(text, {}))
        self.assertFalse(consumer.visual_capture_complete(text.replace('checkpoint captured', 'wrong event'), captures))
        self.assertFalse(consumer.visual_capture_complete(text.replace('"ready"', '"unknown"'), captures))


if __name__ == '__main__':
    unittest.main()
