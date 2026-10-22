#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Actual compositor pixels: same-process vs foreign-HWND DComp and GDI controls."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time
from PIL import Image
from graphics_wexcomp import OwnedWexcomp, verify_native_gl_driver


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine-build', type=Path, required=True)
    parser.add_argument('--wexcomp-bin', type=Path, required=True)
    parser.add_argument('--wexcomp-config', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--arch', choices=('x86_64', 'i686'), default='x86_64')
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='vulkan')
    parser.add_argument('--gl-driver', choices=('software', 'd3d12', 'svga'), default='software')
    parser.add_argument('--compositor-renderer', choices=('pixman', 'gles2'), default='pixman')
    parser.add_argument('--vulkan-icd', type=Path)
    parser.add_argument('--wine-debug', default='-all,+winediag')
    parser.add_argument('--case', action='append', choices=('own-dcomp', 'foreign-full-dcomp', 'foreign-dcomp', 'foreign-gdi'),
                        help='Run selected controls; the default retains all controls, including known open gaps')
    args = parser.parse_args()
    if args.renderer == 'vulkan' and not args.vulkan_icd:
        parser.error('Vulkan requires an explicit ICD')
    if args.gl_driver != 'software' and args.renderer != 'gl':
        parser.error('Accelerated GL drivers require --renderer gl')
    if args.compositor_renderer == 'gles2' and args.gl_driver == 'software':
        parser.error('GLES2 outer control requires an explicit accelerated driver')
    build = args.wine_build.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = Path(__file__).resolve().parent
    helpers = load('graphics_helpers', source / 'check-g3-correctness.py')
    probes = load('graphics_probes', source / 'test_graphics_probes.py')
    exe = output / 'foreign-present.exe'
    probes.compile_probe(source / 'test_g4_foreign_present_probe.cpp', exe, args.arch + '-w64-mingw32-g++')
    environment = {k: v for k, v in os.environ.items() if not k.startswith(('WINE', 'VK_', 'WEX_', 'GALLIUM_', 'MESA_'))
                   and k not in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'LD_PRELOAD', 'LD_LIBRARY_PATH')}
    wine, server = build / 'wine', build / 'server/wineserver'
    prefix = output / 'prefix'
    environment.update(WINEPREFIX=str(prefix), WINE=str(wine), WINESERVER=str(server),
                       WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d;d3d11,dxgi,wined3d,dcomp=b;winex11.drv=d;winewayland.drv=b',
                       WINEDEBUG=args.wine_debug, LIBGL_ALWAYS_SOFTWARE='1', WINE_D3D_CONFIG='renderer=' + args.renderer)
    helpers.configure_gl_driver(environment, args.gl_driver)
    if args.vulkan_icd:
        icd = str(args.vulkan_icd.resolve(strict=True))
        environment.update(VK_DRIVER_FILES=icd, VK_ICD_FILENAMES=icd)
    compositor = OwnedWexcomp(args.wexcomp_bin.resolve(strict=True), args.wexcomp_config.resolve(strict=True),
                              output, environment, args.compositor_renderer)
    result = {'classification': 'Native wexcomp process-boundary presentation diagnostic; explicit outer renderer',
              'arch': args.arch, 'renderer': args.renderer, 'gl_driver': args.gl_driver, 'cases': {}}
    child = None
    try:
        result['wexcomp'] = compositor.start()
        if args.compositor_renderer == 'gles2':
            result['outer_gl_driver_verification'] = compositor.verify_gpu_renderer(args.gl_driver)
        result['gl_driver_verification'] = verify_native_gl_driver(environment, output, args.gl_driver)
        boot = helpers.run_logged([str(wine), 'wineboot', '-u'], environment, output, 'wineboot', 90)
        if boot.returncode:
            raise RuntimeError('Owned prefix initialization failed')
        for name, arguments, color in (('own-dcomp', [], (255, 0, 0)),
                                        ('foreign-full-dcomp', ['--foreign-full'], (255, 0, 0)),
                                        ('foreign-dcomp', ['--foreign'], (255, 0, 0)),
                                        ('foreign-gdi', ['--foreign-gdi'], (0, 255, 0))):
            if args.case and name not in args.case:
                continue
            stdout = output / (name + '.stdout.log')
            with stdout.open('wb') as out, (output / (name + '.stderr.log')).open('wb') as err:
                child = subprocess.Popen([str(wine), str(exe)] + arguments, env=environment,
                                         stdout=out, stderr=err, start_new_session=True)
                deadline = time.monotonic() + 20
                while 'FRAME_SENT ' not in stdout.read_text(errors='replace'):
                    if child.poll() is not None:
                        break
                    if time.monotonic() >= deadline:
                        raise RuntimeError('Probe did not submit first frame: ' + name)
                    time.sleep(.1)
                time.sleep(1)
                capture = output / (name + '.png')
                subprocess.run(['grim', str(capture)], env=environment, check=True, timeout=10)
                code = child.wait(timeout=20)
                child = None
            with Image.open(capture) as image:
                count = sum(1 for pixel in image.convert('RGB').getdata()
                            if all(abs(pixel[i] - color[i]) <= 4 for i in range(3)))
            submitted = 'FRAME_SENT ok=1' in stdout.read_text(errors='replace')
            expected = 4096
            passed = code == 0 and count == expected and submitted
            result['cases'][name] = dict(exit_code=code, expected_rgb=color, matching_front_pixels=count,
                                        expected_front_pixels=expected,
                                        first_frame_submitted=submitted,
                                        passed=passed)
            print(name, 'PASS' if passed else 'FAIL', result['cases'][name], flush=True)
        compositor.verify()
        return 0 if all(case['passed'] for case in result['cases'].values()) else 1
    finally:
        if child and child.poll() is None:
            child.terminate()
            child.wait(timeout=5)
        subprocess.run([str(server), '-k'], env=environment, timeout=15)
        subprocess.run([str(server), '-w'], env=environment, check=True, timeout=15)
        result['remaining_servers'] = helpers.matching_servers(server, prefix)
        compositor.stop()
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        if result['remaining_servers']:
            raise RuntimeError('Owned servers remain')


if __name__ == '__main__':
    raise SystemExit(main())
