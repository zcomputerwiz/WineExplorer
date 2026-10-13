#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Launch logonui with inert LightDM protocol pipes; never authenticates users."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import threading

root = Path('/srv/protonexplorer/wex-refactor-p3b')
assert Path(os.environ['WINEPREFIX']).is_relative_to(root / 'prefixes')
read_requests, write_requests = os.pipe()
read_replies, write_replies = os.pipe()
env = os.environ | {'LIGHTDM_TO_SERVER_FD': str(write_requests),
                    'LIGHTDM_FROM_SERVER_FD': str(read_replies)}

def server():
    header = os.read(read_requests, 8)
    if len(header) != 8:
        return
    message, size = struct.unpack('>II', header)
    assert message == 0, 'Only a connect request is allowed'
    data = bytearray()
    while len(data) < size:
        data.extend(os.read(read_requests, size - len(data)))
    version = b'refactor-fixture'
    payload = struct.pack('>I', len(version)) + version
    os.write(write_replies, struct.pack('>II', 0, len(payload)) + payload)
    # No prompts or authentication replies. Retain pipes while the UI runs.
    while os.read(read_requests, 4096):
        pass

threading.Thread(target=server, daemon=True).start()
with (root / 'checks/power/greeter.log').open('w') as log:
    proc = subprocess.Popen([os.environ['WINELOADER'], 'logonui.exe'], env=env,
                            pass_fds=(write_requests, read_replies), stdout=log,
                            stderr=subprocess.STDOUT)
    try:
        code = subprocess.call([os.environ['WINELOADER'], str(root / 'checks/power/probe.exe'), 'greeter'])
        proc.wait(timeout=15)
        sys.exit(code)
    finally:
        if proc.poll() is None:
            proc.terminate()
