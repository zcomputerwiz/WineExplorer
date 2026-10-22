<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native acceleration progress (2026-10-21)

Scope: NATIVE_ACCELERATION_SCOPE.md. Wine4315be8/compositor94faa651 unchanged.
Native **application GL acceleration works in WSL**. Outer desktop acceleration
is not proven and fails the explicit headless GLES2 startup control. No master
merge or installed-shell/theme claim. No existing VM/session was modified.

## Independent backend verification and real consumers

New `check-g4-native-egl.py` starts a fresh owned private wexcomp endpoint and
runs `eglinfo -B -p wayland -a gl` against that endpoint, not GLX/WSLg. Native
consumer and process-boundary harnesses now support `--gl-driver d3d12` through
this strict verifier. It rejects failed probes, foreign/GLX endpoints, software
renderer strings and the Microsoft Basic Render Driver. Default software
controls and the existing X11 verification remain separate.

ROOT=`/var/tmp/wex-g4-tree-20261020`; SCRIPT is the repo's scripts directory.
All launches use `run-isolated-graphics.py --socket-root ROOT/isolated-sockets`.
Fresh output directories/prefixes; no owned binary rebuild during these probes.

| Run under ROOT | Result |
| --- | --- |
| native-accel-egl-first | Native Wayland EGL1.5, Mesa25.0.7, GL4.1, D3D12 (Intel UHD Graphics750) |
| native-accel-electron-first | Real Electron44.9.2/Chromium152 lifecycle and frozen resize PASS, host0/diagnostic0 |
| native-accel-electron-profile | Repeat lifecycle/frozen resize PASS with bounded CPU/RSS profiling |
| native-accel-webview2-correctpath | Real WebView2155: strict WebGL1/2 default/FBO pixels PASS |
| native-accel-recovery-first | All10 GPU-child replacement/context/pixel/backend checks PASS |
| native-accel-boundary-x86_64 / -i686 | Own and Chromium-topology foreign-full DComp: exactly4096 front pixels each |
| native-accel-boundary-profile-x86_64 / -i686 | Same exact pixels after adding retained-path/CPU-Present diagnostics |
| native-accel-outer-gles2 | FAIL: wlroots cannot create GLES2 renderer without a DRM FD |

Electron's real GPU child maps WSL libd3d12/libdxcore and Wine's native Wayland
driver. This corroborates the native EGL context and configured backend; the
ANGLE-visible G45/G43 string is Wine's synthetic adapter identification, not
the physical adapter. Runtime child flags are recorded, not proof of functional
sandbox enforcement; no sandbox-bypass launch flags were added by our harness.

Actual independent compositor images inspected: frozen green page with blue
square and complete frame/menu, recovered page with WebGL content, and visible
WebView2 text/square/window frame. All5 frozen samples of the first accelerated
Electron run pass exact1600 blue pixels and full left/right/bottom rims. Runtime
payloads/captures remain local and untracked; no proprietary payload enters Git.

## Measured resource use (not an idle-power certification)

New opt-in `--resource-profile` samples only processes carrying the exact owned
Wine prefix, including the owned compositor, once per second. PID start ticks
guard reuse; process-churn intervals are counted/excluded from CPU averages.
Pure parser/accounting tests include process names with parentheses and PID reuse.
Current compositor/consumer/resource harness tests:22 PASS.

`native-accel-electron-profile/resources.jsonl` and results.json record:

- Active lifecycle:15.83 stable seconds,123.34% of one CPU core;2 churn intervals
  excluded. This includes injected lifecycle work and captures.
- Frozen settled page:39.23 stable seconds,13.54% of one core;0 churn intervals.
  Aggregate summed RSS peaks at2,623,434,752 bytes (~2.44GiB); shared pages are
  double-counted. This is not private allocation or GPU-memory usage.
- Frozen CPU breakdown: wineserver5.33%, renderer4.26%, GPU process2.63%,
  winedevice1.07%, browser0.25%; labwc0 measured ticks at100Hz resolution.
  RAF callbacks and one-second fixture observations still run with frozen DOM.
  Thus this is not a stopped-event-loop/idle-browser claim, nor proof of a new
  CPU defect. No production polling workaround was added.

The native producer now prints `FRAME_PATH` retained operation counters and
`PRESENT_CPU` API-call durations without changing drawing or pixel assertions.
Own-window premultiplied reference controls show160 CPU readbacks/161 CPU
compositions over160 frames (both PEs), not zero-copy; Present CPU total1703ms /
max15ms on x64,1570ms/max12ms on x86. Those small diagnostic scenes are not the
real Chromium image graph and CPU API return time is not GPU completion/scanout.
Real Chromium path counts and GPU stalls remain to be measured separately.
Foreign-full reference controls also show160 readbacks/160 CPU compositions:
x64 total819ms/max11ms, x86 total807ms/max10ms. These probes intentionally use
premultiplied swapchains; this does not establish the opaque GPU path is unused
by Chromium. No zero-copy/efficient-delivery pass is inferred from rendering.

## Outer compositor limitation / next checks

Outer rendering remains **headless pixman**. Explicit `--compositor-renderer
gles2` inventory exits1 with logs:
`drmGetDevices2 failed: No such file or directory`,
`Cannot create GLES2 renderer: no DRM FD available`.
The owned process is stopped afterward. Do not pass /dev/dxg as a fake DRM FD
or label client acceleration as outer GPU composition. Generic EGL fallback
can silently retry software; this is why the actual renderer string is checked.
See [Mesa EGL documentation](https://docs.mesa3d.org/egl.html) and
[D3D12 driver documentation](https://docs.mesa3d.org/drivers/d3d12.html).

Remaining scoped work: real-consumer composition counters/copy and completion
measurements, then an actual DRM-capable accelerated outer-compositor cell.
Read-only VM inventory found WineExplorer-VM running (leave Qwen alone),
ProtonExplorer-VM and WEX-Install-Test powered off, and /dev/kvm available.
No clone created. D: has about1.78TiB free. Proton root/build VDI copies would
cost about61GiB+153GiB; avoid blindly duplicating the build disk. The old install
test VDI is about10GiB but its installed state/SSH are not established. A future
disposable clone or fresh guest must have all storage/snapshots on D:.

## Failed commands retained

- Initial native consumer command rejected the old GLX-only guard (see scope).
- First patch of that guard failed `apply_patch verification failed`: argument
  order was `desktop, restart_from, gl_driver`, not `desktop, gl_driver,
  restart_from`. Re-read exact source and applied the narrow correction.
- `Get-Content scripts/graphics_probes.py` and
  `Get-Content scripts/check-g4-native-hwnd.py` failed (nonexistent paths).
  File inventory identifies `test_graphics_probes.py` and
  `check-g4-foreign-present.py`; no files were created under the guessed names.
- First WebView2 command used `--runtime ROOT/private-runtime/runtime` and was
  rejected before launch: `Required file missing: .../runtime/msedgewebview2.exe`.
  Read-only find located the existing versioned155 directory; correctpath passes.
- An early grep/tail of native-accel-electron-first processes.jsonl/host.log
  returned `No such file or directory` while wineboot was still initializing.
  Later completed files exist and show the verified runtime/GPU evidence.
- Unescalated `git diff --check` hit denied `.git/modules/wexcomp`; same-scope
  approved access succeeded. No checkout/reset or other branch edits.
- `rg ... docs/CHROMIUM_GRAPHICS*` on Windows returned os error123 for the literal
  wildcard path. Use `rg ... docs -g 'CHROMIUM*'` instead.

All completed consumer/boundary runs report clean owned Wine-server teardown.
No reset credit used; primary33% consumed at latest check. This is progress on
the scoped task, not a G4-complete or ready-to-merge statement.
