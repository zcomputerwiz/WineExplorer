# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Run after configuring an isolated prefix for Wayland; own headless compositor."""
import os
import re
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import time

root = Path(os.environ.get("WEX_ASSEMBLED_ROOT", "/srv/protonexplorer/wex-wayland-l1"))
state = Path(tempfile.mkdtemp(prefix="association.", dir=root / "checks"))
runtime = state / "runtime"
runtime.mkdir(mode=0o700)
config = state / "config"
config.mkdir()
(config / "rc.xml").write_text("<labwc_config><core><decoration>server</decoration></core></labwc_config>\n", encoding="utf-8")
env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY="wex-shell-0",
           WLR_BACKENDS="headless", WLR_RENDERER="pixman", WLR_HEADLESS_OUTPUTS="1")
env.pop("DISPLAY", None)
compositor_dir = Path.home() / "wexcomp-build"
wine = Path(os.environ.get("WINELOADER", str(root / "wow64-source/build/wine64/loader/wine")))
server = Path(os.environ.get("WINESERVER", str(root / "wow64-source/build/wine64/server/wineserver")))
env.update(WINELOADER=str(wine), WINESERVER=str(server), WINEDEBUG="-all",
           WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d", GIO_USE_VFS="local")
assert env["WINEPREFIX"] != str(Path.home() / ".wine")
compositor_binary = os.environ.get("WEX_WEXCOMP_BIN", str(compositor_dir / "build/labwc"))
compositor = subprocess.Popen([compositor_binary, "-C", str(config)], env=env,
                             stdout=(state / "compositor.log").open("w"), stderr=subprocess.STDOUT)
peer = probe = external_app = None
native_app = os.environ.get("WEX_ASSOCIATION_NATIVE_APP", "")
assert native_app in ("", "gtk", "foot")

def stop_external():
    global external_app
    if external_app is None:
        return
    if external_app.poll() is None:
        os.killpg(external_app.pid, signal.SIGTERM)
        try:
            external_app.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(external_app.pid, signal.SIGKILL)
            external_app.wait()
    external_app = None
binding = os.environ.get("WEX_ASSOCIATION_BRIDGE") == "1"
states = os.environ.get("WEX_ASSOCIATION_STATES") == "1"
assert not (states and binding)
hosting = binding or os.environ.get("WEX_ASSOCIATION_HOSTING") == "1"
try:
    deadline = time.monotonic() + 10
    while not (runtime / "wex-shell-0").exists():
        assert compositor.poll() is None and time.monotonic() < deadline
        time.sleep(0.1)
    for bits in (64, 32):
        peer_args = [str(compositor_dir / "tests/wex/foreign-client")]
        if native_app:
            native_env = dict(env, WAYLAND_DISPLAY="wayland-0", GDK_BACKEND="wayland", GTK_CSD="0")
            command = ([sys.executable, str(Path(__file__).with_name("wayland-gtk-resize-client.py"))]
                       if native_app == "gtk" else ["foot", "--title=Wex real native client",
                           "--app-id=wex-real", "-w240x180", "-o", "colors.background=338855",
                           "/usr/bin/sleep", "120"])
            external_app = subprocess.Popen(command, env=native_env, start_new_session=True,
                                            stdout=(state / f"external{bits}.log").open("w"),
                                            stderr=subprocess.STDOUT)
            peer_args += ["--publish-external", "Wex real native client"]
        else:
            peer_args += ["--publish-hosting" if hosting or states else "--publish-association"]
        peer = subprocess.Popen(peer_args,
                                env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=(state / f"peer{bits}.log").open("w"), text=True)
        assert select.select([peer.stdout], [], [], 10)[0], "native peer readiness timed out"
        values = peer.stdout.readline().strip().split()
        assert len(values) == 3 and values[0] == "LEASE", values
        if not native_app and (os.environ.get("WEX_ASSOCIATION_RESIZE") == "1" or os.environ.get("WEX_ASSOCIATION_RESIZE_CONTROLLER") == "1"):
            assert binding, "resize transport requires the real bridge probe"
            fixture = os.environ.get("WEX_ASSOCIATION_RESIZE_FIXTURE", "")
            hints = {"limited": "limited", "minimum": "limited", "fixed": "fixed", "height": "height-limit"}
            if fixture in hints:
                peer.stdin.write(hints[fixture] + "\n")
            command = "follow-configures"
            if fixture in ("delayed", "chosen"):
                command = "follow-" + fixture
            if os.environ.get("WEX_ASSOCIATION_RESIZE_FAILED") == "1":
                command = "ignore-configures"
            peer.stdin.write(command + "\n")
            peer.stdin.flush()
        report = state / f"probe{bits}.log"
        binary = "states" if states else "binding" if binding else "hosting" if hosting else "association"
        probe = subprocess.Popen([str(wine), str(root / f"checks/{binary}{bits}.exe"), *values[1:], f"Z:{report}"],
                                 env=env, stdout=(state / f"launch{bits}.log").open("w"), stderr=subprocess.STDOUT)
        deadline = time.monotonic() + (120 if hosting else 20)
        preview_captured = False
        while not report.exists() or "READY_CONTROLLER_END" not in report.read_text(encoding="utf-8", errors="replace"):
            assert probe.poll() is None and time.monotonic() < deadline, (
                report.read_text(encoding="utf-8", errors="replace") if report.exists()
                else "Wine association readiness timed out")
            if report.exists() and not preview_captured:
                content = report.read_text(encoding="utf-8", errors="replace")
                if "READY_LIVE_PREVIEW" in content:
                    from PIL import Image
                    sample = re.search(r"PREVIEW_PIXEL (\d+) (\d+)", content)
                    assert sample, content
                    screenshot = state / f"preview{bits}.png"
                    subprocess.run(["grim", str(screenshot)], env=env, check=True, timeout=10)
                    with Image.open(screenshot) as image:
                        actual = image.convert("RGB").getpixel((int(sample[1]), int(sample[2])))
                    assert actual == (0x33, 0x88, 0x55), (bits, "published native preview", actual)
                    Path(str(report) + ".preview").write_text("preview captured\n")
                    preview_captured = True
                    print(f"LIVE_PREVIEW_RENDER_PASSED bits={bits}", flush=True)
            time.sleep(0.1)
        if hosting:
            from PIL import Image
            pixel = re.search(r"HOSTING_PIXEL (\d+) (\d+)", report.read_text(encoding="utf-8"))
            assert pixel, "hosting probe did not publish a native client sample point"
            screenshot = state / f"hosting{bits}.png"
            subprocess.run(["grim", str(screenshot)], env=env, check=True, timeout=10)
            with Image.open(screenshot) as image:
                actual = image.convert("RGB").getpixel((int(pixel[1]), int(pixel[2])))
            assert actual == (0x33, 0x88, 0x55), (bits, "native content inside Wine frame", actual)
            print(f"HOSTING_RENDER_PASSED bits={bits}", flush=True)
            if os.environ.get("WEX_ASSOCIATION_RESIZE_FAILED") == "1":
                peer.stdin.write(f"click {pixel[1]} {pixel[2]}\n")
                peer.stdin.flush()
                assert select.select([peer.stdout], [], [], 5)[0], "failed recovery input timed out"
                reply = peer.stdout.readline().strip().split()
                assert len(reply) == 4 and reply[:3] == ["CLICK", "1", "1"], reply
                print(f"FAILED_NATIVE_FOCUS_PASSED bits={bits}", flush=True)
        if states:
            for expected in (1, 0, 1, 0):
                peer.stdin.write("minimize\n" if expected else "restore\n")
                peer.stdin.flush()
                deadline = time.monotonic() + 5
                while True:
                    peer.stdin.write("state\n"); peer.stdin.flush()
                    assert select.select([peer.stdout], [], [], 5)[0]
                    if peer.stdout.readline().strip() == f"STATE {expected}":
                        break
                    assert time.monotonic() < deadline, "native state transition timed out"
                    time.sleep(.01)
        peer.stdin.write("release\n")
        peer.stdin.flush()
        if binding or states:
            Path(str(report) + ".continue").write_text("render captured\n", encoding="utf-8")
        assert probe.wait(timeout=30 if binding else 15) == 0, report.read_text(encoding="utf-8", errors="replace")
        result = report.read_text(encoding="utf-8", errors="replace")
        assert f"ASSOCIATION_PASSED bits={bits} failures=0" in result, result
        print(result.strip(), flush=True)
        peer.stdin.write("quit\n")
        peer.stdin.flush()
        assert peer.wait(timeout=5) == 0
        peer = None
        stop_external()
        subprocess.run([str(server), "-k"], env=env, check=True, timeout=45)
        subprocess.run([str(server), "-w"], env=env, check=True, timeout=45)
    if os.environ.get("WEX_ASSOCIATION_STACK") == "1":
        subprocess.run([sys.executable, str(Path(__file__).with_name("check-wayland-stacking.py")),
                        str(wine), str(root / "checks/association-stack64.exe"), str(state / "stack"),
                        "--second-probe", str(root / "checks/association-stack32.exe")],
                       env=env, check=True, timeout=180)
finally:
    stop_external()
    for process in (probe, peer):
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    try:
        subprocess.run([str(server), "-k"], env=env, timeout=45)
    finally:
        compositor.terminate()
        compositor.wait(timeout=10)
print(f"Association logs: {state}")
