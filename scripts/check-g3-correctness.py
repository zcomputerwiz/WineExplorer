#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Run original G3 regressions on an explicit build and owned display/prefixes.

Software Xvfb evidence is functional evidence, never physical-GPU acceptance.
No installed runtime, live prefix, inherited override, or global cleanup is used.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import select
import signal
import shutil
import subprocess
import tempfile


def run_logged(command, environment, output, label, timeout):
    """Keep diagnostics even when a regression hangs instead of exiting."""
    stdout = output / (label + '.stdout.log')
    stderr = output / (label + '.stderr.log')
    # Files, not pipes: a timed-out child's descendants cannot keep communicate()
    # waiting for EOF. The finally block terminates only our matching servers.
    with stdout.open('wb') as out, stderr.open('wb') as err:
        process = subprocess.Popen(command, env=environment, stdout=out, stderr=err,
                                   start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
        except BaseException:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
            raise
    return subprocess.CompletedProcess(command, code, stdout.read_text(errors='replace'),
                                       stderr.read_text(errors='replace'))


def matching_servers(server, prefix):
    """Read-only backstop after -w; never select servers merely by process name."""
    matches = []
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            if process.stat().st_uid != os.getuid() or (process / 'exe').resolve() != server:
                continue
            if b'WINEPREFIX=' + os.fsencode(prefix) in (process / 'environ').read_bytes().split(bytes([0])):
                matches.append(int(process.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
    return matches


def configure_private_theme(wine, environment, output, label, mode, style):
    """Only the runner's newly allocated prefix; never an installed/user prefix."""
    if mode == 'default':
        return []
    key = r'HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager'
    values = [('ThemeActive', '0' if mode == 'classic' else '1')]
    if mode == 'open-luna':
        values += [('DllName', 'Z:' + str(style).replace('/', '\\')),
                   ('ColorName', 'NormalColor'), ('SizeName', 'NormalSize')]
    records = []
    for name, value in values:
        command = [str(wine), 'reg', 'add', key, '/v', name, '/t', 'REG_SZ', '/d', value, '/f']
        result = run_logged(command, environment, output, label + '-theme-' + name, 90)
        records.append({'command': command, 'exit_code': result.returncode})
        if result.returncode:
            raise RuntimeError('Private theme setup failed: ' + name)
    environment['WEX_G4_THEME_EXPECT'] = mode
    if style:
        environment['WEX_G4_THEME_STYLE'] = 'Z:' + str(style).replace('/', '\\')
    return records


def configure_gl_driver(environment, driver):
    """Explicit per-run selection; never silently inherit a loader override."""
    for key in ('GALLIUM_DRIVER', 'MESA_LOADER_DRIVER_OVERRIDE', 'LD_LIBRARY_PATH'):
        environment.pop(key, None)
    environment['LIBGL_ALWAYS_SOFTWARE'] = '1' if driver == 'software' else '0'
    if driver == 'd3d12':
        environment.update(GALLIUM_DRIVER='d3d12', LD_LIBRARY_PATH='/usr/lib/wsl/lib')


def verify_gl_driver(environment, output, driver):
    if driver == 'software':
        return None
    result = run_logged(['glxinfo', '-B'], environment, output, 'gl-driver', 30)
    passed = (result.returncode == 0 and 'Accelerated: yes' in result.stdout
              and re.search(r'OpenGL renderer string: D3D12 \([^\n]+\)', result.stdout) is not None
              and not re.search(r'llvmpipe|softpipe|swiftshader', result.stdout, re.I))
    if not passed:
        raise RuntimeError('Requested accelerated D3D12 driver unavailable; see gl-driver logs')
    return {'passed': True, 'stdout': result.stdout, 'exit_code': result.returncode}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine-build', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--wine-debug', default='-all,+winediag',
                        help='Wine diagnostic channels (recorded with the run environment)')
    parser.add_argument('--full-suite', action='store_true')
    parser.add_argument('--memory-profile', action='store_true',
                        help='Log test-process virtual address-space bounds before each G4 case')
    parser.add_argument('--hwnd-integration', action='store_true',
                        help='Separate G4 HWND child/layer integration gate; failures remain open')
    parser.add_argument('--fence-interleaving', action='store_true',
                        help='Separate test-process IAT-instrumented Wine fence interleaving/fault controls')
    parser.add_argument('--nonclient-diagnostic', action='store_true',
                        help='Separate native-caption baseline/preservation diagnostic; may fail before D3D setup')
    parser.add_argument('--theme-mode', choices=('default', 'classic', 'open-luna'), default='default',
                        help='Configure only this run\'s new prefixes before launching probes')
    parser.add_argument('--theme-style', type=Path,
                        help='Locally built open Luna style, required for --theme-mode open-luna')
    parser.add_argument('--x11-unmanaged', action='store_true',
                        help='Private-prefix unmanaged windows: Wine draws Classic frames without a host WM')
    parser.add_argument('--g4-correctness', action='store_true',
                        help='Run the independent G4 target-pixel and observer regression probe')
    parser.add_argument('--g4-case', choices=('supported_pixels_and_transactions',
                        'unsupported_properties_fail_honestly', 'retained_multilayer_and_transform_pixels',
                        'cross_thread_completion_pixels', 'imported_fence_observers', 'fence_event_handle_lifetime',
                        'fence_waiter_cleanup', 'fence_creator_exit_and_reexport',
                        'device_virtual_memory_reclaim',
                        'retained_changing_frame_pixels', 'retained_formats_resize_and_lifetime',
                        'retained_nested_group_pixels', 'concurrent_commit_and_device_lifetime',
                        'multi_target_transactions', 'reparented_tree_lifetime', 'fence_capacity_and_reclaim',
                        'opaque_retained_gpu_delivery', 'opaque_multilayer_gpu_delivery',
                        'video_capability_consistency', 'multisample_format_capabilities', 'present1_dirty_retention',
                        'bitmap_interpolation_transactions', 'border_mode_transactions', 'border_mode_geometry',
                        'fence_process_death_and_idle',
                        'application_background_pixels', 'application_background_detach',
                        'application_background_expose'),
                        help='Run one named G4 regression for controlled before/after comparisons')
    parser.add_argument('--upstream-tests', action='store_true',
                        help='Run built upstream d3d11/dxgi suites for both architectures; retain failures')
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--gl-driver', choices=('software', 'd3d12'), default='software',
                        help='Explicit accelerated WSL Mesa D3D12 control; verified with glxinfo')
    parser.add_argument('--vulkan-icd', type=Path,
                        help='Explicit ICD JSON for optional Vulkan control; no inherited driver selection')
    args = parser.parse_args()
    if args.gl_driver != 'software' and (args.renderer != 'gl' or args.full_suite):
        parser.error('--gl-driver d3d12 requires GL and a standalone probe suite')
    if args.theme_mode == 'open-luna' and not args.theme_style:
        parser.error('--theme-mode open-luna requires --theme-style')
    if args.theme_style and args.theme_mode != 'open-luna':
        parser.error('--theme-style is only valid with --theme-mode open-luna')
    theme_style = args.theme_style.resolve(strict=True) if args.theme_style else None
    if (args.theme_mode != 'default' or args.x11_unmanaged) and (args.full_suite or args.upstream_tests):
        parser.error('Explicit theme/window-manager controls are supported only by the standalone probes')
    if args.nonclient_diagnostic and (args.fence_interleaving or args.hwnd_integration or args.upstream_tests
                                     or args.g4_correctness or args.full_suite or args.g4_case or args.memory_profile):
        parser.error('--nonclient-diagnostic must run separately from other suites')
    if args.fence_interleaving and (args.hwnd_integration or args.upstream_tests or args.g4_correctness
                                   or args.full_suite or args.g4_case or args.memory_profile):
        parser.error('--fence-interleaving must run separately from other suites')
    if args.memory_profile and not args.g4_correctness:
        parser.error('--memory-profile requires --g4-correctness')
    if args.hwnd_integration and (args.upstream_tests or args.g4_correctness or args.full_suite or args.g4_case):
        parser.error('--hwnd-integration must be run separately from other suites')
    if args.upstream_tests and (args.g4_correctness or args.full_suite):
        parser.error('--upstream-tests must be run separately from the custom probes')
    if args.g4_case and not args.g4_correctness:
        parser.error('--g4-case requires --g4-correctness')
    if not shutil.which('Xvfb'):
        parser.error('Xvfb is required; no existing display will be reused')
    sockets = Path('/tmp/.X11-unix')
    if sockets.exists() and os.statvfs(sockets).f_flag & os.ST_RDONLY:
        parser.error('X socket directory is read-only (e.g. WSLg); use an owned socket directory '
                     'bound over it in a private mount namespace, never change global X sockets')
    if args.renderer == 'vulkan' and not args.vulkan_icd:
        parser.error('--renderer vulkan requires --vulkan-icd')
    icd = args.vulkan_icd.resolve(strict=True) if args.vulkan_icd else None
    build = args.wine_build.resolve(strict=True)
    wine, server = build / 'wine', build / 'server/wineserver'
    for executable in (wine, server):
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise ValueError('Missing executable from selected build: ' + str(executable))
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('graphics_probes', source / 'test_graphics_probes.py')
    probes = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probes)
    result = {'wine_build': str(build), 'uid': os.getuid(),
              'classification': ('Xvfb + software OpenGL functional control' if args.renderer == 'gl'
                                 else 'Xvfb + explicitly selected Vulkan ICD functional control'),
              'cases': {}}
    display = None
    environment = None
    prefixes = []
    scratch = tempfile.TemporaryDirectory(prefix='wex-g3-correctness-', delete=False)
    with scratch as temporary, \
            (output / 'xvfb.log').open('wb') as xlog:
        root = Path(temporary)
        read_fd, write_fd = os.pipe()
        try:
            display = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0',
                                        '1024x768x24', '-nolisten', 'tcp'],
                                       pass_fds=(write_fd,), stdout=xlog, stderr=xlog,
                                       start_new_session=True)
            os.close(write_fd)
            write_fd = None
            if not select.select([read_fd], [], [], 10)[0]:
                raise TimeoutError('Owned Xvfb did not allocate a display')
            number = os.read(read_fd, 64).strip().decode('ascii')
            if not number.isdigit() or display.poll() is not None:
                raise ValueError('Owned Xvfb failed: inspect xvfb.log')
            environment = {key: value for key, value in os.environ.items()
                           if not key.startswith(('WINE', 'VK_')) and
                           key not in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY')}
            environment.update(DISPLAY=':' + number, WINE=str(wine), WINESERVER=str(server),
                               WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d;d3d11,dxgi,wined3d,dcomp=b',
                               WINEDEBUG=args.wine_debug, LIBGL_ALWAYS_SOFTWARE='1',
                               WINE_D3D_CONFIG='renderer=' + args.renderer)
            configure_gl_driver(environment, args.gl_driver)
            result['gl_driver'] = args.gl_driver
            result['gl_driver_verification'] = verify_gl_driver(environment, output, args.gl_driver)
            if args.gl_driver != 'software':
                result['classification'] = 'Xvfb + accelerated WSL Mesa D3D12 functional control; not desktop acceptance'
            if args.renderer == 'vulkan':
                environment.update(VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd))
            if args.memory_profile:
                environment['WEX_G4_MEMORY_TRACE'] = '1'
            result['memory_profile'] = args.memory_profile
            result['test_process_iat_instrumentation'] = args.fence_interleaving
            result['theme_mode'] = args.theme_mode
            result['x11_unmanaged'] = args.x11_unmanaged
            if theme_style:
                result['theme_style'] = str(theme_style)
                result['theme_style_sha256'] = hashlib.sha256(theme_style.read_bytes()).hexdigest()
            result['display'] = environment['DISPLAY']
            result['environment'] = {key: environment[key] for key in
                                     ('DISPLAY', 'WINE', 'WINESERVER', 'WINEDLLOVERRIDES',
                                      'WINEDEBUG', 'LIBGL_ALWAYS_SOFTWARE')}
            result['environment']['WINE_D3D_CONFIG'] = environment['WINE_D3D_CONFIG']
            for key in ('GALLIUM_DRIVER', 'LD_LIBRARY_PATH'):
                if key in environment:
                    result['environment'][key] = environment[key]
            if icd:
                result['vulkan_icd'] = str(icd)
                result['vulkan_icd_sha256'] = hashlib.sha256(icd.read_bytes()).hexdigest()
            for arch, compiler in (('x86_64', 'x86_64-w64-mingw32-g++'),
                                   ('i686', 'i686-w64-mingw32-g++')):
                if args.upstream_tests:
                    for module in ('dxgi', 'd3d11'):
                        pe_arch = 'i386' if arch == 'i686' else arch
                        exe = build / 'dlls' / module / 'tests' / (pe_arch + '-windows') / (module + '_test.exe')
                        if not exe.is_file():
                            raise FileNotFoundError('Missing upstream test executable: ' + str(exe))
                        label = module + '-' + arch
                        prefix = root / label
                        prefixes.append(prefix)
                        env = dict(environment, WINEPREFIX=str(prefix), WINETEST_INTERACTIVE='0',
                                   WINETEST_NO_MT_D3D='1')
                        command = [str(wine), str(exe), module, '--single']
                        try:
                            run = run_logged(command, env, output, label, 1200)
                            code = run.returncode
                        except subprocess.TimeoutExpired:
                            code = 124
                        stdout = (output / (label + '.stdout.log')).read_text(errors='replace')
                        stderr = (output / (label + '.stderr.log')).read_text(errors='replace')
                        summaries = [line for line in (stdout + stderr).splitlines() if 'tests executed' in line]
                        executed = sum(int(match.group(1)) for line in summaries
                                       if (match := re.search(r'(\d+) tests executed', line)))
                        passed = code == 0 and executed > 0
                        result['cases'][label] = {'command': command, 'exit_code': code,
                                                  'passed': passed, 'summaries': summaries,
                                                  'sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}
                        print(label, 'PASS' if passed else 'FAIL', code, summaries, flush=True)
                    continue
                probe_name = 'test_g4_correctness_probe.cpp' if args.g4_correctness else 'test_g3_ordering_probe.cpp'
                exe = output / (('g4-correctness-' if args.g4_correctness else 'g3-ordering-') + arch + '.exe')
                if args.hwnd_integration:
                    probe_name = 'test_g4_hwnd_integration_probe.cpp'
                    exe = output / ('g4-hwnd-integration-' + arch + '.exe')
                if args.fence_interleaving:
                    probe_name = 'test_g4_fence_interleaving_probe.cpp'
                    exe = output / ('g4-fence-interleaving-' + arch + '.exe')
                if args.nonclient_diagnostic:
                    probe_name = 'test_g4_correctness_probe.cpp'
                    exe = output / ('g4-nonclient-' + arch + '.exe')
                probes.compile_probe(source / probe_name, exe, compiler)
                prefix = root / arch
                prefixes.append(prefix)
                env = dict(environment, WINEPREFIX=str(prefix))
                theme_setup = configure_private_theme(wine, env, output, arch, args.theme_mode, theme_style)
                if args.x11_unmanaged:
                    setup = [str(wine), 'reg', 'add', r'HKCU\Software\Wine\X11 Driver',
                             '/v', 'Managed', '/t', 'REG_SZ', '/d', 'N', '/f']
                    configured = run_logged(setup, env, output, arch + '-unmanaged', 90)
                    if configured.returncode:
                        raise RuntimeError('Private unmanaged-window setup failed')
                command = [str(wine), str(exe)]
                if args.nonclient_diagnostic:
                    command.append('--nonclient-diagnostic')
                if args.g4_case:
                    command.extend(('--case', args.g4_case))
                run = run_logged(command, env, output, arch, 120)
                required = ('ordinary_buffer_copy=PASS', 'ordinary_texture1d_copy=PASS',
                            'ordinary_texture2d_copy=PASS',
                            'ordinary_texture3d_copy=PASS',
                            'pending_fence_clear_and_query_order=PASS',
                            'queued_signal_immediate_release=PASS',
                            'format_layout_rejection=PASS',
                            'multiple_fence_waiters=PASS',
                            'multiframe_changing_patterns=PASS',
                            'handle_duplicate_and_close=PASS',
                            'deferred_context_rejection=PASS')
                if args.g4_correctness:
                    required = ('supported_pixels_and_transactions=PASS',
                                'unsupported_properties_fail_honestly=PASS',
                                'retained_multilayer_and_transform_pixels=PASS',
                                'cross_thread_completion_pixels=PASS',
                                'imported_fence_observers=PASS',
                                'fence_event_handle_lifetime=PASS',
                                'fence_waiter_cleanup=PASS',
                                'fence_creator_exit_and_reexport=PASS',
                                'device_virtual_memory_reclaim=PASS',
                                'retained_changing_frame_pixels=PASS',
                                'retained_formats_resize_and_lifetime=PASS',
                                'retained_nested_group_pixels=PASS',
                                'concurrent_commit_and_device_lifetime=PASS',
                                'multi_target_transactions=PASS',
                                'reparented_tree_lifetime=PASS',
                                'fence_capacity_and_reclaim=PASS',
                                'opaque_retained_gpu_delivery=PASS',
                                'opaque_multilayer_gpu_delivery=PASS',
                                'video_capability_consistency=PASS',
                                'multisample_format_capabilities=PASS',
                                'present1_dirty_retention=PASS', 'bitmap_interpolation_transactions=PASS',
                                'border_mode_transactions=PASS',
                                'border_mode_geometry=PASS',
                                'fence_process_death_and_idle=PASS', 'application_background_pixels=PASS',
                                'application_background_detach=PASS', 'application_background_expose=PASS')
                if args.g4_case:
                    required = (args.g4_case + '=PASS',)
                if args.hwnd_integration:
                    required = ('hwnd_top_child_clip=PASS', 'hwnd_bottom_child_clip=PASS',
                                'hwnd_two_target_retention=PASS')
                if args.fence_interleaving:
                    required = ('fence_signal_during_registration_and_callback_tail=PASS',
                                'fence_registration_failure_rollback=PASS')
                if args.nonclient_diagnostic:
                    required = ('application_nonclient_preservation=PASS',)
                passed = run.returncode == 0 and all(marker in run.stdout for marker in required)
                result['cases'][arch] = {'command': command, 'exit_code': run.returncode,
                                         'passed': passed, 'sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                                         'theme_setup': theme_setup}
                if args.x11_unmanaged:
                    result['cases'][arch]['x11_setup'] = {'command': setup, 'exit_code': configured.returncode}
                print(arch, 'PASS' if passed else 'FAIL', run.stdout, flush=True)
            if not all(case['passed'] for case in result['cases'].values()):
                raise RuntimeError('Graphics regressions failed; inspect each retained log and compare baseline')
            if args.full_suite:
                prefix = root / 'full-suite'
                prefixes.append(prefix)
                command = ['python3', '-B', str(source / 'test_graphics_probes.py'),
                           '--wine', str(wine), '--output-json', str(output / 'full-suite.json')]
                run = run_logged(command, dict(environment, WINEPREFIX=str(prefix)),
                                 output, 'full-suite', 240)
                result['full_suite_exit_code'] = run.returncode
                print(run.stdout, flush=True)
                if run.returncode:
                    raise RuntimeError('Existing G0-G3 suite failed')
        except Exception as error:
            result['failure'] = repr(error)
            raise
        finally:
            os.close(read_fd)
            if write_fd is not None:
                os.close(write_fd)
            cleanup_errors = []
            for prefix in prefixes:
                env = dict(environment or os.environ, WINEPREFIX=str(prefix))
                try:
                    # Wine returns 1 from -k when no server was running. Always
                    # verify bounded -w and process absence, rather than treating
                    # that normal auto-exit as failure or ignoring every error.
                    killed = subprocess.run([str(server), '-k'], env=env, timeout=15)
                    if killed.returncode not in (0, 1):
                        raise RuntimeError('Unexpected wineserver -k exit: ' + str(killed.returncode))
                    subprocess.run([str(server), '-w'], env=env, timeout=15, check=True)
                    remaining = matching_servers(server, prefix)
                    if remaining:
                        raise RuntimeError('Owned server still running: ' + str(remaining))
                    result.setdefault('cleanup', []).append({'prefix': str(prefix),
                                                             'kill_exit': killed.returncode,
                                                             'wait_exit': 0, 'remaining_servers': []})
                except (OSError, subprocess.SubprocessError, RuntimeError) as error:
                    cleanup_errors.append(str(error))
            if display and display.poll() is None:
                os.killpg(display.pid, signal.SIGTERM)
                try:
                    display.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(display.pid, signal.SIGKILL)
                    display.wait(timeout=5)
            if cleanup_errors:
                result['cleanup_errors'] = cleanup_errors
                result['retained_prefix_root'] = str(root)
            else:
                scratch.cleanup()
            (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
            if cleanup_errors:
                raise RuntimeError('Owned-prefix cleanup needs inspection; see results.json')


if __name__ == '__main__':
    main()
