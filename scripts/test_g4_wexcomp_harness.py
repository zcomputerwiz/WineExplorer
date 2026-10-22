#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Owned compositor lifecycle and private endpoint setup; no real display required."""
from pathlib import Path
import tempfile
import unittest
from unittest import mock
from graphics_wexcomp import OwnedWexcomp, capture_command, verify_native_gl_driver


class OwnedCompositor(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='wexcomp-harness-unit-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.binary = self.root / 'compositor'
        self.binary.write_bytes(b'test-only fixture')

    def test_private_environment_and_bounded_exact_process_cleanup(self):
        env = dict(DISPLAY=':live', WAYLAND_DISPLAY='live', DBUS_SESSION_BUS_ADDRESS='live', WAYLAND_SOCKET='99')
        original = dict(env)
        control = OwnedWexcomp(self.binary, self.root, self.root, env)
        process = mock.Mock(pid=42)
        process.poll.return_value = None
        original_read_text = Path.read_text
        stat_fields = ['S'] + ['0'] * 18 + ['900']
        def read_text(path, *args, **kwargs):
            if str(path).replace('\\', '/') == '/proc/42/stat':
                return '42 (wexcomp) ' + ' '.join(stat_fields)
            return original_read_text(path, *args, **kwargs)

        def protocols(*args, **kwargs):
            kwargs['stdout'].write(b"interface: 'wex_window_manager_v1'\n")

        with mock.patch('graphics_wexcomp.subprocess.Popen', return_value=process) as launch, \
                mock.patch('graphics_wexcomp.subprocess.run', side_effect=protocols), \
                mock.patch.object(Path, 'read_text', read_text), \
                mock.patch.object(Path, 'is_socket', return_value=True):
            record = control.start()
            started_env = launch.call_args.kwargs['env']
            for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'DBUS_SESSION_BUS_ADDRESS'):
                self.assertNotIn(key, started_env)
            self.assertEqual(started_env['WLR_BACKENDS'], 'headless')
            self.assertEqual(started_env['WLR_RENDERER'], 'pixman')
            self.assertTrue(launch.call_args.kwargs['start_new_session'])
            self.assertEqual(env['WAYLAND_DISPLAY'], 'wex-shell-0')
            self.assertNotIn('DISPLAY', env)
            self.assertTrue(record['private_protocol_verified'])
            self.assertEqual(original['DISPLAY'], ':live')
            control.verify()
            self.assertTrue(record['alive_at_end'])
            control.stop()
            process.terminate.assert_called_once()
            process.wait.assert_called_once_with(timeout=10)

    def test_existing_runtime_not_reused(self):
        (self.root / 'wayland-runtime').mkdir()
        control = OwnedWexcomp(self.binary, self.root, self.root, {})
        with mock.patch('graphics_wexcomp.subprocess.Popen') as launch, self.assertRaises(FileExistsError):
            control.start()
        launch.assert_not_called()

    def test_only_explicit_outer_renderers(self):
        self.assertEqual(OwnedWexcomp(self.binary, self.root, self.root, {}, renderer='gles2').renderer, 'gles2')
        with self.assertRaises(ValueError):
            OwnedWexcomp(self.binary, self.root, self.root, {}, renderer='automatic-fallback')

    def test_compositor_identity_includes_kernel_start_ticks(self):
        control = OwnedWexcomp(self.binary, self.root, self.root, {})
        control.process = mock.Mock(pid=42)
        control.process.poll.return_value = None
        fields = ['S'] + ['0'] * 18 + ['900']
        with mock.patch.object(Path, 'read_text', return_value='42 (wexcomp) ' + ' '.join(fields)):
            identity = control.identity()
            self.assertEqual(identity, {'pid': 42, 'start_ticks': 900})
            self.assertEqual(control.verify_identity(identity), identity)
            with self.assertRaisesRegex(RuntimeError, 'identity changed'):
                control.verify_identity({'pid': 41, 'start_ticks': 900})
        control.process.poll.return_value = 1
        with self.assertRaisesRegex(RuntimeError, 'not running'):
            control.identity()

    def test_outer_renderer_requires_its_own_unique_live_context(self):
        control = OwnedWexcomp(self.binary, self.root, self.root, {}, renderer='gles2')
        control.process = mock.Mock()
        control.process.poll.return_value = None
        control.record = {}
        text = 'Creating GLES2 renderer\nGL renderer: SVGA3D; build: RELEASE; LLVM;\n'
        path = self.root / 'wexcomp.log'
        path.write_text(text)
        self.assertTrue(control.verify_gpu_renderer('svga')['passed'])
        for invalid in ('', text * 2, text.replace('SVGA3D', 'llvmpipe'), text.replace('Creating GLES2 renderer', 'other')):
            path.write_text(invalid)
            with self.subTest(text=invalid), self.assertRaises(RuntimeError):
                control.verify_gpu_renderer('svga')
        path.write_text(text)
        with self.assertRaises(RuntimeError):
            control.verify_gpu_renderer('d3d12')
        control.process.poll.return_value = 1
        with self.assertRaises(RuntimeError):
            control.verify_gpu_renderer('svga')

    def test_missing_private_protocol_is_not_success(self):
        control = OwnedWexcomp(self.binary, self.root, self.root, {})
        process = mock.Mock()
        process.poll.return_value = None
        with mock.patch('graphics_wexcomp.subprocess.Popen', return_value=process), \
                mock.patch('graphics_wexcomp.subprocess.run'), \
                mock.patch.object(Path, 'is_socket', return_value=True), \
                self.assertRaisesRegex(RuntimeError, 'lacks project protocol'):
            control.start()
        control.stop()
        process.terminate.assert_called_once()

    def test_virgl_outer_context_rejects_nested_software(self):
        control = OwnedWexcomp(self.binary, self.root, self.root, {}, renderer='gles2')
        control.process = mock.Mock()
        control.process.poll.return_value = None
        control.record = {}
        for device, passed in (('D3D12 (Intel test)', True), ('llvmpipe', False), ('softpipe', False)):
            (self.root / 'wexcomp.log').write_text('Creating GLES2 renderer\nGL renderer: virgl (' + device + ')\n')
            if passed:
                self.assertTrue(control.verify_gpu_renderer('virgl')['passed'])
            else:
                with self.assertRaises(RuntimeError):
                    control.verify_gpu_renderer('virgl')

    def test_exited_compositor_and_capture_command(self):
        control = OwnedWexcomp(self.binary, self.root, self.root, {})
        with self.assertRaises(RuntimeError):
            control.verify()
        self.assertEqual(capture_command(self.root / 'frame.png'), ['grim', str(self.root / 'frame.png')])

    def test_scale_requires_exact_live_owned_endpoint(self):
        control = OwnedWexcomp(self.binary, self.root, self.root, {})
        with self.assertRaises(RuntimeError):
            control.set_output_scale(1.25)
        with self.assertRaises(ValueError):
            control.set_output_scale(float('nan'))
        control.record = {'private_protocol_verified': True}
        control.process = mock.Mock()
        control.process.poll.return_value = None
        control.environment.update(WAYLAND_DISPLAY='live', XDG_RUNTIME_DIR='live')
        with mock.patch('graphics_wexcomp.subprocess.run') as run, self.assertRaises(RuntimeError):
            control.set_output_scale(1.25)
        run.assert_not_called()

    def test_scale_verifies_reported_state(self):
        env = dict(WAYLAND_DISPLAY='wex-shell-0', XDG_RUNTIME_DIR=str(self.root / 'wayland-runtime'))
        control = OwnedWexcomp(self.binary, self.root, self.root, env)
        control.record = {'private_protocol_verified': True}
        control.process = mock.Mock()
        control.process.poll.return_value = None
        state = mock.Mock(stdout='HEADLESS-1 "Headless output"\n  Scale: 1.250000\n')
        with mock.patch('graphics_wexcomp.subprocess.run', return_value=state) as run:
            control.set_output_scale(1.25)
            self.assertEqual(run.call_args.kwargs['env'], env)
            self.assertEqual(control.record['output_scales'], [{'requested': 1.25, 'observed': 1.25}])
        with mock.patch('graphics_wexcomp.subprocess.run', return_value=state), self.assertRaises(RuntimeError):
            control.set_output_scale(1.5)


class NativeGLVerification(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='wexcomp-egl-unit-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.env = {'WAYLAND_DISPLAY': 'wex-shell-0', 'XDG_RUNTIME_DIR': str(self.root / 'wayland-runtime')}

    def probe(self, text, code=0):
        def run(*args, **kwargs):
            kwargs['stdout'].write(text)
            return mock.Mock(returncode=code)
        with mock.patch('graphics_wexcomp.subprocess.run', side_effect=run):
            return verify_native_gl_driver(self.env, self.root, 'd3d12')

    def test_actual_native_renderer_required(self):
        text = 'Wayland platform:\nOpenGL core profile renderer: D3D12 (Intel(R) UHD Graphics 750)\n'
        self.assertTrue(self.probe(text)['passed'])
        for invalid in ('', text.replace('Wayland', 'X11'), text.replace('D3D12', 'llvmpipe'),
                        text + 'llvmpipe', text.replace('Intel(R) UHD Graphics 750', 'Microsoft Basic Render Driver')):
            with self.subTest(text=invalid), self.assertRaises(RuntimeError):
                self.probe(invalid)
        with self.assertRaises(RuntimeError):
            self.probe(text, code=1)

    def test_no_foreign_endpoint_or_glx(self):
        for change in ({'WAYLAND_DISPLAY': 'live'}, {'XDG_RUNTIME_DIR': '/run/user/live'}, {'DISPLAY': ':0'}):
            original = self.env.copy()
            self.env.update(change)
            with mock.patch('graphics_wexcomp.subprocess.run') as run, self.assertRaises(RuntimeError):
                verify_native_gl_driver(self.env, self.root, 'd3d12')
            run.assert_not_called()
            self.env = original
        self.assertIsNone(verify_native_gl_driver({}, self.root, 'software'))

    def test_svga_is_verified_separately_from_wsl(self):
        text = 'Wayland platform:\nOpenGL core profile renderer: SVGA3D; build: RELEASE; LLVM;\n'
        def probe(*args, **kwargs):
            kwargs['stdout'].write(text)
            return mock.Mock(returncode=0)
        with mock.patch('graphics_wexcomp.subprocess.run', side_effect=probe):
            self.assertTrue(verify_native_gl_driver(self.env, self.root, 'svga')['passed'])
            with self.assertRaises(RuntimeError):
                verify_native_gl_driver(self.env, self.root, 'd3d12')


if __name__ == '__main__':
    unittest.main()
