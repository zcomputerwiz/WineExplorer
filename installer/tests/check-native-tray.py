#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Real SNI/XEmbed acceptance in the installed Bob session (1280x800, 100%).

Usage: check-native-tray.py VM PASSWORD_FILE STEPS_DIR
Installs test-only nm-applet, volumeicon and a dummy ALSA mixer. Steam's
signed-in close/reopen check is manual. Does not start a synthetic tray client.
"""
import base64
import contextlib
import io
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import vmtest as vm

name, password, directory = sys.argv[1:4]
steps = Path(directory)
steps.mkdir(parents=True, exist_ok=True)
assert vm.screen_size(name) == (1280, 800), "Use the installed default 1280x800 display at 100%"


def command(text, timeout=30):
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        status = vm.serial(name, "root", password, text, timeout)
    print(output.getvalue(), end="", flush=True)
    if status:
        raise RuntimeError(f"Guest command exited {status}")
    return output.getvalue()


def stage(source, target):
    command(f"rm -f {target}.b64")
    data = base64.b64encode(source.read_bytes()).decode()
    for offset in range(0, len(data), 16000):
        command(f"printf '%s' '{data[offset:offset + 16000]}' >> {target}.b64")
    command(f"base64 -d {target}.b64 > {target}; rm -f {target}.b64")


def native(*args, wait=False):
    log = "/tmp/wex-tray-native.log"
    flags = ["--native", "--log-file", log]
    if wait:
        flags += ["--wait"]
    result = command("rm -f " + log + "; python3 /tmp/wex-session-run.py " +
                     shlex.join([*flags, *args]) + (" && cat " + log if wait else ""))
    return result


def shot(label):
    return vm.shot(name, str(steps / ("tray-" + label + ".png")))


def wait_text(text, seconds=15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if text.lower() in vm.ocr(name).lower():
            return
    raise RuntimeError("No visible " + text)


def click_text(text):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        position = vm.find_text(name, text)
        if position:
            vm.click(name, *position)
            return
    raise RuntimeError("No target " + text)


def pixels(path):
    return subprocess.check_output(["convert", path, "-crop", "20x20+1190+775", "rgb:-"])


stage(ROOT / "installer/tests/session-run.py", "/tmp/wex-session-run.py")
command("python3 /tmp/wex-session-run.py --report")
command("env DEBIAN_FRONTEND=noninteractive apt-get update && "
        "env DEBIAN_FRONTEND=noninteractive apt-get install -y network-manager-gnome volumeicon-alsa alsa-utils", 300)
command("modprobe snd-dummy && amixer -c Dummy sset Master 75% unmute")
native("env", "GDK_BACKEND=x11", "volumeicon")
time.sleep(1)
vm.move(name, 10, 10)
vm.move(name, 1200, 785)
deadline = time.monotonic() + 15
while time.monotonic() < deadline:
    path = shot("volume-tooltip")
    image = subprocess.check_output(["convert", path, "-crop", "220x80+1060+700", "-resize", "400%", "png:-"])
    text = subprocess.run(["tesseract", "stdin", "stdout", "--psm", "6"], input=image,
                          capture_output=True).stdout.decode().lower()
    # The small XP font's i/l glyphs are ambiguous to OCR; retain the screenshot.
    if "volumelcon" in text.replace("i", "l"):
        break
else:
    raise RuntimeError("No volumeicon tooltip")
vm.move(name, 10, 10)
before = shot("volume-before")
vm.click(name, 1200, 785)
deadline = time.monotonic() + 10
while time.monotonic() < deadline:
    vm.move(name, 10, 10)
    after = shot("volume-muted")
    if pixels(before) != pixels(after):
        break
else:
    raise RuntimeError("No XEmbed Damage update after mute")
muted = command("amixer -c Dummy sget Master")
muted_levels = [int(value) for value in re.findall(r"\[(\d+)%\]", muted)]
vm.click(name, 1200, 785, "wheel-up")
time.sleep(.3)
mixer = command("amixer -c Dummy sget Master")
levels = [int(value) for value in re.findall(r"\[(\d+)%\]", mixer)]
# volumeicon normalizes ALSA's dB range; its five-percent step need not be
# five percent of the raw dummy mixer range. Assert the real direction.
assert levels and len(levels) == len(muted_levels) and all(
    after > before for before, after in zip(muted_levels, levels)), mixer
vm.click(name, 1200, 785, "right")
wait_text("Open Mixer")
shot("volume-menu")
click_text("Preferences")
wait_text("Preferences")
shot("volume-preferences")
vm.keys(name, ["alt-f4"])
command("pkill -u bobsmith -x volumeicon; true")
command("! pgrep -u bobsmith -x volumeicon")
shot("volume-removed")
print("PASS XEmbed icon, tooltip, click, Damage, scroll, menu, command and exit", flush=True)

native("nm-applet", "--indicator")
deadline = time.monotonic() + 15
while time.monotonic() < deadline:
    items = native("busctl", "--user", "get-property", "org.kde.StatusNotifierWatcher",
                   "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", "RegisteredStatusNotifierItems", wait=True)
    if "nm_applet" in items:
        break
else:
    raise RuntimeError("No SNI registration")
time.sleep(.3)
vm.move(name, 10, 10)
vm.move(name, 1200, 785)
time.sleep(1)
shot("nm-tooltip")
vm.click(name, 1200, 785)
wait_text("Disconnect")
shot("nm-left-menu")
vm.keys(name, ["esc"])
vm.click(name, 1200, 785, "right")
wait_text("Disconnect")
shot("nm-menu")
position = vm.find_text(name, "VPN Connections")
assert position, "No VPN submenu"
vm.move(name, *position)
time.sleep(.5)
shot("nm-submenu")
vm.keys(name, ["esc"])
vm.keys(name, ["esc"])
vm.click(name, 1200, 785, "right")
wait_text("Disconnect")
click_text("Connection Information")
wait_text("Connection Information")
shot("nm-information")
vm.keys(name, ["alt-f4"])
vm.move(name, 10, 10)
before = shot("nm-connected")
try:
    command("busctl call org.freedesktop.NetworkManager /org/freedesktop/NetworkManager "
            "org.freedesktop.NetworkManager Enable b false")
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if pixels(before) != pixels(shot("nm-disabled")):
            break
    else:
        raise RuntimeError("No SNI icon update after NetworkManager signal")
finally:
    command("busctl call org.freedesktop.NetworkManager /org/freedesktop/NetworkManager "
            "org.freedesktop.NetworkManager Enable b true")
command("pkill -u bobsmith -x nm-applet; true")
command("! pgrep -u bobsmith -x nm-applet")
items = native("busctl", "--user", "get-property", "org.kde.StatusNotifierWatcher",
               "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", "RegisteredStatusNotifierItems", wait=True)
assert "nm_applet" not in items, items
shot("nm-removed")
print("PASS SNI registration, tooltip, Win32 menus, lazy submenu, command, signals and exit", flush=True)
print("MANUAL: signed-in Steam close-to-tray/reopen/menu (user requested)", flush=True)
