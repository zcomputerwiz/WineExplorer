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


def window(title, detail=False):
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
            result = caption, tuple(map(int, frame.groups()[1:])), tuple(map(int, client.groups())), int(scale[1], 16)
            return (*result, int(frame[1], 16), block) if detail else result
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
def check_frames():
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


def check_qt_dialog():
    def buttons(client):
        # Qt's green/red standard icons make full-screen OCR omit the whole
        # button row. Read just the real label areas, excluding those icons.
        current = steps / "qt-dialog-current.png"
        vm.shot(name, str(current))
        words = []
        for label, fraction, width in (("yes", .32, 54), ("no", .75, 40)):
            x, y = client[0] + round(client[2] * fraction), client[1] + client[3] - 52
            crop = steps / ("qt-" + label + "-label.png")
            subprocess.run(["convert", str(current), "-crop", f"{width}x35+{x}+{y}",
                            "+repage", "-resize", "300%", str(crop)], check=True)
            words.append(subprocess.run(["tesseract", str(crop), "stdout", "--psm", "7"],
                                        capture_output=True, text=True, check=True).stdout.strip())
        print("QT_LABELS " + repr(words), flush=True)
        return words[0].lower() == "yes" and words[1].lower() == "no"

    native("wlr-randr", "--output", "Virtual-1", "--scale", "1.5", wait=True)
    native("foot", "--title=Polish-switch-target")
    settled("Polish-switch-target")
    native("qterminal", "--workdir", "/home/bobsmith")
    _, frame, _, _ = settled("bobsmith@")
    vm.click(name, frame[2] - 17, frame[1] + 15)
    _, _, client, _ = settled("Exit QTerminal")
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if buttons(client):
            break
    else:
        vm.shot(name, str(steps / "qt-dialog-initial-failed.png"))
        raise RuntimeError("QTerminal Exit dialog did not initially draw its buttons")
    readback()
    vm.shot(name, str(steps / "qt-dialog-before-switch.png"))
    vm.click(name, 200, 785)
    time.sleep(.3)
    vm.click(name, 365, 785)
    time.sleep(.5)
    started = time.monotonic()
    deadline = started + 5
    found = False
    while time.monotonic() < deadline:
        if buttons(client):
            found = time.monotonic() <= deadline
            break
    elapsed = time.monotonic() - started
    vm.shot(name, str(steps / "qt-dialog-after-switch.png"))
    readback()
    print(f"QT_BUTTONS visible={int(found)} elapsed={elapsed:.3f}s limit=5s", flush=True)
    assert found, "QTerminal Yes/No buttons did not redraw within five seconds"
    vm.click(name, client[0] + round(client[2] * .32) + 20, client[1] + client[3] - 35)
    command("for i in $(seq 1 30); do pgrep -u bobsmith -x qterminal >/dev/null || break; sleep .1; done; ! pgrep -u bobsmith -x qterminal")
    vm.shot(name, str(steps / "qt-dialog-closed.png"))
    command("pkill -u bobsmith -x foot")
    native("wlr-randr", "--output", "Virtual-1", "--scale", "1", wait=True)
    print("PASS QTerminal Exit dialog taskbar switch at 150%", flush=True)


def check_live_scale():
    def geometry(entry, scale):
        work = readback("workarea")
        assert f"WORK 0 0 {width} {height - 30}" in work, work
        inventory = readback("inventory")
        native_rect = next(re.search(r"logical=(-?\d+),(-?\d+),(-?\d+),(-?\d+)", line)
                           for line in inventory.splitlines() if "title=Polish-live-scale" in line)
        assert native_rect, inventory
        client = entry[2]
        expected = client[0], client[1], client[0] + client[2], client[1] + client[3]
        assert all(abs(round(int(value) * scale) - target) <= 1
                   for value, target in zip(native_rect.groups(), expected)), (native_rect.groups(), expected)
        tray = re.search(r"role=0000000000000003.*?rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)",
                         readback(), re.S)
        assert tray and tuple(map(int, tray.groups())) == (0, height - 30, width, height), tray

    def state(scale, maximized):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                entry = window("Polish-live-scale", detail=True)
                if entry[3] == round(scale*120) and bool(entry[4] & 0x01000000) == maximized:
                    assert "foreground=1" in entry[5] and "focus=0000000000000001" in entry[5], entry
                    return entry
            except RuntimeError:
                pass
            time.sleep(.2)
        vm.shot(name, str(steps / "scale-failed.png"))
        raise RuntimeError(f"Foot lost hosting/maximize/focus at scale {scale}")

    native("wlr-randr", "--output", "Virtual-1", "--scale", "1", wait=True)
    native("foot", "--title=Polish-live-scale")
    _, frame, _, _ = settled("Polish-live-scale")
    vm.click(name, frame[0] + 100, frame[1] + 15, count=2)
    time.sleep(3)  # Observe the settled transaction, including its two-second timeout.
    width, height = vm.screen_size(name)
    state_start = False
    for index, scale in enumerate((1, 1.5, 1)):
        if scale != 1 or state_start:
            native("wlr-randr", "--output", "Virtual-1", "--scale", str(scale), wait=True)
            time.sleep(3)
        entry = state(scale, True)
        assert entry[1] == (-4, -4, width + 4, height - 26), entry
        geometry(entry, scale)
        vm.type_text(name, "echo scale-checked")
        vm.keys(name, ["ret"])
        vm.shot(name, str(steps / f"scale-maximized-{index}-{int(scale*100)}.png"))
        assert "scale-checked" in vm.ocr(name), "Foot did not receive keyboard input"
        print(f"PASS maximized scale={scale} frame={entry[1]} client={entry[2]}", flush=True)
        state_start = True
    vm.click(name, 200, 12, count=2)
    time.sleep(3)
    for index, scale in enumerate((1, 1.5, 1)):
        native("wlr-randr", "--output", "Virtual-1", "--scale", str(scale), wait=True)
        time.sleep(3)
        entry = state(scale, False)
        assert 0 <= entry[1][0] < entry[1][2] <= width and 0 <= entry[1][1] < entry[1][3] <= height - 30, entry
        geometry(entry, scale)
        vm.shot(name, str(steps / f"scale-restored-{index}-{int(scale*100)}.png"))
        print(f"PASS restored scale={scale} frame={entry[1]} client={entry[2]}", flush=True)
    command("pkill -u bobsmith -x foot")
    print("PASS live 100%-150%-100% scale, maximized/restored foot and taskbar", flush=True)


mode = sys.argv[4] if len(sys.argv) > 4 else "frames"
{"frames": check_frames, "qt-dialog": check_qt_dialog, "scale": check_live_scale}[mode]()
