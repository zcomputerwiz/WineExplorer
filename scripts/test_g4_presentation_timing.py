#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Wine-free executable timeline, decoded-pixel evidence and CLI controls."""
import copy
import importlib.util
import json
import io
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from PIL import Image
from graphics_frame_pixels import recovery_page_pixels, recovery_pixels
import test_graphics_frame_pixels as frame_fixtures

spec = importlib.util.spec_from_file_location('consumer_timing', Path(__file__).with_name('check-g4-webview2.py'))
consumer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(consumer)
analyzer_spec = importlib.util.spec_from_file_location('timing_analyzer', Path(__file__).with_name('analyze-webview2-run.py'))
analyzer = importlib.util.module_from_spec(analyzer_spec)
analyzer_spec.loader.exec_module(analyzer)


class Timeline(unittest.TestCase):
    def setUp(self):
        self.clock = 100.
        self.calls, self.acks = [], []
        self.run_id = 'a' * 32
        self.request = {'runId': self.run_id, 'remainingMs': 30000,
                        'windowBounds': dict(x=20, y=20, width=320, height=300),
                        'clientBounds': dict(x=20, y=50, width=300, height=250),
                        'viewport': dict(width=300, height=250),
                        'marker': dict(x=50, y=100, width=40, height=40),
                        'background': [232, 245, 233]}
        self.front_duration = .01
        self.front_passed = [False] * 4 + [True]
        self.front_error = None
        self.page_error = None
        self.page_passed = True
        self.collector = consumer.PresentationTimingCollector(
            self.run_id, self.front, self.page, self.ack,
            now=lambda: self.clock, sleep=self.sleep)

    def sleep(self, seconds):
        self.clock += seconds

    def front(self, stage, request, timeout, record):
        self.calls.append((stage, self.clock))
        self.clock += self.front_duration
        if self.front_error:
            raise self.front_error
        front, _, state, marker, background = frame_fixtures.FramePixels().recovery_images(1.25)
        index = [name for name, _ in consumer.TIMING_FRONTS].index(stage) if stage != 'front_after_page_250' else 4
        if not self.front_passed[index]:
            # A valid stale image with intact rims, but the old marker position.
            front.paste(Image.new('RGB', (50, 50), background), (87, 187))
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'front.png'
            front.save(path)
            with Image.open(path) as decoded:
                pixels = recovery_pixels(decoded, state, 1.25, marker, background)
        return {'pixels': pixels, 'observed_scale': 1.25,
                'compositor_identity': {'pid': 7, 'start_ticks': 42}}

    def page(self, page, request, timeout):
        self.calls.append(('page', self.clock))
        if self.page_error:
            raise self.page_error
        _, image, _, marker, background = frame_fixtures.FramePixels().recovery_images(1.25)
        if not self.page_passed:
            image.paste(Image.new('RGB', image.size, background))
        return {'pixels': recovery_page_pixels(image, request['viewport'], 1.25, marker, background)}

    def ack(self, stage, details):
        self.acks.append({'runId': self.run_id, 'stage': stage, 'captured': True, **details})

    def event(self, kind, value):
        return '1 ms presentation timing %s %s\n' % (kind, json.dumps(value))

    def collect(self):
        text = self.event('requested', self.request)
        self.collector.poll(text)
        self.assertEqual(len(self.calls), 4)
        self.assertEqual([x['stage'] for x in self.acks], ['fronts_complete'])
        text += self.event('acknowledged', self.acks[0])
        text += self.event('page capture', self.request)
        self.collector.poll(text)
        text += self.event('acknowledged', self.acks[1])
        text += self.event('result', {'runId': self.run_id, 'collectionValid': True})
        return self.collector.result(text), text

    def test_fixed_front_order_one_page_then_post_page_with_stale_pngs_retained(self):
        result, text = self.collect()
        self.assertTrue(result['collection_valid'])
        self.assertEqual(result['acceptance'], 'NOT_PROVEN')
        self.assertEqual(result['classification'], 'stale until after page readback')
        self.assertEqual([c[0] for c in self.calls],
                         ['front_0', 'front_250', 'front_1000', 'front_3000', 'page', 'front_after_page_250'])
        for (stage, moment), expected in zip(self.calls[:4], (100, 100.25, 101, 103)):
            self.assertAlmostEqual(moment, expected)
        self.assertAlmostEqual(self.calls[-1][1] - self.calls[-2][1], .25)
        self.assertEqual([s['pixels']['passed'] for s in result['front_samples']], [False] * 4 + [True])
        self.assertTrue(result['page']['pixels']['passed'])
        self.collector.poll(text)  # rereading the append-only log must not recapture
        self.assertEqual(len(self.calls), 6)

    def evaluate_health(self, text, chromium=''):
        # Exercise the existing analyzer on real logs. Frames intentionally stall:
        # process/log health, not animation progression, gates this experiment.
        text += '0 ms navigation completed hr=0x00000000 success=1 web_error=0\n'
        text += ''.join('%d ms sample hr=0x00000000 {"frames":14}\n' % (1000 * i)
                        for i in range(1, 7))
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            (root / 'host.log').write_text(text)
            (root / 'chromium.log').write_text(chromium)
            analysis = analyzer.analyze(root)
        return consumer.presentation_timing_result(self.collector, text, analysis), analysis

    def test_complete_stale_collection_requires_health_but_not_animation(self):
        original, text = self.collect()
        result, analysis = self.evaluate_health(text)
        self.assertEqual(analysis['state'], 'animation_stalled')
        self.assertFalse(analysis['diagnostic_passed'])
        self.assertTrue(result['collection_valid'])
        self.assertTrue(result['experiment_health']['passed'])
        self.assertTrue(result['experiment_valid'])
        self.assertEqual(result['classification'], 'stale until after page readback')
        self.assertEqual(result['front_samples'], original['front_samples'])

    def test_complete_collection_with_unexpected_gpu_or_renderer_exit_is_inconclusive(self):
        original, text = self.collect()
        for event, details in (('child process gone', {'type': 'GPU', 'reason': 'crashed', 'exitCode': 1}),
                               ('renderer gone', {'reason': 'killed', 'exitCode': 9})):
            with self.subTest(event=event):
                result, _ = self.evaluate_health(text + '9 ms %s %s\n' % (event, json.dumps(details)))
                self.assertTrue(result['collection_valid'])
                self.assertFalse(result['experiment_health']['passed'])
                self.assertFalse(result['experiment_valid'])
                self.assertEqual(result['classification'], 'invalid/inconclusive')
                self.assertEqual(result['front_samples'], original['front_samples'])
                self.assertEqual(result['page'], original['page'])

    def test_all_existing_classified_chromium_errors_invalidate_complete_experiment(self):
        original, text = self.collect()
        for chromium in ('Requested GL implementation test not found', 'eglInitialize D3D11Warp failed',
                         'D3D11CreateDevice failed', 'DCompositionCreateDevice failed',
                         'GPU process exited unexpectedly', 'test:FATAL: crash',
                         'Unable to initialize SkSurface', 'Failed to create shared context for virtualization'):
            with self.subTest(chromium=chromium):
                result, analysis = self.evaluate_health(text, chromium)
                self.assertTrue(any(analysis['errors'].values()))
                self.assertTrue(result['collection_valid'])
                self.assertFalse(result['experiment_valid'])
                self.assertEqual(result['classification'], 'invalid/inconclusive')
                self.assertEqual(result['front_samples'], original['front_samples'])

    def test_malformed_process_events_or_analyzer_evidence_fail_closed(self):
        _, text = self.collect()
        for event in ('child process gone {bad', 'child process gone []',
                      'child process gone {"reason":"crashed","exitCode":1}',
                      'child process gone {"type":7,"reason":"crashed","exitCode":1}',
                      'renderer gone {"reason":"crashed","exitCode":true}',
                      'renderer gone {"reason":7,"exitCode":1}', 'renderer gone'):
            with self.subTest(event=event):
                result, _ = self.evaluate_health(text + '9 ms ' + event + '\n')
                self.assertTrue(result['collection_valid'])
                self.assertFalse(result['experiment_valid'])
                self.assertTrue(result['experiment_health']['malformed_process_events'])
                self.assertEqual(result['classification'], 'invalid/inconclusive')
        _, analysis = self.evaluate_health(text)
        for invalid in (None, {}, {**analysis, 'errors': {}}, {**analysis, 'chromium_log_present': False},
                        {**analysis, 'errors': {**analysis['errors'], 'fatal_log': False}}):
            with self.subTest(invalid=invalid):
                result = consumer.presentation_timing_result(self.collector, text, invalid)
                self.assertTrue(result['collection_valid'])
                self.assertFalse(result['experiment_valid'])

    def test_lateness_is_preserved_without_retry_or_pixel_abort(self):
        self.front_duration = .4
        result, _ = self.collect()
        self.assertTrue(result['collection_valid'])
        self.assertAlmostEqual(result['front_samples'][1]['lateness_ms'], 150)
        self.assertAlmostEqual(result['front_samples'][0]['duration_ms'], 400)
        self.assertEqual(result['classification'], 'invalid/inconclusive')
        self.assertEqual(len(self.calls), 6)

    def test_valid_but_stale_page_does_not_abort_post_page_collection(self):
        self.page_passed = False
        result, _ = self.collect()
        self.assertTrue(result['collection_valid'])
        self.assertFalse(result['page']['pixels']['passed'])
        self.assertEqual(len(result['front_samples']), 5)
        self.assertEqual(result['classification'], 'invalid/inconclusive')

    def test_capture_lateness_includes_geometry_observation_overhead(self):
        def delayed_png(*args):
            record = self.front(*args)
            record['capture_started_monotonic'] = self.clock - .005
            return record
        self.collector.front = delayed_png
        result, _ = self.collect()
        self.assertAlmostEqual(result['front_samples'][0]['capture_lateness_ms'], 5)

    def test_partial_capture_evidence_survives_later_observation_failure(self):
        def failed_after_decode(stage, request, timeout, record):
            record.update(path='retained.png', pixels={'passed': False})
            raise RuntimeError('compositor identity changed after capture')
        self.collector.front = failed_after_decode
        with self.assertRaises(RuntimeError):
            self.collector.poll(self.event('requested', self.request))
        result = self.collector.result('')
        self.assertFalse(result['collection_valid'])
        self.assertEqual(result['front_samples'][0]['path'], 'retained.png')
        self.assertFalse(result['front_samples'][0]['pixels']['passed'])
        self.assertEqual(self.acks, [])

    def test_descriptive_classifications(self):
        for pixels, classification in (([True]*5, 'correct before page readback'),
                ([False, True, True, True, True], 'stale then correct before page readback'),
                ([False]*5, 'still stale'), ([True, False, True, True, True], 'invalid/inconclusive')):
            with self.subTest(pixels=pixels):
                self.setUp()
                self.front_passed = pixels
                self.assertEqual(self.collect()[0]['classification'], classification)

    def test_no_page_before_front_ack_or_without_request(self):
        for text in (self.event('page capture', self.request),
                     self.event('requested', self.request) + self.event('page capture', self.request)):
            self.setUp()
            with self.assertRaises(ValueError):
                self.collector.poll(text)
            self.assertEqual(self.calls, [])

    def test_invalid_page_capture_never_acknowledges_post_page_completion(self):
        for error in (FileNotFoundError('missing PNG'), ValueError('corrupt PNG/dimensions')):
            with self.subTest(error=error):
                self.setUp()
                text = self.event('requested', self.request)
                self.collector.poll(text)
                self.page_error = error
                with self.assertRaises(type(error)):
                    self.collector.poll(text + self.event('page capture', self.request))
                self.assertEqual([a['stage'] for a in self.acks], ['fronts_complete'])
                self.assertEqual(len(self.collector.samples), 4)
                self.assertFalse(self.collector.result(text)['collection_valid'])

    def test_stale_nonce_duplicate_malformed_events_and_bad_scene(self):
        for text in (self.event('requested', {**self.request, 'runId': 'stale'}),
                     self.event('requested', self.request) * 2,
                     '1 ms presentation timing requested {bad\n',
                     self.event('requested', []),
                     self.event('requested', {**self.request, 'background': [0, 0, 0]})):
            self.setUp()
            with self.assertRaises((ValueError, TypeError)):
                self.collector.poll(text)
            self.assertFalse(self.collector.result(text)['collection_valid'])

    def test_timeout_missing_ack_observer_exception_and_geometry_change_fail_closed(self):
        result, text = self.collect()
        self.assertFalse(self.collector.result(text, 'observer failed')['collection_valid'])
        self.assertFalse(self.collector.result(text.replace('"captured": true', '"captured": false'))['collection_valid'])
        self.assertFalse(self.collector.result(text.replace('1 ms presentation timing acknowledged',
                                                            '1 ms missing acknowledgement'))['collection_valid'])
        self.assertFalse(self.collector.result(text.replace('"pixelPassed": true', '"pixelPassed": false'))['collection_valid'])
        for error in (FileNotFoundError('PNG missing'), ValueError('PNG/geometry invalid'), RuntimeError('observer')):
            self.setUp()
            self.front_error = error
            with self.assertRaises(type(error)):
                self.collector.poll(self.event('requested', self.request))
            self.assertFalse(self.collector.result('')['collection_valid'])
            self.assertIn('ended_monotonic', self.collector.samples[0])
            self.assertEqual(self.acks, [])
        self.setUp()
        self.front_duration = 6
        with self.assertRaises(TimeoutError):
            self.collector.poll(self.event('requested', self.request))
        self.setUp()
        text = self.event('requested', self.request)
        self.collector.poll(text)
        self.clock += 31
        with self.assertRaises(TimeoutError):
            self.collector.poll(text)
        self.setUp()
        self.collector.poll(text)
        changed = copy.deepcopy(self.request)
        changed['clientBounds']['y'] += 1
        with self.assertRaises(ValueError):
            self.collector.poll(text + self.event('page capture', changed))


class TimingCLI(unittest.TestCase):
    def test_atomic_ack_refuses_existing_nonce_stage_and_keeps_prior_evidence(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            consumer.write_timing_ack(root, 'a'*32, 'fronts_complete', {'samples': ['original']})
            with self.assertRaises(FileExistsError):
                consumer.write_timing_ack(root, 'a'*32, 'fronts_complete', {'samples': ['replacement']})
            ack = next(root.glob('*.ack'))
            self.assertEqual(json.loads(ack.read_text())['samples'], ['original'])
            self.assertEqual(list(root.glob('*.tmp')), [])

    def test_missing_timing_helper_fails_preflight_before_any_launch(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            for name in ('wine', 'server/wineserver', 'electron.exe', 'fixture/main.js',
                         'fixture/package.json', 'fixture/recovery-frame-wait.js', 'labwc'):
                path = root / name
                path.parent.mkdir(exist_ok=True)
                path.write_text('test')
            args = ['runner', '--wine-build', str(root), '--runtime', str(root),
                    '--host', str(root/'electron.exe'), '--electron-app', str(root/'fixture'),
                    '--output-dir', str(root/'never-created'), '--presentation-timing', '--seconds', '60',
                    '--gl-driver', 'virgl', '--wexcomp-bin', str(root/'labwc'),
                    '--wexcomp-config', str(root), '--compositor-renderer', 'gles2']
            errors = io.StringIO()
            with mock.patch('sys.argv', args), mock.patch('sys.stderr', errors), \
                    mock.patch.object(consumer.shutil, 'which', return_value='/usr/bin/grim'), \
                    mock.patch.object(consumer.subprocess, 'Popen') as launch:
                with self.assertRaises(SystemExit) as raised:
                    consumer.main()
                self.assertEqual(raised.exception.code, 2)
                self.assertIn('presentation-timing.js', errors.getvalue())
                launch.assert_not_called()
            self.assertFalse((root/'never-created').exists())

    def test_generic_front_schedule_disabled_only_in_explicit_mode(self):
        self.assertEqual(consumer.screenshot_schedule(True), [])
        self.assertEqual(consumer.screenshot_schedule(True, True, True), [])
        self.assertEqual(consumer.screenshot_schedule(), [8, 18])
        self.assertIn(32, consumer.screenshot_schedule(gpu_recovery=True))

    def test_conflicting_modes_fail_before_launch(self):
        base = ['runner', '--wine-build', '/absent', '--runtime', '/absent', '--host', '/absent',
                '--output-dir', '/absent', '--electron-app', '/absent', '--presentation-timing',
                '--seconds', '60']
        for conflict in (['--gpu-recovery'], ['--lifecycle'], ['--freeze-scene'],
                         ['--restart-from', '/absent'], ['--profile-proof'], ['--lifecycle-visuals']):
            with self.subTest(conflict=conflict), mock.patch('sys.argv', base + conflict), \
                    mock.patch.object(consumer.subprocess, 'Popen') as launch:
                with self.assertRaises(SystemExit) as error:
                    consumer.main()
                self.assertEqual(error.exception.code, 2)
                launch.assert_not_called()

    def test_manifest_includes_optional_js_and_transitive_native_dependency(self):
        app = Path(__file__).with_name('g4-electron-probe')
        paths = consumer.electron_payloads(app / 'electron.exe', app / 'electron.exe', app,
                                          presentation_timing=True)
        self.assertIn(app / 'presentation-timing.js', paths)
        self.assertIn(app / 'recovery-frame-wait.js', paths)
        self.assertIn(Path(consumer.__file__).with_name('test_g4_native_window_state.cpp'), paths)
        ordinary = consumer.electron_payloads(app / 'electron.exe', app / 'electron.exe', app)
        self.assertNotIn(app / 'presentation-timing.js', ordinary)


if __name__ == '__main__':
    unittest.main()
