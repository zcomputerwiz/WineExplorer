#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Run Phase G4B Presentation & Pacing probe on an explicit Wine build.

Verifies:
- D3D11 logical backbuffer index remains 0 while underlying buffers rotate
- ResizeBuffers1 resets current backbuffer index to 0
- Frame latency waitable object creation, signal state, latency bounds
- Occluded / minimized window presentation returns DXGI_STATUS_OCCLUDED
- Composition event-driven frame pacing loop (30 frames)
- Non-waitable swapchain rejection semantics
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
    stdout = output / (label + '.stdout.log')
    stderr = output / (label + '.stderr.log')
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
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    args = parser.parse_args()

    if not shutil.which('Xvfb'):
        parser.error('Xvfb is required; no existing display will be reused')
    sockets = Path('/tmp/.X11-unix')
    if sockets.exists() and os.statvfs(sockets).f_flag & os.ST_RDONLY:
        parser.error('X socket directory is read-only (e.g. WSLg); use an owned socket directory '
                     'bound over it in a private mount namespace, never change global X sockets')

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
              'classification': 'Xvfb + software OpenGL functional control (G4B Presentation & Pacing)',
              'cases': {}}

    display = None
    environment = None
    prefixes = []
    scratch = tempfile.TemporaryDirectory(prefix='wex-g4-pacing-', delete=False)

    with scratch as temporary, (output / 'xvfb.log').open('wb') as xlog:
        root = Path(temporary)
        read_fd, write_fd = os.pipe()
        try:
            display = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0',
                                        '1280x1024x24', '-nocursor'],
                                       stdout=xlog, stderr=xlog, pass_fds=(write_fd,))
            os.close(write_fd)
            rlist, _, _ = select.select([read_fd], [], [], 15)
            if not rlist:
                raise RuntimeError('Xvfb did not report display number')
            display_num = os.read(read_fd, 16).decode().strip()
            os.close(read_fd)

            environment = {
                'PATH': '/usr/bin:/bin',
                'DISPLAY': ':' + display_num,
                'WINE': str(wine),
                'WINESERVER': str(server),
                'WINEDLLOVERRIDES': 'mscoree,mshtml=;winemenubuilder.exe=d;d3d11,dxgi,wined3d,dcomp=b',
                'WINEDEBUG': '-all,+winediag',
                'LIBGL_ALWAYS_SOFTWARE': '1',
                'GALLIUM_DRIVER': 'llvmpipe',
                'WINE_D3D_CONFIG': 'renderer=' + args.renderer,
                'WINEESYNC': '0',
                'WINEFSYNC': '0',
            }

            for arch, compiler in (('x86_64', 'x86_64-w64-mingw32-g++'),
                                   ('i686', 'i686-w64-mingw32-g++')):
                exe = output / f'test_g4_pacing_probe_{arch}.exe'
                probes.compile_probe(source / 'test_g4_pacing_probe.cpp', exe, compiler)

                prefix = root / f'pfx_{arch}'
                prefixes.append(prefix)
                env = dict(environment)
                env['WINEPREFIX'] = str(prefix)

                command = [str(wine), str(exe)]
                run = run_logged(command, env, output, f'g4b_{arch}', 120)

                required = (
                    'pacing_flip_backbuffer_indexing=PASS',
                    'pacing_frame_latency_waitable_object=PASS',
                    'pacing_occluded_minimized_pacing=PASS',
                    'pacing_composition_waitable_pacing_loop=PASS',
                    'pacing_non_waitable_swapchain_behavior=PASS',
                )
                passed = run.returncode == 0 and all(marker in run.stdout for marker in required)
                result['cases'][arch] = {
                    'command': command,
                    'exit_code': run.returncode,
                    'passed': passed,
                    'sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                    'stdout': run.stdout,
                }
                print(f"{arch} {'PASS' if passed else 'FAIL'}")
                print(run.stdout)
                if not passed:
                    raise RuntimeError(f'G4B pacing probe failed for {arch}: exit {run.returncode}\n{run.stderr}')

        finally:
            for prefix in prefixes:
                try:
                    subprocess.run([str(server), '-w'], env={'WINEPREFIX': str(prefix)},
                                   timeout=10, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except BaseException:
                    for pid in matching_servers(server, str(prefix)):
                        try:
                            os.kill(pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
            if display and display.poll() is None:
                display.terminate()
                try:
                    display.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    display.kill()

    results_file = output / 'results.json'
    results_file.write_text(json.dumps(result, indent=2))
    print(f"\nAll architectures passed Phase G4B Presentation & Pacing verification.")
    print(f"Results saved to {results_file}")


if __name__ == '__main__':
    main()
