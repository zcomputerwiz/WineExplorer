<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 recovery phase-4 pre-fault evidence report — 2026-10-22

Status: **stopped before fault injection; not a recovery acceptance result.**
The parent reviewed the paired PNGs and confirmed a stale outer-front image
against the current Electron page image, with the complete frame intact. This
report records the bounded control and the reason the next planned experiment
is a fixed-timeline no-fault run, not another recovery fault.

## Run identity and output

- Branch/revision: `codex/g4-r1-completion`, root `b2b4d306fc738c45f3949635ceef6c0f7be954e1`.
- Wine submodule: `4315be83d75644f13500381dcec94dcdc5fa904e`.
- wexcomp submodule: `94faa651f9e267fd03157348b26ffb3c66082484`.
- Owned QEMU guest: `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/system.qcow2`,
  booted only through the existing WSL launcher; no other guest was started.
- Fresh run root:
  `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/recovery-phase4-r1-20261022/`.
- Healthy control: `.../healthy-control/`; diagnostic: `.../single-fault/`;
  compressed diagnostic evidence:
  `.../single-fault-evidence.zip` (SHA-256
  `A5F7F0D19A187AFED99474D98AE4137A7D34C27958790FF680A70B9C699CF995`).
- The archive contains host, Chromium, results and runner output; wexcomp,
  capture, Wine and Wineboot stdout/stderr; native EGL and per-stage native
  window logs; process trace; both pre_fault acknowledgements; and paired
  front/page PNGs. It excludes Wine prefixes and private runtimes. The initial
  missing-helper preflight record is separately retained at the run root.

## Exact execution commands

The owned launcher was run with the documented WSL isolation command (fresh
output path was selected by a private launcher edit):

```powershell
wsl -d Debian --exec unshare --mount --propagation private python3 -B /mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/launch.py
```

The first diagnostic invocation, before the staging repair, was:

```sh
python3 -B check-g4-webview2.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --runtime /home/debian/wex-g4/private-electron-44.9.2/runtime --host /home/debian/wex-g4/private-electron-44.9.2/runtime/electron.exe --electron-app /home/debian/wex-g4/g4-r1-phase4-stage/g4-electron-probe --gpu-recovery --seconds 40 --gl-driver virgl --wexcomp-bin /home/debian/wex-g4/labwc --wexcomp-config /home/debian/wex-g4/wayland --compositor-renderer gles2 --output-dir /home/debian/wex-g4/recovery-phase4-single-fault-20261022
```

It exited 1 before launching Wine: the staged
`test_g4_native_window_state.cpp` dependency was absent. This was a harness
preflight failure, not a graphics failure. The original failure and empty
output are preserved in `preflight-failure.txt` and the unique guest output.
As authorized, the exact source and `run-isolated-graphics.py` wrapper were
staged and hash-verified before retry. The native-window helper compiled in
preflight; isolated namespace checks passed. No earlier result was relabeled.

The healthy accelerated no-fault control command was:

```sh
python3 -B check-g4-webview2.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --runtime /home/debian/wex-g4/private-electron-44.9.2/runtime --host /home/debian/wex-g4/private-electron-44.9.2/runtime/electron.exe --electron-app /home/debian/wex-g4/g4-r1-phase4-stage/g4-electron-probe --freeze-scene --seconds 25 --gl-driver virgl --wexcomp-bin /home/debian/wex-g4/labwc --wexcomp-config /home/debian/wex-g4/wayland --compositor-renderer gles2 --output-dir /home/debian/wex-g4/recovery-phase4-healthy-20261022
```

Exit 0 (`host_exit=0`, `diagnostic_exit=0`). Host, inner EGL and outer
wexcomp GLES2 verified VirGL over D3D12/Intel UHD Graphics 750. Animation
progressed to 849 frames across 24 visible samples; classified Chromium error
counters were all zero. Parent inspection of `healthy-control/screen-24.png`
confirmed complete caption, menu, client and blue marker. This control does
not substitute for the recovery gate.

After staging repair, the bounded single-fault diagnostic command was run
through the isolated mount/network/socket wrapper:

```sh
sudo -n /usr/bin/python3 -B /home/debian/wex-g4/g4-r1-phase4-stage/run-isolated-graphics.py --socket-root /home/debian/wex-g4/isolated-sockets -- runuser -u debian -- python3 -B /home/debian/wex-g4/g4-r1-phase4-stage/check-g4-webview2.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --runtime /home/debian/wex-g4/private-electron-44.9.2/runtime --host /home/debian/wex-g4/private-electron-44.9.2/runtime/electron.exe --electron-app /home/debian/wex-g4/g4-r1-phase4-stage/g4-electron-probe --gpu-recovery --seconds 40 --gl-driver virgl --wexcomp-bin /home/debian/wex-g4/labwc --wexcomp-config /home/debian/wex-g4/wayland --compositor-renderer gles2 --output-dir /home/debian/wex-g4/recovery-phase4-single-fault-verified-20261022
```

Wrapper exit 1; `host_exit=0`, `diagnostic_exit=4`, run ID
`2351f6c68f3c7aa8e43c11b1ca213e4d`, `fault=null`. The healthy `pre_fault`
pixel gate refused injection: page passed, front failed. Window bounds were
`{x:25,y:25,width:740,height:520}`; observed client `{x:29,y:79,width:732,height:462}`;
viewport 732x462, scale 1.0. Page marker sample `(212,198)` was blue
`[21,101,192]`, with 400/400 blue pixels in its sampled marker region and the
expected green background. The mapped front sample `(241,277)` was green
`[232,245,233]` instead of blue; all left/right/bottom rim fractions were
1.0. Parent image inspection confirms front still displayed “Frames: 2” and
the old marker position while page showed the current frozen scene. The frame
is intact. This is stale-front/current-page evidence, not evidence of the
underlying cause. No recovered-stage image exists and no recovery acceptance
was tested.

## Source, deployed and binary identities

The complete source/deployed manifest is retained at
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/recovery-phase4-r1-20261022/deployment-manifest.txt`.
Relevant SHA-256 values (source / deployed, equal where paired):

| Artifact | SHA-256 |
| --- | --- |
| Private launcher `build/g4-virgl-20261022/launch.py` | `f5a1e0249db86c791595b5f028c5d482419c2bcd55992c1ce7810791625657d5` |
| `scripts/check-g4-webview2.py` | `05339ef26377f5f3db925a80471852a3a295bdf1e07f6555eae74cf712d84d89` |
| `scripts/check-g3-correctness.py` | `48ee9788c31c2d35b800133e91f97b3c7ffe44ac609bab8cc35d8791a39af207` |
| `scripts/test_graphics_probes.py` | `329ae04bbab141b5d541661ad3f708608f6f613656fce47c219b10240ebcf11d` |
| `scripts/graphics_frame_pixels.py` | `03e6dcbbcd44f7bbb33ee9a26cb0dee23cf6355b9d92d384ff34520e95d775a0` |
| `scripts/graphics_resources.py` | `110ca088e5b1505e54fdfd1f84fe06fa0cd95086bfbbeff7aeb7a585d14a929a` |
| `scripts/graphics_wexcomp.py` | `9a09a1f41e9a8036a0fa6597c9050da81fc1d75e0a6cf45b9991f503d4e77f78` |
| `scripts/analyze-webview2-run.py` | `d4b7649550aedc99cdf39581959eb4ab4724d6495c7fb42f22fa24efa74e9c28` |
| `scripts/g4-electron-probe/main.js` | `363d0c6ff0c652508ed4f065035b49f7549441e37b33f52844489d0b692c19f0` |
| `scripts/g4-electron-probe/package.json` | `0bc224c4b50d715fec113dff98fad0d901e9a738ad087f290949c04aa931e860` |
| `scripts/run-isolated-graphics.py` (source/deployed) | `82a21d9bc378249f1602af989c53f8ed9603af632b678a041a041789f12a5e1c` |
| `scripts/test_g4_native_window_state.cpp` (source/deployed) | `e55c831c7e24e3d8969859183890f6b7fd146754d20f2922146a43f9391ff60a` |
| Launched `native-window-state.exe` in diagnostic output | `F71FC8F3865008C6CA1A15692D290FD78276D5323D2285C8B4DB1200E9F4B27B` |
| Wine binary | `477df57788ca7cc54d3297cf7a455cd4220133cb4ffa845de24c2ab5c9c30867` |
| wineserver binary | `a9ce0193c5b32cbe760fe6e4b66c02f747de775c30e914fa87e7687d1f31c6e5` |
| Electron runtime `electron.exe` | `448da7f0a402b7299f87dc7dc896d48fd58a726ccbcf65b69820745f1bd38bf2` |
| Outer compositor `labwc` | `4a24dd13fed3d733d4edd6f213e4db284ff0dee9229e5df898d694d4446e0ec8` |
| VM seed ISO | `e8d423e612a9aef41489bf5deb5e3195751517cc1a37369af2754a451d2d8096` |

## Launcher termination and shutdown evidence

The private launcher starts QEMU and Xvfb, stores its own PID in `pid`, handles
SIGINT/SIGTERM by setting a stop flag, sends QMP `system_powerdown`, waits up to
45 seconds, terminates Xvfb, then writes the final host-side `results.json`.
For this run, `pid` contains 366; process inspection observed QEMU PID 381 and
Xvfb PID 368 during execution. Later process inspections found no matching
launcher/QEMU/Xvfb, and the SSH forward was gone. The pre-existing
`WineExplorer-VM` remained the only running VirtualBox VM. Parent's independent
offline `qemu-img check` reported no errors; this verifies image consistency,
not guest shutdown.

The expected launcher final `results.json` is absent. `qemu.stdout.log`,
`qemu.stderr.log`, and `xvfb.log` are empty; `serial.log` has boot/provisioning
output but no completed poweroff marker. The launcher source has no normal path
that omits the final write after entering its `try/finally`, so either it did
not complete that `finally`, or that write failed; retained evidence does not
distinguish these. The launch was interrupted through the owned exec session,
but the retained session result does not establish whether the signal reached
the Python launcher, whether the exec session expired, or whether an external
termination (including SIGKILL) occurred. No timeout/SIGKILL is proven, and no
graceful shutdown is claimed. The later read-only poll found the exec session
ID no longer available; the app terminal connector had no session attached.

Before another launch, use a persistent owned session, record and validate
launcher PID plus process start identity, send SIGINT to that exact identity,
and preserve the launched command's exit status and final launcher result.
Diagnose any missing final record before guest reuse. Do not infer shutdown from
QEMU absence alone.

## Checks and handoff

This phase made no tracked implementation change and therefore ran no new code
test suite. The pre-existing Linux focused test results for the pixel/capture
instrumentation are recorded in the preceding milestone and parent review; they
do not prove live recovery. No VM is running as a result of this report. Next
step is parent-directed fixed-timeline no-fault capture timing work, retaining
strict front pixels and front-before-page order. No retry loop, page-first
capture, fault injection, or production rendering fix is authorized by this
evidence.
