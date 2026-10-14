# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Own a full opt-in Explorer shell in an isolated headless Wayland session."""
import os
from pathlib import Path
import re
import select
import subprocess
import tempfile
import time
from PIL import Image

root = Path(os.environ.get("WEX_ASSEMBLED_ROOT", "/srv/protonexplorer/wex-wayland-l1"))
wine, server = os.environ["WINELOADER"], os.environ["WINESERVER"]
state = Path(tempfile.mkdtemp(prefix="shell-adoption.", dir=root / "checks"))
comp = Path.home() / "wexcomp-build"
enabled = os.environ.get("WEX_SHELL_TEST_DISABLED") != "1"

def report_wait(report, marker, process):
    deadline = time.monotonic() + 20
    while True:
        content = report.read_text(errors="replace") if report.exists() else ""
        assert "FAIL " not in content, content
        if marker in content:
            return content
        assert process.poll() is None and time.monotonic() < deadline, (marker, content)
        time.sleep(.05)

for bits in ((64, 32) if enabled else (64,)):
    case = state / str(bits)
    runtime, config = case / "runtime", case / "config"
    runtime.mkdir(mode=0o700, parents=True)
    config.mkdir()
    (config / "rc.xml").write_text(
        "<labwc_config><core><decoration>server</decoration></core>"
        '<keyboard><keybind key="A-Tab"><action name="None"/></keybind></keyboard>'
        "</labwc_config>\n")
    env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY="wex-shell-0",
               WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1")
    env.pop("DISPLAY", None)
    env.pop("WEX_WAYLAND_NATIVE_WINDOWS", None)
    if enabled:
        env["WEX_WAYLAND_NATIVE_WINDOWS"] = "1"
    compositor = subprocess.Popen([str(comp / "build/labwc"), "-C", str(config)], env=env,
                                  stdout=(case / "compositor.log").open("w"), stderr=subprocess.STDOUT)
    peer = shell = probe = None
    try:
        deadline = time.monotonic() + 10
        while not (runtime / "wex-shell-0").exists():
            assert compositor.poll() is None and time.monotonic() < deadline
            time.sleep(.05)
        def start_peer():
            process = subprocess.Popen([str(comp / "tests/wex/foreign-client"), "--publish-hosting"], env=env,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
                                       stderr=(case / "peer.log").open("w"))
            assert select.select([process.stdout], [], [], 10)[0]
            assert process.stdout.readline().startswith("LEASE ")
            return process

        if bits == 64 or not enabled:
            peer = start_peer()
        explorer = str(Path(wine).parent.parent / "programs/explorer/i386-windows/explorer.exe") if bits == 32 else "explorer"
        shell = subprocess.Popen([wine, explorer, "/shell"], env=env,
                                 stdout=(case / "shell.log").open("w"), stderr=subprocess.STDOUT)
        report = case / "probe.log"
        probe = subprocess.Popen([wine, str(root / f"checks/shell-adoption{bits}.exe"),
                                  "enabled" if enabled else "disabled", "Z:" + str(report)], env=env,
                                 stdout=(case / "launch.log").open("w"), stderr=subprocess.STDOUT)
        if peer is None:
            report_wait(report, "READY_SHELL", probe)
            peer = start_peer()  # also cover windows mapped after shell startup
        if enabled:
            def command(value):
                peer.stdin.write(value + "\n")
                peer.stdin.flush()

            def capture(content, phase):
                match = re.search(rf"PIXEL_{phase} (\d+) (\d+)", content)
                assert match, content
                path = case / f"{phase.lower()}.png"
                subprocess.run(["grim", str(path)], env=env, check=True, timeout=10)
                with Image.open(path) as image:
                    pixel = image.convert("RGB").getpixel((int(match[1]), int(match[2])))
                assert pixel == (0x33, 0x88, 0x55), (phase, pixel)

            capture(report_wait(report, "READY_TITLE", probe), "INITIAL")
            command("title")
            report_wait(report, "READY_RESIZE", probe)
            command("resize")
            capture(report_wait(report, "READY_CAPTURE", probe), "RESIZED")
            Path(str(report) + ".captured").write_text("captured\n")
            content = report_wait(report, "READY_NATIVE_CLICK", probe)
            point = re.search(r"PIXEL_RESIZED (\d+) (\d+)", content)
            command(f"click {point[1]} {point[2]}")
            assert select.select([peer.stdout], [], [], 5)[0]
            click = peer.stdout.readline().strip().split()
            assert len(click) == 4 and click[:3] == ["CLICK", "1", "1"] and int(click[3]) >= 2, click
            report_wait(report, "READY_MENU_CAPTURE", probe)
            subprocess.run(["grim", str(case / "menu.png")], env=env, check=True, timeout=10)
            Path(str(report) + ".menu").write_text("menu captured\n")
            report_wait(report, "READY_MINIMIZED", probe)
            deadline = time.monotonic() + 5
            while True:
                command("state")
                assert select.select([peer.stdout], [], [], 5)[0]
                if peer.stdout.readline().strip() == "STATE 1":
                    break
                assert time.monotonic() < deadline, "native did not minimize"
                time.sleep(.05)
            Path(str(report) + ".restore").write_text("native minimized\n")
            report_wait(report, "READY_NATIVE_MINIMIZE", probe)
            command("minimize")
            report_wait(report, "READY_NATIVE_RESTORE", probe)
            command("restore")
            report_wait(report, "READY_CSD", probe)
            command("csd")
            report_wait(report, "READY_SSD", probe)
            command("ssd")
        assert probe.wait(timeout=25) == 0, report.read_text(errors="replace")
        print(report.read_text(), flush=True)
        if enabled:
            assert peer.wait(timeout=10) == 0, "native did not honor forwarded close"
    finally:
        for process in (probe, peer):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        try:
            subprocess.run([server, "-k"], env=env, timeout=45)
            subprocess.run([server, "-w"], env=env, timeout=45)
        finally:
            if shell and shell.poll() is None:
                shell.terminate()
            compositor.terminate()
            compositor.wait(timeout=10)
print(f"Shell adoption logs: {state}")
