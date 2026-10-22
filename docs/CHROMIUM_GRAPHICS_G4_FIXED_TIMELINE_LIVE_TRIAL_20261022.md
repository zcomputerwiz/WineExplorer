<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 fixed-timeline recovery diagnostic — 2026-10-22

This is one bounded accelerated Electron diagnostic trial after adding the
two-renderer-rAF opportunity wait. It is not a Chromium/G4 acceptance result.
No production Wine/compositor changes were made. The VirGL guest was shut down
cleanly after the two consumer runs.

## Run and retained evidence

Branch `codex/g4-r1-completion` started at `83bd54eb` (Windows launcher-path
validation). The fresh D: run root is
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/recovery-r1-fixed-timeline-20261022/`.
It contains the persistent controller's `.wsl-session.json`, separate WSL
stdout/stderr logs, the Linux `launcher-identity.json`, launcher `results.json`,
`serial.log`, QEMU/Xvfb/host GL logs, and two evidence directories:

- `healthy-control-evidence/`: actual frozen-control screenshots, host,
  Chromium, Wine stdout/stderr, compositor, capture, native EGL and process
  logs, plus `results.json`.
- `single-fault-evidence/`: all corresponding logs, all screen/front/page PNGs,
  all stage ACK files, per-stage native-window logs, `native-window-state.exe`,
  and `results.json`. Prefixes, profiles, Wayland runtime directories, and the
  Electron runtime were not copied into either evidence directory.

The entire raw fault output remains on the guest at
`/home/debian/wex-g4/recovery-r1-fixed-timeline-20261022/single-fault/`; this
includes its per-run prefix/profile, which are not part of the D: evidence
copy. No unrelated VM or checkout was used.

## Exact commands and exit codes

Persistent launcher controller (kept alive until its separate final exit):

```powershell
pwsh -NoProfile -File D:/DeepThought/Projects/WineExplorer/scripts/g4-virgl-launcher-session.ps1 -RunName recovery-r1-fixed-timeline-20261022
```

It launched the reviewed private
`build/g4-virgl-20261022/launch.py` under Debian WSL with a private mount
namespace, a 900-second launcher bound, isolated X sockets, and loopback SSH
2235. The run name and output directory were new. Linux PID/start-tick/cmdline
identity and Windows WSL PID/start time were recorded and checked before
consumer work. SSH used only the existing lab key and dedicated known-hosts
file.

Both consumers used the staged `run-isolated-graphics.py` wrapper, fresh
prefixes, `--gl-driver virgl`, the owned `labwc` GLES2 compositor and the
existing Electron 44.9.2 runtime. Exact guest commands:

```sh
sudo -n /usr/bin/python3 -B /home/debian/wex-g4/recovery-r1-fixed-timeline-stage/scripts/run-isolated-graphics.py --socket-root /home/debian/wex-g4/isolated-sockets -- runuser -u debian -- python3 -B /home/debian/wex-g4/recovery-r1-fixed-timeline-stage/scripts/check-g4-webview2.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --runtime /home/debian/wex-g4/private-electron-44.9.2/runtime --host /home/debian/wex-g4/private-electron-44.9.2/runtime/electron.exe --electron-app /home/debian/wex-g4/recovery-r1-fixed-timeline-stage/scripts/g4-electron-probe --freeze-scene --seconds 60 --gl-driver virgl --wexcomp-bin /home/debian/wex-g4/labwc --wexcomp-config /home/debian/wex-g4/wayland --compositor-renderer gles2 --output-dir /home/debian/wex-g4/recovery-r1-fixed-timeline-20261022/healthy-control
```

Healthy frozen control: wrapper/host exit 0, diagnostic exit 0. Actual
`screen-24.png` shows the complete window/menu and blue marker at “Frozen at
x=160”. It froze at the requested scene state; renderer frame counters
continued by design. 59 visible samples progressed from 0 to 2810 frames.
Outer GLES2 renderer and inner EGL renderer both explicitly reported
`virgl (D3D12 (Intel(R) UHD Graphics 750))`.

Exactly one fault attempt followed, with the strict pre-fault front-and-page
pixel gate enabled (no page-first capture or pixel retry):

```sh
sudo -n /usr/bin/python3 -B /home/debian/wex-g4/recovery-r1-fixed-timeline-stage/scripts/run-isolated-graphics.py --socket-root /home/debian/wex-g4/isolated-sockets -- runuser -u debian -- python3 -B /home/debian/wex-g4/recovery-r1-fixed-timeline-stage/scripts/check-g4-webview2.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --runtime /home/debian/wex-g4/private-electron-44.9.2/runtime --host /home/debian/wex-g4/private-electron-44.9.2/runtime/electron.exe --electron-app /home/debian/wex-g4/recovery-r1-fixed-timeline-stage/scripts/g4-electron-probe --gpu-recovery --seconds 60 --gl-driver virgl --wexcomp-bin /home/debian/wex-g4/labwc --wexcomp-config /home/debian/wex-g4/wayland --compositor-renderer gles2 --output-dir /home/debian/wex-g4/recovery-r1-fixed-timeline-20261022/single-fault
```

Wrapper/host exit 0 and diagnostic exit 0. The pre-fault gate passed, then
exactly one owned GPU child (PID 604, creation time `1792677869489.432`) was
killed. Replacement GPU PID 956 appeared. Browser main PID 420 and renderer
PID 680 retained their creation identities; no renderer exit was recorded.
Both WebGL contexts recorded one context-lost event and were valid after
replacement. Pre/post default pixels were `[255,0,0,255]`, texture pixels
`[0,255,0,255]`, GL errors 0, framebuffer status 36053, GPU compositing
remained enabled, and renderer identity stayed ANGLE Intel G45/G43 D3D11.
Frames advanced 29 to 73 after recovery. The trial's animation subdiagnostic
records one GPU-process crash (the intentionally injected child); its
`diagnostic_passed` is false on that counter. The separate recovery verdict
and overall diagnostic exit are true/0. This distinction is retained rather
than relabeling the counter.

The frozen-scene DOM changes were followed by exactly two renderer rAF
callbacks before capture. `host.log` records pre-fault wait 30 ms
(`timestamps` 6443, 6461) and recovered wait 96 ms (14825, 14874), both
`observedFrames=2`, `passed=true`, `timeoutMs=1500`. These callbacks provide a
rendering opportunity, not proof of compositor presentation.

## Pixel evidence and geometry

All four front verdicts and all four page verdicts passed:
`pre_fault`, `recovered`, `settled_2s`, and `settled_4s`. The strict pre-fault
gate therefore permitted injection. There were no missing captures or
observer errors. Parent review should use the paired PNGs under
`single-fault-evidence/`; the pre-fault and recovered pairs were inspected
visually during this run and show complete content.

Observed outer HWND/window bounds were `{x:25,y:25,width:740,height:520}`;
observed client origin/bounds were `{x:29,y:79,width:732,height:462}`; page
viewport was `732x462`; output scale was 1.0. The 4-pixel horizontal and
54-pixel vertical client offset was included in front sampling. The marker
rectangle was `{x:192,y:177.59375,width:40,height:40}` in client coordinates.
Front marker center `(241,277)` and page marker center `(212,198)` each matched
blue `[21,101,192]`; each sampled 400/400 blue marker pixels. Pre-fault front
background `(269,277)` matched `[232,245,233]`; later front/page background
matched `[255,243,224]`. Front left/right/bottom rim validity was 1.0 at every
stage. Full decoded per-stage verdicts and actual pixels are in
`single-fault-evidence/results.json` and the paired PNGs.

## Source and deployed hashes

Before either consumer ran, all 13 staged source files below were compared
against the guest copies by SHA-256; every pair matched. The private launcher
was executed directly from its hashed workspace path. The consumer's
`results.json` also records Electron/runtime and Wine stack binary hashes.

| Source and deployed file | SHA-256 |
| --- | --- |
| `build/g4-virgl-20261022/launch.py` (private launcher) | `b6ea8870863d778b01d34fb663787b9d02ff917e1987d84e9b23faddc0aa581b` |
| `scripts/check-g4-webview2.py` | `66f9aba9d28ea5be93978d7793267118348e01b72af8b2a78890d4a1e9d19302` |
| `scripts/check-g3-correctness.py` | `48ee9788c31c2d35b800133e91f97b3c7ffe44ac609bab8cc35d8791a39af207` |
| `scripts/graphics_resources.py` | `110ca088e5b1505e54fdfd1f84fe06fa0cd95086bfbbeff7aeb7a585d14a929a` |
| `scripts/graphics_frame_pixels.py` | `03e6dcbbcd44f7bbb33ee9a26cb0dee23cf6355b9d92d384ff34520e95d775a0` |
| `scripts/graphics_wexcomp.py` | `9a09a1f41e9a8036a0fa6597c9050da81fc1d75e0a6cf45b9991f503d4e77f78` |
| `scripts/analyze-webview2-run.py` | `d4b7649550aedc99cdf39581959eb4ab4724d6495c7fb42f22fa24efa74e9c28` |
| `scripts/test_graphics_probes.py` | `329ae04bbab141b5d541661ad3f708608f6f613656fce47c219b10240ebcf11d` |
| `scripts/test_g4_native_window_state.cpp` | `e55c831c7e24e3d8969859183890f6b7fd146754d20f2922146a43f9391ff60a` |
| `scripts/snapshot-graphics-x11.py` | `0024e6c86e8bf5b1f1cdffb7acc307282c75078d53c82dc58846cf3287db2063` |
| `scripts/run-isolated-graphics.py` | `82a21d9bc378249f1602af989c53f8ed9603af632b678a041a041789f12a5e1c` |
| `scripts/g4-electron-probe/main.js` | `a05610638fb40e5078ed4c8495b99e86699795a92f7df57d6ab12b3874c8a50f` |
| `scripts/g4-electron-probe/package.json` | `0bc224c4b50d715fec113dff98fad0d901e9a738ad087f290949c04aa931e860` |
| `scripts/g4-electron-probe/recovery-frame-wait.js` | `5fa053c6912b67dc11fb42a564cbef10398287a4f0bbce0459ad06a753384149` |

## Shutdown and limits

After consumer completion, the separate command

```powershell
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Projects/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/recovery-r1-fixed-timeline-20261022/launcher-identity.json --timeout 60
```

validated launcher and child identities before SIGINT. Its verified final
record says `stop_reason=signal`, `graceful_powerdown=true`, QEMU and Xvfb
stopped, QEMU exit status 0. The persistent Windows controller then exited
with WSL process exit code 0 and its recorded Windows PID was no longer
running. No process-absence-only inference was used.

This is one diagnostic harness run, not product acceptance, not proof that all
Chromium graphics survive recovery, and not a cause assignment for any prior
whole-window blanking. No repeated faults, post-fault WebView2 matrix,
fractional-scale live trial, or production fix was attempted. Lower-priority
follow-up: consider extending independent actual-pixel controls to a second
supported scale only after parent review of these captures and a separate
bounded plan. The current trial's scale-1 geometry and full-front rim checks
are recorded, not generalized.
