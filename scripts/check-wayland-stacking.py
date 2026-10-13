# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise Win32 z-order across three Wine processes; requires grim and Pillow.

Run in the opt-in session environment with its WINEPREFIX, WINESERVER,
XDG_RUNTIME_DIR and WAYLAND_DISPLAY=wex-shell-0. Pass Wine, the compiled
wayland-stacking-probe.exe, and an output directory. No shipping prefix is used.
"""
import pathlib
import re
import subprocess
import sys
import time
from PIL import Image

wine, probe, output = sys.argv[1:]
output = pathlib.Path(output)
output.mkdir(parents=True, exist_ok=True)
clients = []
foreground = None
def action(name, after):
    global foreground
    result = subprocess.run([wine, probe, 'set', name, after], text=True,
                            capture_output=True, timeout=10, check=True)
    print(result.stdout.strip(), flush=True)
    if after != 'close':
        match = re.search(r'foreground=(\S+) atom=(\S+)', result.stdout)
        assert match, result.stdout
        assert int(match[2], 16), 'window has no shared handle atom'
        if foreground is None: foreground = match[1]
        assert foreground == match[1], 'SWP_NOACTIVATE changed foreground'
    time.sleep(.2)
def pixel(name, color):
    path = output / ('win32-stack-' + name + '.png')
    subprocess.run(['grim', str(path)], check=True, timeout=5)
    with Image.open(path) as image:
        actual = image.convert('RGB').getpixel((200, 200))
    assert actual == color, (name, actual, color)
    print('PASS', name, flush=True)
try:
    for name, color in [('StackA', 'FF0000'), ('StackB', '0000FF'), ('StackC', '00FF00')]:
        clients.append((name, subprocess.Popen([wine, probe, 'create', name, color])))
        time.sleep(1)
    pixel('initial', (0, 255, 0))
    action('StackA', 'top'); pixel('above-foreign', (255, 0, 0))
    action('StackA', 'StackB'); pixel('below-foreign', (0, 255, 0))
    action('StackB', 'top'); pixel('second-process-top', (0, 0, 255))
    action('StackC', 'StackA'); pixel('third-process-below', (0, 0, 255))
    action('StackA', 'top'); pixel('raise-again', (255, 0, 0))
    action('StackA', 'bottom'); pixel('bottom', (0, 0, 255))
    action('StackC', 'topmost'); pixel('topmost', (0, 255, 0))
    action('StackA', 'top'); pixel('normal-under-topmost', (0, 255, 0))
    action('StackC', 'normal')
    action('StackB', 'top'); pixel('return-to-normal', (0, 0, 255))
    action('StackB', 'close'); clients[1][1].wait(timeout=10)
    action('StackA', 'top'); pixel('sibling-destroyed', (255, 0, 0))
    print('WIN32_SHARED_STACKING_PASSED', flush=True)
finally:
    for name, p in clients:
        if p.poll() is None:
            subprocess.run([wine, probe, 'set', name, 'close'], timeout=10)
            try: p.wait(timeout=10)
            except subprocess.TimeoutExpired: p.terminate(); p.wait(timeout=5)
