#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Real-program frame checks for WAYLAND_POLISH_WORKORDER.md (installed VM)."""
import base64
import contextlib
import io
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import vmtest as vm

name, password, directory = sys.argv[1:4]
steps = Path(directory)
steps.mkdir(parents=True, exist_ok=True)


def command(text, timeout=30):
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        result = vm.serial(name, "root", password, text, timeout)
    print(output.getvalue(), end="", flush=True)
    if result:
        raise RuntimeError(f"Guest command exited {result}")
    return output.getvalue()


def stage(source, target):
    encoded = base64.b64encode(Path(source).read_bytes()).decode()
    command(f"rm -f {target}.b64")
    for offset in range(0, len(encoded), 16000):
        command(f"printf '%s' '{encoded[offset:offset + 16000]}' >> {target}.b64")
    command(f"base64 -d {target}.b64 > {target}; rm -f {target}.b64")


def native(*args, wait=False):
    flags = ["--native", "--log-file", "/tmp/wex-polish-native.log"]
    if wait:
        flags.append("--wait")
    return command("python3 /tmp/wex-session-run.py " + shlex.join(
        [*flags, "env", "LANG=C.UTF-8", *args]))


def readback(action="zorder"):
    return command("rm -f /tmp/wex-polish-state.log; python3 /tmp/wex-session-run.py "
                   "--wait --log-file /tmp/wex-polish-state.log "
                   "/tmp/wex-polish-control.exe " + action + "; cat /tmp/wex-polish-state.log")


def window(title):
    for block in re.split(r"(?m)^Z ", readback())[1:]:
        caption = block.splitlines()[0].split(" ", 4)[-1].strip()
        if title not in caption:
            continue
        frame = re.search(r"style=([0-9a-f]+) rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)", block)
        client = re.search(r"client=(-?\d+),(-?\d+) (\d+)x(\d+)", block)
        scale = re.search(r"scale120=([0-9a-f]+)", block)
        if frame and client and scale:
            if "group=0000000000000001" not in block:
                raise RuntimeError("Hosting not yet active: " + caption)
            return caption, tuple(map(int, frame.groups()[1:])), tuple(map(int, client.groups())), int(scale[1], 16)
    raise RuntimeError("No hosted " + title)


def settled(title):
    end = time.monotonic() + 10
    while time.monotonic() < end:
        try:
            return window(title)
        except RuntimeError:
            time.sleep(.2)
    raise RuntimeError("No settled " + title)


def pixels(path, width, x, y):
    return subprocess.check_output(["convert", str(path), "-crop", f"{width}x1+{x}+{y}", "rgb:-"])


def frame_check(title, label, scale):
    caption, frame, client, actual_scale = settled(title)
    assert actual_scale == round(scale * 120), (actual_scale, scale)
    # Put the real program away from screen edges before reading its borders.
    width, height = vm.screen_size(name)
    left = min(80, max(0, width - 10 - (frame[2] - frame[0])))
    top = min(80, max(0, height - 40 - (frame[3] - frame[1])))
    vm.drag(name, frame[0] + 100, frame[1] + 10, left + 100, top + 10)
    time.sleep(.5)
    caption, moved, client, actual_scale = settled(title)
    assert (moved[2] - moved[0], moved[3] - moved[1]) == (frame[2] - frame[0], frame[3] - frame[1])
    path = steps / (label + ".png")
    vm.shot(name, str(path))
    background = pixels(path, 1, 10, 700)
    y = client[1] + min(100, client[3] // 2)
    left = pixels(path, client[0] - moved[0], moved[0], y)
    right = pixels(path, moved[2] - client[0] - client[2], client[0] + client[2], y)
    assert left and right
    assert all(left[i:i+3] != background for i in range(0, len(left), 3)), (label, "missing left", left)
    assert all(right[i:i+3] != background for i in range(0, len(right), 3)), (label, "missing right", right)
    inventory = readback("inventory")
    geometry = None
    for line in inventory.splitlines():
        if "title=" + caption in line:
            match = re.search(r"logical=(-?\d+),(-?\d+),(-?\d+),(-?\d+)", line)
            if match:
                geometry = tuple(map(int, match.groups()))
    assert geometry, (caption, inventory)
    expected = (client[0], client[1], client[0]+client[2], client[1]+client[3])
    assert all(abs(round(value * scale) - target) <= 1 for value, target in zip(geometry, expected)), (geometry, expected)
    print(f"PASS {label}: frame={moved} client={client} native={geometry} borders={left.hex()}/{right.hex()}", flush=True)


stage(ROOT / "installer/tests/session-run.py", "/tmp/wex-session-run.py")
assert "WAYLAND_DISPLAY=wex-shell-0" in command("python3 /tmp/wex-session-run.py --report")
with tempfile.TemporaryDirectory(prefix="wex-polish-") as temp:
    executable = Path(temp) / "control.exe"
    subprocess.run(["x86_64-w64-mingw32-gcc", "-Os", "-s", "-Wall",
                    "-I" + str(ROOT / "src/include"), str(ROOT / "scripts/wayland-window-control.c"),
                    "-o", str(executable), "-lgdi32"], check=True)
    stage(executable, "/tmp/wex-polish-control.exe")
command("pkill -u bobsmith -x volumeicon || true")
for scale in (1, 1.5):
    native("wlr-randr", "--output", "Virtual-1", "--scale", str(scale), wait=True)
    time.sleep(1)
    for app, title, args in (
        ("foot", "Polish-foot", ["foot", "--title=Polish-foot"]),
        ("qterminal", "bobsmith@", ["qterminal", "--workdir", "/home/bobsmith"]),
        ("xterm", "Polish-xterm", ["xterm", "-title", "Polish-xterm", "-xrm", "XTerm*allowTitleOps:false"]),
    ):
        native(*args)
        frame_check(title, f"{app}-{int(scale*100)}", scale)
        command(f"pkill -u bobsmith -x {app}")
        time.sleep(.3)
    native("env", "GDK_BACKEND=x11", "volumeicon")
    time.sleep(.5)
    vm.click(name, 1200, 785, "right")
    end = time.monotonic() + 10
    while time.monotonic() < end:
        target = vm.find_text(name, "Preferences")
        if target:
            vm.click(name, *target)
            break
    else:
        raise RuntimeError("No volumeicon Preferences menu entry")
    frame_check("Preferences", f"volumeicon-{int(scale*100)}", scale)
    command("pkill -u bobsmith -x volumeicon")
    time.sleep(.3)
native("wlr-randr", "--output", "Virtual-1", "--scale", "1", wait=True)
print("PASS real-program frame matrix at 100% and 150%", flush=True)
