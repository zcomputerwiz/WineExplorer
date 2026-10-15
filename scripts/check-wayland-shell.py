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
owners = os.environ.get("WEX_SHELL_TEST_OWNERS") == "1"
assert not owners or enabled, "owner tests require the explicit adoption gate"

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
    comp_args = [os.environ.get("WEX_WEXCOMP_BIN", str(comp / "build/labwc")), "-C", str(config)]
    if os.environ.get("WEX_SHELL_TEST_DEBUG") == "1":
        comp_args.append("-d")
    compositor = subprocess.Popen(comp_args, env=env,
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
                                  "owners" if owners else "enabled" if enabled else "disabled", "Z:" + str(report)], env=env,
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

            if not owners:
                for marker, value in (("LIMITED", "limited"), ("FIXED", "fixed"),
                                      ("HEIGHT_LIMIT", "height-limit"), ("UNLIMITED", "unlimited")):
                    report_wait(report, f"READY_{marker}", probe)
                    command(value)
            if owners:
                report_wait(report, "READY_OWNER_CHILD", probe)
                command("child")
                capture(report_wait(report, "READY_OWNER_CLEAR", probe), "CHILD")
                command("orphan")
                report_wait(report, "READY_OWNER_SET", probe)
                command("parent")
                report_wait(report, "READY_OWNER_CSD", probe)
                command("csd")
                report_wait(report, "READY_OWNER_SSD", probe)
                command("ssd")
                report_wait(report, "READY_OWNER_UNMAP", probe)
                command("parent-unmap")
                report_wait(report, "READY_OWNER_REMAP", probe)
                command("parent-remap")
                command("parent-state")
                assert select.select([peer.stdout], [], [], 5)[0]
                assert peer.stdout.readline().strip() == "PARENT 0", "remap restored native ownership"
                Path(str(report) + ".remapped").write_text("native parent remains unset\n")
                report_wait(report, "READY_OWNER_RELATE", probe)
                command("parent")
                report_wait(report, "READY_OWNER_CLOSE", probe)
                command("parent-close")
                report_wait(report, "READY_CHILD_CLOSE", probe)
                Path(str(report) + ".close-child").write_text("close\n")
                assert probe.wait(timeout=25) == 0, report.read_text(errors="replace")
                assert peer.wait(timeout=10) == 0, "native child did not honor close"
                print(report.read_text(), flush=True)
                continue

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
            content = report_wait(report, "READY_TASK_MINIMIZE", probe)
            task = re.search(r"TASK_POINT (\d+) (\d+)", content)
            assert task, content
            x, y = int(task[1]), int(task[2])
            # Exclude the hover border and icon; the active title is bold.
            crop = (x + 10, y - 6, x + 120, y + 6)
            active = case / "task-active.png"
            subprocess.run(["grim", str(active)], env=env, check=True, timeout=10)
            with Image.open(active) as image:
                active_title = image.convert("RGB").crop(crop).tobytes()

            def task_click(button="click", point=task):
                assert point, report.read_text()
                command(f"{button} {point[1]} {point[2]}")
                assert select.select([peer.stdout], [], [], 5)[0]
                reply = peer.stdout.readline().strip().split()
                assert len(reply) == 4 and reply[0] == "CLICK", reply

            def menu_key(key):
                command(f"key {key}")  # Linux input-event codes, US menu mnemonics
                assert select.select([peer.stdout], [], [], 5)[0]
                assert peer.stdout.readline().strip() == "KEY"

            task_click()
            report_wait(report, "READY_MINIMIZED", probe)
            minimized = case / "task-minimized.png"
            deadline = time.monotonic() + 5
            while True:
                command("state")
                assert select.select([peer.stdout], [], [], 5)[0]
                if peer.stdout.readline().strip() == "STATE 1":
                    break
                assert time.monotonic() < deadline, "native did not minimize"
                time.sleep(.05)
            deadline = time.monotonic() + 5
            while True:
                subprocess.run(["grim", str(minimized)], env=env, check=True, timeout=10)
                with Image.open(minimized) as image:
                    inactive = image.convert("RGB").crop(crop).tobytes()
                if inactive != active_title:
                    break
                assert time.monotonic() < deadline, "minimized task button kept its active appearance"
                time.sleep(.05)
            Path(str(report) + ".restore").write_text("native minimized\n")
            report_wait(report, "READY_TASK_RESTORE", probe)
            task_click()
            report_wait(report, "READY_TASK_MENU_CANCEL", probe)
            restored = case / "task-restored.png"
            # The only eligible task occupies the first button. Compare with
            # the pointer in the same place so hover cannot explain the change.
            deadline = time.monotonic() + 5
            while True:
                subprocess.run(["grim", str(restored)], env=env, check=True, timeout=10)
                with Image.open(restored) as image:
                    if inactive != image.convert("RGB").crop(crop).tobytes():
                        break
                assert time.monotonic() < deadline, "task button did not repaint its active appearance"
                time.sleep(.05)
            task_click("right")
            report_wait(report, "\nMENU_CANCEL\n", probe)
            menu_key(1)  # Escape
            report_wait(report, "READY_TASK_DEACTIVATE", probe)
            task_click(point=(None, "1200", "100"))  # unoccupied desktop
            report_wait(report, "READY_TASK_ACTIVATE", probe)
            task_click()
            for phase, end_key in (("MOVE_COMMIT", 28), ("MOVE_CANCEL", 1),
                                   ("MOVE_DRAG", None), ("MOVE_CANCELMODE", "cancel")):
                content = report_wait(report, f"READY_TASK_MENU_MOVE_{phase}", probe)
                if end_key is None:
                    task_click("press", point=re.search(r"PRESS_POINT (\d+) (\d+)", content))
                else:
                    task_click("right")
                    report_wait(report, f"\nMENU_MOVE_{phase}\n", probe)
                    menu_key(50)  # M
                content = report_wait(report, f"READY_MOVE_INPUT_{phase}", probe)
                target = re.search(rf"MOVE_POINT_{phase} (\d+) (\d+)", content)
                task_click("motion", point=target)
                report_wait(report, f"READY_MOVE_FINISH_{phase}", probe)
                if end_key == "cancel":
                    Path(str(report) + ".cancel-mode").write_text("cancel mode\n")
                elif end_key is None:
                    task_click("release-at", point=target)
                else:
                    menu_key(end_key)  # Enter commits; Escape cancels
                content = report_wait(report, f"READY_MOVE_CAPTURE_{phase}", probe)
                expected = tuple(map(int, re.search(rf"NATIVE_{phase} (\d+) (\d+) (\d+) (\d+)", content).groups()))
                deadline = time.monotonic() + 5
                while True:
                    command("geometry")
                    assert select.select([peer.stdout], [], [], 5)[0]
                    reply = peer.stdout.readline().split()
                    assert len(reply) == 5 and reply[0] == "GEOMETRY", reply
                    if all(abs(a - b) <= 1 for a, b in zip(map(int, reply[1:]), expected)):
                        break
                    assert time.monotonic() < deadline, ("native content did not follow move", reply, expected)
                    time.sleep(.05)
                capture(content, phase)
                Path(str(report) + "." + phase).write_text("move captured\n")
            for action in ("MINIMIZE", "RESTORE"):
                report_wait(report, f"READY_TASK_MENU_{action}", probe)
                task_click("right")
                report_wait(report, f"\nMENU_{action}\n", probe)
                subprocess.run(["grim", str(case / f"task-menu-{action.lower()}.png")],
                               env=env, check=True, timeout=10)
                menu_key(49 if action == "MINIMIZE" else 19)  # N / R
            report_wait(report, "READY_NATIVE_MINIMIZE", probe)
            command("minimize")
            report_wait(report, "READY_NATIVE_RESTORE", probe)
            command("restore")
            try:
                report_wait(report, "READY_CSD", probe)
            except AssertionError:
                command("state")
                if select.select([peer.stdout], [], [], 5)[0]:
                    print("RESTORE_FAILURE_NATIVE", peer.stdout.readline().strip(), flush=True)
                raise
            command("csd")
            report_wait(report, "READY_SSD", probe)
            command("ssd")
            report_wait(report, "READY_TASK_MENU_CLOSE", probe)
            task_click("right")
            report_wait(report, "\nMENU_CLOSE\n", probe)
            menu_key(46)  # C
        assert probe.wait(timeout=25) == 0, report.read_text(errors="replace")
        print(report.read_text(), flush=True)
        if enabled:
            assert peer.wait(timeout=10) == 0, "native did not honor forwarded close"
    except BaseException:
        # Opt-in, failure-only diagnostics: attach solely to this test's own
        # shell process before teardown. Never inspect the real desktop shell.
        if os.environ.get("WEX_SHELL_TEST_BACKTRACE") == "1":
            try:
                # Wine's launcher can remain start.exe while the actual shell
                # is a separate Unix process. Match only this isolated prefix;
                # never attach to another session's Explorer.
                prefix = b"WINEPREFIX=" + os.fsencode(env["WINEPREFIX"])
                for process in Path("/proc").iterdir():
                    if not process.name.isdigit():
                        continue
                    try:
                        if process.joinpath("comm").read_text().strip() != "explorer.exe":
                            continue
                        if prefix not in process.joinpath("environ").read_bytes().split(b"\0"):
                            continue
                    except (OSError, PermissionError):
                        continue
                    with (case / f"shell-backtrace-{process.name}.log").open("w") as output:
                        subprocess.run(["sudo", "-n", "gdb", "-batch", "-ex", "set pagination off",
                                        "-ex", "info sharedlibrary", "-ex", "thread apply all bt",
                                        "-p", process.name], stdout=output,
                                       stderr=subprocess.STDOUT, timeout=20)
            except (OSError, subprocess.TimeoutExpired) as error:
                print(f"Test shell backtrace unavailable: {error}", flush=True)
        raise
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
