#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Bounded owned-endpoint EGL inventory, no Wine or installed-display changes."""
import argparse
import json
import os
from pathlib import Path

from graphics_wexcomp import OwnedWexcomp, verify_native_gl_driver


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wexcomp-bin', required=True, type=Path)
    parser.add_argument('--wexcomp-config', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--compositor-renderer', choices=('pixman', 'gles2'), default='pixman')
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith(('WINE', 'VK_', 'WLR_', 'MESA_', 'GALLIUM_', 'EGL_'))
                   and k not in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'LD_PRELOAD', 'LD_LIBRARY_PATH')}
    environment.update(GALLIUM_DRIVER='d3d12', LIBGL_ALWAYS_SOFTWARE='0', LD_LIBRARY_PATH='/usr/lib/wsl/lib')
    control = OwnedWexcomp(args.wexcomp_bin.resolve(strict=True),
                           args.wexcomp_config.resolve(strict=True), output, environment, args.compositor_renderer)
    result = {'classification': 'Native EGL inventory only; not Chromium/desktop acceptance', 'passed': False}
    try:
        result['wexcomp'] = control.start()
        result['native_gl_driver'] = verify_native_gl_driver(environment, output, 'd3d12')
        control.verify()
        result['passed'] = True
    except Exception as error:
        result['failure'] = repr(error)
    finally:
        control.stop()
        result['cleanup'] = {'owned_compositor_stopped': control.process is None or control.process.poll() is not None}
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
