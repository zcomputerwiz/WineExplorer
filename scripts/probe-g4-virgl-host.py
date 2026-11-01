#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Bounded diskless KVM/GTK-GL startup check, NOT guest VirGL/Chromium proof.

Run inside run-isolated-graphics.py, with an explicit output directory on D:.
No existing VM, source disk, user display or networking is used.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import select
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    if not output.is_relative_to(Path('/mnt/d/VirtualBox_VMs').resolve()):
        parser.error('Host probe output must be in the authorized D: VM storage directory')
    output.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location('graphics_helpers', Path(__file__).with_name('check-g3-correctness.py'))
    helpers = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helpers)
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith(('WINE', 'VK_', 'MESA_', 'GALLIUM_'))
                   and k not in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'LD_PRELOAD', 'LD_LIBRARY_PATH')}
    helpers.configure_gl_driver(environment, 'd3d12')
    runtime = output / 'runtime'
    runtime.mkdir(mode=0o700)
    environment.update(XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(output / 'config'),
                       XDG_CACHE_HOME=str(output / 'cache'), XDG_DATA_HOME=str(output / 'data'))
    read_fd, write_fd = os.pipe()
    display = qemu = None
    result = {'classification': 'Diskless paused-host startup only; guest capabilities and rendering NOT PROVEN',
              'passed': False}
    try:
        with (output / 'xvfb.log').open('wb') as log:
            display = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0', '1024x768x24',
                                        '-nolisten', 'tcp'], pass_fds=(write_fd,), stdout=log, stderr=log)
        os.close(write_fd)
        write_fd = None
        if not select.select([read_fd], [], [], 10)[0]:
            raise TimeoutError('Owned display startup timed out')
        number = os.read(read_fd, 64).strip().decode('ascii')
        if not number.isdigit() or display.poll() is not None:
            raise RuntimeError('Owned display startup failed')
        environment['DISPLAY'] = ':' + number
        result['host_gl'] = helpers.verify_gl_driver(environment, output, 'd3d12')
        command = ['qemu-system-x86_64', '-accel', 'kvm', '-machine', 'q35', '-m', '256', '-S',
                   '-nodefaults', '-device', 'virtio-vga-gl', '-display', 'gtk,gl=on',
                   '-net', 'none', '-serial', 'none', '-monitor', 'none', '-no-reboot']
        result['command'] = command
        with (output / 'qemu.stdout.log').open('wb') as out, (output / 'qemu.stderr.log').open('wb') as err:
            qemu = subprocess.Popen(command, env=environment, stdout=out, stderr=err)
        try:
            result['early_exit'] = qemu.wait(timeout=5)
        except subprocess.TimeoutExpired:
            result['alive_after_five_seconds'] = True
            result['passed'] = True
    except Exception as error:
        result['failure'] = repr(error)
    finally:
        for process in (qemu, display):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        os.close(read_fd)
        if write_fd is not None:
            os.close(write_fd)
        result['cleanup'] = {'qemu_stopped': qemu is None or qemu.poll() is not None,
                             'display_stopped': display is None or display.poll() is not None}
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
