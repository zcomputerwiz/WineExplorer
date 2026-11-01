#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Actual compositor-pixel contract controls for the private render role."""
import argparse
import json
import os
from pathlib import Path
import select
import subprocess
from PIL import Image
from graphics_wexcomp import OwnedWexcomp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wexcomp-bin', type=Path, required=True)
    parser.add_argument('--wexcomp-config', type=Path, required=True)
    parser.add_argument('--wine-build', type=Path, required=True, help='Generated protocol metadata from this explicit build')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--compositor-renderer', choices=('pixman', 'gles2'), default='pixman')
    parser.add_argument('--gl-driver', choices=('software', 'svga'), default='software')
    args = parser.parse_args()
    if (args.gl_driver == 'svga') != (args.compositor_renderer == 'gles2'):
        parser.error('SVGA GPU control requires explicit GLES2; keep software control separate')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    protocols = args.wine_build.resolve(strict=True) / 'dlls/winewayland.drv'
    executable = output / 'render-role-probe'
    command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(protocols),
               str(Path(__file__).with_name('test_wex_render_surface.c')),
               str(protocols / 'xdg-shell-protocol.c'), str(protocols / 'wex-window-v1-protocol.c'),
               str(protocols / 'wlr-foreign-toplevel-management-unstable-v1-protocol.c'),
               '-lwayland-client', '-o', str(executable)]
    subprocess.run(command, check=True, timeout=30)
    environment = {k: v for k, v in os.environ.items() if not k.startswith(('WINE', 'WLR_', 'VK_', 'GALLIUM_', 'MESA_'))
                   and k not in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'LD_PRELOAD', 'LD_LIBRARY_PATH')}
    if args.gl_driver == 'svga':
        environment.update(GALLIUM_DRIVER='svga', LIBGL_ALWAYS_SOFTWARE='0')
    compositor = OwnedWexcomp(args.wexcomp_bin.resolve(strict=True), args.wexcomp_config.resolve(strict=True),
                              output, environment, args.compositor_renderer)
    expected = dict(attached=4096, moved=4096, negative_clip=2288, outside_parent=0, explicit_bounds=600,
                    empty_parent=0, buffer_scale_two=1024, wrong_parent=0, clipped=400,
                    parent_shrunk=0, parent_grown=400, invalid_size=0, reattached=4096,
                    parent_revoked=0, stale_parent=0, new_parent=4096, producer_destroyed=0,
                    producer_recreated=4096, invalid_surface_order_rejected=0, before_disconnect=4096,
                    producer_disconnected=0, producer_reconnected=4096, parent_surface_destroyed=0,
                    destroyed_surface_handle=0)
    result = {'cases': {}, 'classification': 'Private render-role functional contract; not security certification'}
    process = None
    origin = None
    geometry = dict(attached=(0, 0, 64, 64), moved=(40, 30, 64, 64), negative_clip=(0, 0, 44, 52),
                    explicit_bounds=(0, 0, 20, 30), buffer_scale_two=(0, 0, 32, 32),
                    clipped=(180, 180, 20, 20), parent_grown=(180, 180, 20, 20))
    try:
        result['wexcomp'] = compositor.start()
        if args.gl_driver == 'svga':
            result['outer_gl_driver'] = compositor.verify_gpu_renderer('svga')
        with (output / 'probe.stderr.log').open('wb') as err:
            process = subprocess.Popen([str(executable), str(output / 'stage.ack')], env=environment,
                                       stdout=subprocess.PIPE, stderr=err, text=True, bufsize=1)
            for stage, count in expected.items():
                if not select.select([process.stdout], [], [], 20)[0]:
                    raise TimeoutError('No stage: ' + stage)
                line = process.stdout.readline().strip()
                if line != 'STAGE ' + stage:
                    raise RuntimeError('Unexpected stage: ' + line)
                capture = output / (stage + '.png')
                subprocess.run(['grim', str(capture)], env=environment, check=True, timeout=10)
                with Image.open(capture) as image:
                    points = [(i % image.width, i // image.width) for i, pixel in enumerate(image.convert('RGB').getdata())
                              if pixel == (255, 0, 0)]
                actual = len(points)
                bounds = ([min(p[0] for p in points), min(p[1] for p in points),
                           max(p[0] for p in points) + 1, max(p[1] for p in points) + 1] if points else None)
                if stage == 'attached' and bounds:
                    origin = bounds[:2]
                expected_bounds = None
                if stage in geometry and origin:
                    x, y, width, height = geometry[stage]
                    expected_bounds = [origin[0] + x, origin[1] + y, origin[0] + x + width, origin[1] + y + height]
                passed = actual == count and (stage not in geometry or bounds == expected_bounds)
                result['cases'][stage] = {'expected': count, 'actual': actual, 'bounds': bounds,
                                          'expected_bounds': expected_bounds, 'passed': passed}
                print(stage, result['cases'][stage], flush=True)
                (output / 'stage.ack').write_text(stage)
            result['probe_exit'] = process.wait(timeout=10)
        compositor.verify()
        return 0 if result['probe_exit'] == 0 and all(c['passed'] for c in result['cases'].values()) else 1
    finally:
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        compositor.stop()
        result['cleanup'] = {'probe_stopped': process is None or process.poll() is not None,
                             'owned_compositor_stopped': compositor.process is None or compositor.process.poll() is not None}
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
