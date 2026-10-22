#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Wine-free fail-closed restart/profile parser tests; uses only owned temp state."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('consumer', Path(__file__).with_name('check-g4-webview2.py'))
consumer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(consumer)
analyzer_spec = importlib.util.spec_from_file_location('analyzer', Path(__file__).with_name('analyze-webview2-run.py'))
analyzer = importlib.util.module_from_spec(analyzer_spec)
analyzer_spec.loader.exec_module(analyzer)


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

    def test_missing_renderer_frame_helper_fails_before_wine_launch(self):
        arguments = ['check-g4-webview2.py', '--wine-build', '/not-started/build',
                     '--runtime', '/not-started/runtime', '--host', '/not-started/host.exe',
                     '--electron-app', '/not-started/fixture', '--gpu-recovery', '--seconds', '30',
                     '--gl-driver', 'virgl', '--wexcomp-bin', '/not-started/labwc',
                     '--wexcomp-config', '/not-started/config', '--compositor-renderer', 'gles2',
                     '--output-dir', '/not-started/output']
        with mock.patch('sys.argv', arguments), \
                mock.patch.object(consumer.shutil, 'which', return_value='/usr/bin/grim'), \
                mock.patch.object(consumer.subprocess, 'Popen') as launch:
            with self.assertRaises(SystemExit) as raised:
                consumer.main()
            self.assertEqual(raised.exception.code, 2)
            launch.assert_not_called()

    def test_recovery_fixture_payload_hash_inventory_includes_wait_helper(self):
        with tempfile.TemporaryDirectory(prefix='wex-frame-wait-fixture-') as temp:
            app = Path(temp)
            for name in ('main.js', 'package.json', 'recovery-frame-wait.js'):
                (app / name).write_text(name, encoding='utf-8')
            browser, host = app / 'electron.exe', app / 'host.exe'
            payloads = consumer.electron_payloads(browser, host, app, gpu_recovery=True)
            self.assertEqual(payloads, (browser, host, app / 'main.js', app / 'package.json',
                                        app / 'recovery-frame-wait.js'))
            helper = payloads[-1]
            self.assertEqual(consumer.digest(helper), hashlib.sha256(
                b'recovery-frame-wait.js').hexdigest())


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
                      gpu={'pid': 100, 'creationTime': 1},
                      browserMain={'pid': 150, 'creationTime': 1000},
                      renderer={'pid': 200, 'creationTime': 1001},
                      features={'gpu_compositing': 'enabled'}, frames=10,
                      scene={'transform': 'translateX(10px)', 'background': 'rgb(232, 245, 233)'})
        after = copy.deepcopy(before)
        after.update(gpu={'pid': 101, 'creationTime': 2}, laterFrames=20,
                     laterTransform='translateX(30px)',
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
                   lambda r: r['after']['renderer'].update(pid=201),
                   lambda r: r['after']['browserMain'].update(pid=151),
                   lambda r: r.update(rendererExits=[{}]),
                   lambda r: r['after'].update(laterFrames=10), lambda r: r.update(exits=[]),
                   lambda r: r['after'].update(laterTransform=r['after']['scene']['transform']),
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

    def test_p010_video_overlay_failure_is_optional_capability_warning(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'host.log').write_text(''.join(
                '%d ms sample hr=0x00000000 {"frames":%d}\n' % (n * 1000, n * 10)
                for n in range(6)) +
                '7000 ms navigation completed hr=0x00000000 success=1 web_error=0\n')
            (root / 'chromium.log').write_text(
                '[900:1022/000000.000:ERROR:ui\\gl\\direct_composition_support.cc:949] '
                'CheckFormatSupport failed: Call failed. (0x80004005)\n')
            result = analyzer.analyze(root)
            self.assertEqual(result['optional_capability_warnings']['p010_video_processor_format_support'], 1)
            self.assertTrue(result['diagnostic_passed'])


class VisualCapture(unittest.TestCase):
    def test_page_event_parser_separates_interleaved_records_and_acknowledgements(self):
        events = ('10 ms gpu recovery page capture {"stage":"pre_fault"}\n'
                  '11 ms gpu recovery page capture acknowledged {"stage":"pre_fault"}\n'
                  '12 ms gpu recovery page capture {"stage":"recovered"}\n')
        records, acknowledgements = consumer.recovery_page_events(events)
        self.assertEqual(records, [{'stage': 'pre_fault'}, {'stage': 'recovered'}])
        self.assertEqual(acknowledgements, [{'stage': 'pre_fault'}])
        with self.assertRaisesRegex(RuntimeError, 'Malformed GPU recovery page-capture record'):
            consumer.recovery_page_events(events + '13 ms gpu recovery page capture {bad json}\n')
        with self.assertRaisesRegex(RuntimeError, 'Malformed GPU recovery page-capture acknowledgement'):
            consumer.recovery_page_events(events +
                                          '13 ms gpu recovery page capture acknowledged {bad json}\n')

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

    def test_observer_exception_is_preserved_for_fail_closed_acceptance(self):
        result = {}
        def broken_observer():
            raise RuntimeError('capture backend disappeared')
        consumer.observe_fail_closed(broken_observer, result)
        self.assertEqual(result.get('observer_error'), 'RuntimeError: capture backend disappeared')

    def test_gpu_recovery_captures_require_fresh_ordered_ack_and_stable_compositor(self):
        run_id = 'a' * 32
        identity = {'pid': 41, 'start_ticks': 900}
        stages = consumer.RECOVERY_VISUAL_STAGES
        log = ''.join(
            '10 ms gpu recovery front capture requested ' + json.dumps({'stage': stage, 'runId': run_id,
                'windowBounds': {'x': 0, 'y': 0, 'width': 800, 'height': 600},
                'clientBounds': {'x': 0, 'y': 0, 'width': 800, 'height': 600},
                'viewport': {'width': 800, 'height': 600},
                'marker': {'x': 50, 'y': 100, 'width': 40, 'height': 40},
                'background': [232, 245, 233]}) + '\n'
            + '11 ms gpu recovery front capture acknowledged ' + json.dumps({'stage': stage, 'runId': run_id,
                'captured': True, 'pixelPassed': True}) + '\n'
            + '12 ms gpu recovery page capture ' + json.dumps({'stage': stage, 'empty': False,
                'bytes': 100, 'size': {'width': 800, 'height': 600},
                'viewport': {'width': 800, 'height': 600},
                'clientBounds': {'x': 0, 'y': 0, 'width': 800, 'height': 600},
                'marker': {'x': 50, 'y': 100, 'width': 40, 'height': 40},
                'background': [232, 245, 233]}) + '\n'
            + '13 ms gpu recovery page capture acknowledged ' + json.dumps({'stage': stage, 'runId': run_id,
                'captured': True, 'pixelPassed': True}) + '\n'
            for stage in stages)
        captures = {stage: {'sha256': 'b' * 64, 'bytes': 128, 'width': 800, 'height': 600,
                            'geometry': {'x': 0, 'y': 0, 'width': 800, 'height': 600,
                                         'fixed': 65536, 'visible': 1},
                            'client_bounds': {'x': 0, 'y': 0, 'width': 800, 'height': 600},
                            'compositor_identity': identity} for stage in stages}
        pixels = {stage: {'front': {'passed': True}, 'page': {'passed': True}} for stage in stages}
        assess = lambda text=log, files=captures, error=None, before=identity, after=identity: \
            consumer.recovery_front_capture_result(text, files, run_id, error, before, after, pixels)
        self.assertTrue(assess()['passed'])
        self.assertFalse(assess(text=log.replace(run_id, 'c' * 32))['passed'])
        self.assertFalse(assess(text=log.replace('settled_2s', 'recovered', 1))['passed'])
        self.assertFalse(assess(text=log.replace('pre_fault', 'recovered', 1))['passed'])
        self.assertFalse(assess(text=log.replace('"background": [232, 245, 233]',
                                                   '"background": [0, 0, 0]', 1))['passed'])
        failed_pixels = log.replace('"stage": "settled_2s", "runId": "' + run_id
                                   + '", "captured": true, "pixelPassed": true}',
                                   '"stage": "settled_2s", "runId": "' + run_id
                                   + '", "captured": true, "pixelPassed": false}', 1)
        pixels['settled_2s']['front']['passed'] = False
        failed_post_fault = assess(text=failed_pixels)
        self.assertFalse(failed_post_fault['passed'])
        self.assertEqual(len(failed_post_fault['page_captures']), 4)
        self.assertTrue(failed_post_fault['checks']['ordered_page_acknowledgements'])
        pixels['settled_2s']['front']['passed'] = True
        failed_pre_fault = log.replace('"stage": "pre_fault", "runId": "' + run_id
                                      + '", "captured": true, "pixelPassed": true}',
                                      '"stage": "pre_fault", "runId": "' + run_id
                                      + '", "captured": true, "pixelPassed": false}', 1)
        failed_pre_fault += '14 ms gpu recovery fault {"requested":true}\n'
        pixels['pre_fault']['front']['passed'] = False
        pre_fault_result = assess(text=failed_pre_fault)
        self.assertFalse(pre_fault_result['passed'])
        self.assertFalse(pre_fault_result['checks']['healthy_pre_fault_before_injection'])
        pixels['pre_fault']['front']['passed'] = True
        page_ack = '13 ms gpu recovery page capture acknowledged ' + json.dumps(
            {'stage': 'settled_2s', 'runId': run_id, 'captured': True, 'pixelPassed': True})
        page_failed = log.replace(page_ack, page_ack.replace('"pixelPassed": true',
                                                             '"pixelPassed": false'), 1)
        pixels['settled_2s']['page']['passed'] = False
        page_failure = assess(text=page_failed)
        self.assertFalse(page_failure['passed'])
        self.assertTrue(page_failure['checks']['ordered_page_acknowledgements'])
        pixels['settled_2s']['page']['passed'] = True
        self.assertFalse(assess(files={})['passed'])
        self.assertFalse(assess(error='grim failed')['passed'])
        self.assertFalse(assess(after={'pid': 42, 'start_ticks': 900})['passed'])
        self.assertFalse(assess(text=log.replace('"empty": false', '"empty": true'))['passed'])
        self.assertFalse(assess(text=log.replace(
            'gpu recovery page capture {', 'gpu recovery page capture [', 1).replace(
            '"height": 600}}', '"height": 600}]', 1))['passed'])
        pixels['settled_4s']['front']['passed'] = False
        self.assertFalse(assess()['passed'])
        pixels['settled_4s']['front']['passed'] = True
        self.assertFalse(consumer.recovery_front_capture_result(
            log, captures, run_id, None, identity, identity, {})['passed'])
        malformed_ack_lines = log.splitlines()
        first_ack = next(index for index, line in enumerate(malformed_ack_lines)
                         if 'gpu recovery front capture acknowledged ' in line)
        malformed_ack_lines[first_ack] = '11 ms gpu recovery front capture acknowledged []'
        malformed_ack_shape = '\n'.join(malformed_ack_lines) + '\n'
        self.assertFalse(assess(text=malformed_ack_shape)['passed'])

    def test_front_pixel_failure_does_not_skip_paired_page_capture_or_pre_fault_gate(self):
        source = (Path(__file__).with_name('g4-electron-probe') / 'main.js').read_text()
        page_flow = source.split('async function recoveryPageCapture(win, stage) {', 1)[1].split(
            '// Runs inside the ordinary sandboxed renderer', 1)[0]
        self.assertIn('typeof value.pixelPassed === \'boolean\'', page_flow)
        self.assertLess(page_flow.index('await recoveryFrontCheckpoint(stage, win)'),
                        page_flow.index('win.webContents.capturePage()'))
        self.assertNotIn('if (!details.frontPixelPassed)', page_flow)
        fixture = source.split('async function exerciseGPURecovery(win) {', 1)[1].split(
            'async function exerciseLifecycle(win)', 1)[0]
        baseline = fixture.index("recoveryPageCapture(win, 'pre_fault')")
        gate = fixture.index('if (!healthyPixels.frontPixelPassed || !healthyPixels.pagePixelPassed)')
        fault = fixture.index('process.kill(fault.pid)')
        self.assertLess(baseline, gate)
        self.assertLess(gate, fault)


if __name__ == '__main__':
    unittest.main()
