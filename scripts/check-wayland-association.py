# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Run after configuring an isolated prefix for Wayland; own headless compositor."""
import os
from pathlib import Path
import select
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
compositor = subprocess.Popen([str(compositor_dir / "build/labwc"), "-C", str(config)], env=env,
                             stdout=(state / "compositor.log").open("w"), stderr=subprocess.STDOUT)
peer = probe = None
try:
    deadline = time.monotonic() + 10
    while not (runtime / "wex-shell-0").exists():
        assert compositor.poll() is None and time.monotonic() < deadline
        time.sleep(0.1)
    for bits in (64, 32):
        peer = subprocess.Popen([str(compositor_dir / "tests/wex/foreign-client"), "--publish-association"],
                                env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=(state / f"peer{bits}.log").open("w"), text=True)
        assert select.select([peer.stdout], [], [], 10)[0], "native peer readiness timed out"
        values = peer.stdout.readline().strip().split()
        assert len(values) == 3 and values[0] == "LEASE", values
        report = state / f"probe{bits}.log"
        probe = subprocess.Popen([str(wine), str(root / f"checks/association{bits}.exe"), *values[1:], f"Z:{report}"],
                                 env=env, stdout=(state / f"launch{bits}.log").open("w"), stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 20
        while not report.exists() or "READY_CONTROLLER_END" not in report.read_text(encoding="utf-8", errors="replace"):
            assert probe.poll() is None and time.monotonic() < deadline, "Wine association readiness timed out"
            time.sleep(0.1)
        peer.stdin.write("release\n")
        peer.stdin.flush()
        assert probe.wait(timeout=15) == 0, report.read_text(encoding="utf-8", errors="replace")
        result = report.read_text(encoding="utf-8", errors="replace")
        assert f"ASSOCIATION_PASSED bits={bits} failures=0" in result, result
        print(result.strip(), flush=True)
        peer.stdin.write("quit\n")
        peer.stdin.flush()
        assert peer.wait(timeout=5) == 0
        peer = None
        subprocess.run([str(server), "-k"], env=env, check=True, timeout=45)
        subprocess.run([str(server), "-w"], env=env, check=True, timeout=45)
    if os.environ.get("WEX_ASSOCIATION_STACK") == "1":
        subprocess.run([sys.executable, str(Path(__file__).with_name("check-wayland-stacking.py")),
                        str(wine), str(root / "checks/association-stack64.exe"), str(state / "stack"),
                        "--second-probe", str(root / "checks/association-stack32.exe")],
                       env=env, check=True, timeout=90)
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
        subprocess.run([str(server), "-k"], env=env, timeout=45)
    finally:
        compositor.terminate()
        compositor.wait(timeout=10)
print(f"Association logs: {state}")
