#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Real installed-session actions; no synthetic Wayland clients.

Run after check-wow64.sh, with Bob logged into the Welcome screen's Wayland
choice. Detailed browser/GTK/Qt/Steam acceptance is recorded separately.
"""
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

name, password, steps = sys.argv[1:4]
steps = Path(steps)
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
    command(f"rm -f {target}.b64")
    data = base64.b64encode(Path(source).read_bytes()).decode()
    for offset in range(0, len(data), 16000):
        command(f"printf '%s' '{data[offset:offset + 16000]}' >> {target}.b64")
    command(f"base64 -d {target}.b64 > {target}; rm -f {target}.b64")


def launch(*args):
    command("python3 /tmp/wex-session-run.py " + shlex.join(args))


def windows():
    output = command("rm -f /tmp/wex-session-state.log; python3 /tmp/wex-session-run.py "
                     "--wait --log-file /tmp/wex-session-state.log "
                     "/tmp/wex-window-control.exe zorder; cat /tmp/wex-session-state.log")
    capture = re.search(r"(?m)^POINTER .* capture=([0-9a-f]+)", output)
    if capture and int(capture[1], 16):
        return []  # A preview is visible, but the previous gesture has not exited.
    result = []
    for block in re.split(r"(?m)^Z ", output)[1:]:
        title = block.splitlines()[0].split(" ", 4)[-1].strip()
        rect = re.search(r"style=([0-9a-f]+) rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)", block)
        if rect:
            result.append((title, int(rect[1], 16), tuple(map(int, rect.groups()[1:]))))
    return result


def window(title):
    for entry in windows():
        if title in entry[0]:
            return entry
    raise RuntimeError(f"No visible window containing {title!r}")


def settled(title, predicate=lambda item: True, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            entry = window(title)
            if predicate(entry):
                return entry
        except RuntimeError:
            pass
        time.sleep(.25)
    raise RuntimeError(f"Window {title!r} did not reach the expected state")


def capture(label):
    vm.shot(name, str(steps / (label + ".png")))


def closed(title):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if not any(title in item[0] for item in windows()):
            return
        time.sleep(.25)
    raise RuntimeError(f"Window {title!r} did not close")


def text(expected):
    # Terminal glyphs are small; OCR can insert spaces around punctuation.
    normalize = lambda value: re.sub(r"[^a-z0-9]", "", value.lower())
    deadline = time.monotonic() + 15
    vm.move(name, 10, 10)
    while time.monotonic() < deadline:
        if normalize(expected) in normalize(vm.ocr(name)):
            return
    raise RuntimeError(f"No visible text {expected!r}")


try:
    stage(ROOT / "installer/tests/session-run.py", "/tmp/wex-session-run.py")
    report = command("python3 /tmp/wex-session-run.py --report")
    assert "WAYLAND_DISPLAY=wex-shell-0" in report, report
    assert "\nDISPLAY=\n" in report, report
    with tempfile.TemporaryDirectory(prefix="wex-real-session-") as directory:
        executable = Path(directory) / "window-control.exe"
        subprocess.run(["x86_64-w64-mingw32-gcc", "-Os", "-s", "-Wall",
                        "-I" + str(ROOT / "src/include"),
                        str(ROOT / "scripts/wayland-window-control.c"),
                        "-o", str(executable), "-lgdi32"], check=True)
        stage(executable, "/tmp/wex-window-control.exe")
    command("DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends foot </dev/null", 120)
    command("runuser -u bobsmith -- env -i PATH=/usr/local/bin:/usr/bin:/bin "
            "HOME=/home/bobsmith USER=bobsmith XDG_RUNTIME_DIR=/run/user/1001 "
            "WAYLAND_DISPLAY=wayland-0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1001/bus "
            "LANG=C.UTF-8 foot --working-directory=/home/bobsmith "
            "--title=Wex-session-foot </dev/null >/tmp/wex-foot-check.log 2>&1 & true")
    _, _, original = settled("Wex-session-foot")
    left, top, right, bottom = original
    vm.click(name, left + 50, top + 60)
    vm.type_text(name, "echo Wayland-input-checked")
    vm.keys(name, ["ret"])
    text("Wayland-input-checked")
    capture("wayland-foot-input")
    vm.drag(name, left + 100, top + 15, left + 145, top + 50)
    _, _, moved = settled("Wex-session-foot", lambda item: item[2][0] > left + 20)
    left, top, right, bottom = moved
    vm.drag(name, right - 2, top + 100, right - 62, top + 100)
    _, _, edge = settled("Wex-session-foot", lambda item: item[2][2] < right - 30)
    left, top, right, bottom = edge
    vm.drag(name, right - 2, bottom - 2, right + 58, bottom + 38)
    _, _, corner = settled("Wex-session-foot", lambda item: item[2][2] > right + 25 and item[2][3] > bottom + 15)
    capture("wayland-foot-resized")
    left, top, right, bottom = corner
    vm.click(name, left + 100, top + 15, count=2)
    _, _, maximized = settled("Wex-session-foot", lambda item: bool(item[1] & 0x01000000))
    width, height = vm.screen_size(name)
    assert maximized == (-4, -4, width + 4, height - 26), maximized
    capture("wayland-foot-maximized")
    vm.click(name, 200, 12, count=2)
    settled("Wex-session-foot", lambda item: item[2] == corner)
    vm.click(name, corner[2] - 52, corner[1] + 15)
    settled("Wex-session-foot", lambda item: bool(item[1] & 0x20000000))
    vm.click(name, 200, height - 15)
    settled("Wex-session-foot", lambda item: not (item[1] & 0x20000000))
    capture("wayland-foot-taskbar-restored")
    command("printf 'Notepad session check' > /tmp/wex-session-notepad.txt")
    launch("C:/windows/system32/notepad.exe", "Z:/tmp/wex-session-notepad.txt")
    _, _, note = settled("wex-session-notepad")
    vm.click(name, note[0] + 70, note[1] + 90)
    vm.keys(name, ["ctrl-a"])
    vm.type_text(name, "Win64-Wayland-checked")
    text("Win64-Wayland-checked")
    capture("wayland-notepad-input")
    vm.keys(name, ["alt-tab"])
    vm.type_text(name, "echo AltTab-checked")
    vm.keys(name, ["ret"])
    text("AltTab-checked")
    capture("wayland-native-alttab")
    vm.type_text(name, "exit")
    vm.keys(name, ["ret"])
    closed("Wex-session-foot")
    settled("wex-session-notepad")
    vm.keys(name, ["alt-f4"])
    text("save")
    vm.keys(name, ["alt-n"])
    closed("wex-session-notepad")
    print("PASS: real Wayland foot input/move/edge/corner/maximize/restore/taskbar, Notepad input, native Alt+Tab, close")
except Exception:
    capture("wayland-actions-failed")
    raise
