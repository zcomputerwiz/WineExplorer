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


def settled(title, seconds=10):
    end = time.monotonic() + seconds
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
    # Match the tray runner: install only real test programs on the test VM.
    # Qt's Wayland plugin is optional when apt recommendations are disabled.
    command("DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "
            "qterminal qt6-wayland xterm wlr-randr </dev/null", 180)
    command("pkill -u bobsmith -x volumeicon || true")
    for scale in (1, 1.5):
        native("wlr-randr", "--output", "Virtual-1", "--scale", str(scale), wait=True)
        time.sleep(1)
        for app, title, args in (
            ("foot", "Polish-foot", ["foot", "--title=Polish-foot"]),
            ("qterminal", "bobsmith@", ["env", "QT_QPA_PLATFORM=wayland", "qterminal", "--workdir", "/home/bobsmith"]),
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
    native("env", "QT_QPA_PLATFORM=wayland", "qterminal", "--workdir", "/home/bobsmith")
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
        marker = ("scale-alpha-checked", "scale-bravo-checked", "scale-charlie-checked")[index]
        vm.type_text(name, "echo " + marker)
        vm.keys(name, ["ret"])
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if marker.replace("-", "") in re.sub("[^a-z0-9]", "", vm.ocr(name).lower()):
                break
        else:
            raise RuntimeError("Foot did not receive fresh keyboard input: " + marker)
        vm.shot(name, str(steps / f"scale-maximized-{index}-{int(scale*100)}.png"))
        print(f"PASS maximized scale={scale} frame={entry[1]} client={entry[2]}", flush=True)
        state_start = True
    vm.click(name, 200, 12, count=2)
    time.sleep(3)
    restored = None
    for index, scale in enumerate((1, 1.5, 1)):
        native("wlr-randr", "--output", "Virtual-1", "--scale", str(scale), wait=True)
        time.sleep(3)
        entry = state(scale, False)
        assert 0 <= entry[1][0] < entry[1][2] <= width and 0 <= entry[1][1] < entry[1][3] <= height - 30, entry
        bounds = entry[1]
        if restored:
            assert abs(bounds[0] - restored[0]) <= 2 and abs(bounds[1] - restored[1]) <= 2, (restored, bounds)
            # Foot may snap by one character cell as fractional-scale font
            # metrics change. A coordinate-space resize is much larger.
            assert abs((bounds[2]-bounds[0]) - (restored[2]-restored[0])) <= 24, (restored, bounds)
            assert abs((bounds[3]-bounds[1]) - (restored[3]-restored[1])) <= 24, (restored, bounds)
        else:
            restored = bounds
        geometry(entry, scale)
        vm.shot(name, str(steps / f"scale-restored-{index}-{int(scale*100)}.png"))
        print(f"PASS restored scale={scale} frame={entry[1]} client={entry[2]}", flush=True)
    command("pkill -u bobsmith -x foot")
    print("PASS live 100%-150%-100% scale, maximized/restored foot and taskbar", flush=True)


def check_programs():
    def text(expected, seconds=20, region=None):
        end = time.monotonic() + seconds
        normalize = lambda value: re.sub("[^a-z0-9]", "", value.lower())
        vm.move(name, 10, 10)  # Do not OCR through the native I-beam cursor.
        while time.monotonic() < end:
            if region:
                x, y, width, height = region
                current, crop = steps / "program-text.png", steps / "terminal-text.png"
                vm.shot(name, str(current))
                subprocess.run(["convert", str(current), "-crop", f"{width}x{height}+{x}+{y}",
                                "+repage", "-resize", "300%", "-negate", str(crop)], check=True)
                words = subprocess.run(["tesseract", str(crop), "stdout", "--psm", "6"],
                                       capture_output=True, text=True, check=True).stdout
            else:
                words = vm.ocr(name)
            print("PROGRAM_TEXT " + repr(words), flush=True)
            if normalize(expected) in normalize(words):
                return
        raise RuntimeError("No visible " + expected)

    def toggle(title, minimized):
        vm.click(name, 200, 785)
        end = time.monotonic() + 10
        while time.monotonic() < end:
            entry = window(title, detail=True)
            if bool(entry[4] & 0x20000000) == minimized:
                vm.shot(name, str(steps / (label + ("-minimized.png" if minimized else "-restored.png"))))
                return
            time.sleep(.2)
        raise RuntimeError("Taskbar state did not change: " + title)

    selected = set(sys.argv[5:])
    for label, title, args, expected in (
        ("gtk", "gedit", ["gedit", "--standalone"], "PolishGTKchecked"),
        ("qt", "bobsmith@", ["env", "QT_QPA_PLATFORM=wayland", "qterminal", "--workdir", "/home/bobsmith"], "native terminal input"),
        ("xterm", "Polish-Xterm", ["xterm", "-title", "Polish-Xterm", "-xrm", "XTerm*allowTitleOps:false"], "native xterm input"),
        ("firefox", "Troubleshooting", ["firefox-esr", "--no-remote", "about:support"], "Application Basics"),
    ):
        if selected and label not in selected:
            continue
        native(*args)
        caption, frame, client, _ = settled(title, 60)
        assert readback().count(caption) == 1, "Duplicate host: " + caption
        vm.click(name, client[0] + 80, client[1] + (130 if label == "gtk" else 80))
        if label == "firefox":
            vm.keys(name, ["ctrl-l"])
            vm.type_text(name, "about:support")
        else:
            vm.type_text(name, expected if label == "gtk" else "echo " + expected)
        if label != "gtk":
            vm.keys(name, ["ret"])
        text(expected, region=(client[0] + 2, client[1] + (52 if label == "qt" else 2),
                               client[2] - 20, 80) if label in ("qt", "xterm") else None)
        vm.shot(name, str(steps / (label + "-input.png")))
        toggle(title, True)
        toggle(title, False)
        command("pkill -u bobsmith -x " + {"gtk": "gedit", "qt": "qterminal", "xterm": "xterm", "firefox": "firefox-esr"}[label])
        time.sleep(.5)
        print("PASS fresh " + label + " input, one proxy, taskbar minimize/restore", flush=True)

    for executable, title, label in (("winecfg.exe", "Wine configuration", "winecfg"),
                                      ("control.exe", "Control Panel", "control-panel")):
        if selected and label not in selected:
            continue
        command("python3 /tmp/wex-session-run.py " + executable)
        end = time.monotonic() + 30
        while time.monotonic() < end:
            if title.lower() in vm.ocr(name).lower():
                break
        else:
            raise RuntimeError("No " + title)
        vm.shot(name, str(steps / (label + ".png")))
        vm.keys(name, ["alt-f4"])
        time.sleep(.5)
        print("PASS fresh " + label + " opens and closes", flush=True)


def check_steam():
    native("/usr/games/steam")
    deadline = time.monotonic() + 300
    installed = False
    while time.monotonic() < deadline:
        screen = vm.ocr(name)
        if "sign in" in screen.lower():
            break
        if not installed and "steam" in screen.lower():
            button = vm.find_text(name, "Install")
            if button:
                vm.click(name, *button)
                installed = True
        time.sleep(1)
    else:
        raise RuntimeError("Steam did not reach its sign-in window")
    caption, frame, _, _ = settled("Sign in to Steam", 30)
    assert readback().count(caption) == 1, "Duplicate Steam proxy"
    vm.click(name, frame[0] + 150, frame[1] + 140)
    vm.type_text(name, "PolishSteamchecked")  # Dummy text only; never submit sign-in.
    vm.move(name, 10, 10)
    time.sleep(1)
    assert "polishsteamchecked" in vm.ocr(name).lower()
    vm.shot(name, str(steps / "steam-input.png"))
    vm.drag(name, frame[0] + 250, frame[1] + 20, frame[0] + 330, frame[1] + 70)
    _, moved, _, _ = settled("Sign in to Steam")
    assert moved[0] > frame[0] + 40 and moved[1] > frame[1] + 25, moved
    for minimized in (True, False):
        vm.click(name, 200, 785)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            entry = window("Sign in to Steam", detail=True)
            if bool(entry[4] & 0x20000000) == minimized:
                break
            time.sleep(.2)
        else:
            raise RuntimeError("Steam taskbar minimize/restore failed")
        vm.shot(name, str(steps / ("steam-minimized.png" if minimized else "steam-restored.png")))
    command("pkill -u bobsmith -x steam || true")
    print("PASS normal Steam launch, dummy input, move, one proxy, taskbar minimize/restore; no sign-in submitted", flush=True)


def check_installer():
    # Reuse the previously verified, ignored 32-bit acceptance download.
    digest = "39bf65045153fc26c42fa9fe47afd8cd1b11671c7f11d031e738c9a13960c009"
    output = subprocess.run(["i686-w64-mingw32-objdump", "-f", str(ROOT / "build/wayland-session/7z2604.exe")],
                            capture_output=True, text=True, check=True).stdout
    print(output, flush=True)
    assert "pei-i386" in output, output
    command("curl -fsS http://10.0.2.2:18765/7z2604.exe -o /tmp/polish-7z.exe && "
            f"echo '{digest}  /tmp/polish-7z.exe' | sha256sum -c -")
    command("python3 /tmp/wex-session-run.py /tmp/polish-7z.exe")

    def visible(expected):
        end = time.monotonic() + 30
        while time.monotonic() < end:
            if expected.lower() in vm.ocr(name).lower():
                return
        raise RuntimeError("Installer did not show " + expected)

    visible("Destination")
    vm.shot(name, str(steps / "installer32-start.png"))
    vm.click(name, 600, 352)
    vm.keys(name, ["ctrl-a"])
    vm.type_text(name, r"C:\users\bobsmith\7zip-test")
    vm.move(name, 10, 10)
    visible("7zip-test")
    vm.shot(name, str(steps / "installer32-destination.png"))
    button = vm.find_text(name, "Install")
    assert button, "No installer Install button"
    vm.click(name, *button)
    visible("installed")
    command("test -f /home/bobsmith/.wine/drive_c/users/bobsmith/7zip-test/7zFM.exe")
    vm.shot(name, str(steps / "installer32-complete.png"))
    vm.keys(name, ["ret"])
    command("python3 /tmp/wex-session-run.py C:/users/bobsmith/7zip-test/7zFM.exe")
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if re.search(r"(?m)^Z .+ 7-Zip\r?$", readback()):
            break
    else:
        raise RuntimeError("Installed 7-Zip File Manager did not open")
    vm.shot(name, str(steps / "installer32-file-manager.png"))
    vm.keys(name, ["alt-f4"])
    print("PASS verified PE i386 7-Zip GUI install to Bob's private prefix and installed File Manager launch", flush=True)


mode = sys.argv[4] if len(sys.argv) > 4 else "frames"
{"frames": check_frames, "qt-dialog": check_qt_dialog, "scale": check_live_scale,
 "programs": check_programs, "steam": check_steam, "installer32": check_installer}[mode]()
