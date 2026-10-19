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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine-build', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--full-suite', action='store_true')
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--vulkan-icd', type=Path,
                        help='Explicit ICD JSON for optional Vulkan control; no inherited driver selection')
    args = parser.parse_args()
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
                               WINEDEBUG='-all,+winediag', LIBGL_ALWAYS_SOFTWARE='1',
                               WINE_D3D_CONFIG='renderer=' + args.renderer)
            if args.renderer == 'vulkan':
                environment.update(VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd))
            result['display'] = environment['DISPLAY']
            result['environment'] = {key: environment[key] for key in
                                     ('DISPLAY', 'WINE', 'WINESERVER', 'WINEDLLOVERRIDES',
                                      'WINEDEBUG', 'LIBGL_ALWAYS_SOFTWARE')}
            result['environment']['WINE_D3D_CONFIG'] = environment['WINE_D3D_CONFIG']
            if icd:
                result['vulkan_icd'] = str(icd)
                result['vulkan_icd_sha256'] = hashlib.sha256(icd.read_bytes()).hexdigest()
            for arch, compiler in (('x86_64', 'x86_64-w64-mingw32-g++'),
                                   ('i686', 'i686-w64-mingw32-g++')):
                exe = output / ('g3-ordering-' + arch + '.exe')
                probes.compile_probe(source / 'test_g3_ordering_probe.cpp', exe, compiler)
                prefix = root / arch
                prefixes.append(prefix)
                env = dict(environment, WINEPREFIX=str(prefix))
                command = [str(wine), str(exe)]
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
                passed = run.returncode == 0 and all(marker in run.stdout for marker in required)
                result['cases'][arch] = {'command': command, 'exit_code': run.returncode,
                                         'passed': passed, 'sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}
                print(arch, 'PASS' if passed else 'FAIL', run.stdout, flush=True)
                if not passed:
                    raise RuntimeError('Actual G3 regression failed: ' + arch)
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
