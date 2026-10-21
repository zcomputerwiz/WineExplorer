#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Owned headless pinned-wexcomp control, never a user's compositor/session."""
import hashlib
from pathlib import Path
import re
import subprocess
import time


class OwnedWexcomp:
    def __init__(self, binary, config, output, environment):
        self.binary, self.config, self.output = binary, config, output
        self.environment = environment
        self.process = None
        self.record = None

    def start(self):
        runtime = self.output / 'wayland-runtime'
        runtime.mkdir(mode=0o700)
        env = dict(self.environment)
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'WAYLAND_SOCKET',
                    'WLR_RENDER_DRM_DEVICE', 'WLR_DRM_DEVICES'):
            env.pop(key, None)
        env.update(XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(runtime / 'config'),
                   XDG_CONFIG_DIRS=str(self.config), WLR_BACKENDS='headless',
                   WLR_HEADLESS_OUTPUTS='1', WLR_RENDERER='pixman', WEX_SHELL_SOCKET='wex-shell-0')
        command = [str(self.binary), '-V', '-C', str(self.config)]
        with (self.output / 'wexcomp.log').open('wb') as log:
            self.process = subprocess.Popen(command, env=env, stdout=log, stderr=log, start_new_session=True)
        self.environment.pop('DISPLAY', None)
        self.environment.pop('WAYLAND_SOCKET', None)
        self.environment.update(XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY='wex-shell-0',
                                LABWC_WEX_SHELL_SOCKET='wex-shell-0', WEX_WAYLAND_NATIVE_WINDOWS='1')
        deadline = time.monotonic() + 10
        while not (runtime / 'wex-shell-0').is_socket():
            if self.process.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError('Owned wexcomp startup failed; see wexcomp.log')
            time.sleep(.1)
        with (self.output / 'wayland-info.log').open('wb') as log:
            subprocess.run(['wayland-info'], env=self.environment, stdout=log, stderr=log,
                           check=True, timeout=15)
        protocols = (self.output / 'wayland-info.log').read_text(errors='replace')
        if 'wex_window_manager_v1' not in protocols:
            raise RuntimeError('Private wexcomp endpoint lacks project protocol')
        self.record = dict(binary=str(self.binary), sha256=hashlib.sha256(self.binary.read_bytes()).hexdigest(),
                           config=str(self.config), command=command, backend='headless',
                           compositor_renderer='pixman (software)', endpoint='wex-shell-0',
                           private_protocol_verified=True)
        return self.record

    def set_output_scale(self, scale):
        if scale not in (1.0, 1.25, 1.5, 2.0):
            raise ValueError('Only explicit diagnostic scales are supported')
        if (not self.record or not self.process or self.process.poll() is not None
                or self.environment.get('WAYLAND_DISPLAY') != 'wex-shell-0'
                or self.environment.get('XDG_RUNTIME_DIR') != str(self.output / 'wayland-runtime')):
            raise RuntimeError('Scaling requires this live owned compositor/private endpoint')
        subprocess.run(['wlr-randr', '--output', 'HEADLESS-1', '--scale', str(scale)],
                       env=self.environment, check=True, timeout=10, capture_output=True, text=True)
        state = subprocess.run(['wlr-randr'], env=self.environment, check=True, timeout=10,
                               capture_output=True, text=True).stdout
        match = re.search(r'(?m)^\s*Scale:\s*([0-9.]+)\s*$', state)
        if not state.startswith('HEADLESS-1 ') or not match or float(match[1]) != scale:
            raise RuntimeError('Owned output did not report the requested scale: ' + state)
        index = len(self.record.setdefault('output_scales', []))
        (self.output / ('output-scale-%d.log' % index)).write_text(state)
        self.record['output_scales'].append({'requested': scale, 'observed': float(match[1])})

    def verify(self):
        if self.process is None or self.process.poll() is not None:
            raise RuntimeError('Owned wexcomp exited during control')
        self.record['alive_at_end'] = True

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)


def capture_command(target):
    return ['grim', str(Path(target))]
