<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Scoped task: native accelerated Chromium delivery

Starting point: parent23043c5f, Wine4315be8, compositor94faa651. Work only on
`codex/g4-r1-completion`, preserving other workers and installed sessions.

## Deliverable and sequence

1. Inventory native EGL/virtual-GPU availability on the owned private wexcomp
   endpoint. Add fail-closed native driver verification; do not reuse GLX proof
   or treat driver-selection flags as observed acceleration.
2. On a verified route, repeat actual Electron frozen-resize lifecycle and
   strict WebView2 WebGL1/2 pixels. Include32-bit regression coverage. Inspect
   compositor screenshots, not just internal readbacks or animation.
3. Record application rendering, retained composition/copies and outer desktop
   rendering separately. Measure bounded frozen/active CPU and memory, retained
   GPU/CPU path counters and practical stalls. No mandatory zero-copy claim.
4. If this WSL route cannot supply a required layer, document exact evidence
   and investigate a disposable accelerated VM. All disks/snapshots/storage D:;
   do not fill or reset the existing WineExplorer VM or alter Qwen's sessions.
5. Commit/push verified steps and an exact PASS/FAIL/BLOCKED/NOT RUN report.

Software fallback is a separate control, never accelerated acceptance. No
master merge, sandbox bypass, runtime payload publication, system-wide graphics
overrides or unrelated API/theme/security changes. Physical GPU matrix is not
required. Installed-shell/theme acceptance is a later task.

## Initial findings and command failures

- WSL has `/dev/dxg`, WSL D3D12 libraries and `eglinfo`, but no `/dev/dri`.
  Debian has660GiB free; no new VM or large copy required for inventory.
- Initial isolated `check-g4-webview2.py --gl-driver d3d12 --wexcomp-bin ...
  --lifecycle --lifecycle-visuals --resize-border` rejected before launch:
  `Native Wayland control cannot use X11 desktop/GLX driver verification or restart reuse`.
  Native GLX proof was deliberately prohibited; add actual EGL verification.
- `Get-Content scripts/test_graphics_wexcomp.py` failed because that file does
  not exist. `rg --files scripts` located `test_g4_wexcomp_harness.py`.
- `eglinfo --help` returned1 (`Unknown option: --help`) and printed usage;
  supported native options are `-B -p wayland -a gl` / `glcore` / `gles`.
- Read-only `ls /dev/dri` failed because DRM device nodes are absent; `/dev/dxg`
  was present. This is inventory, not proof acceleration is impossible.

Account usage initially26% consumed, five-hour reset1792643659; reset credit
untouched. At5% remaining finish bounded work, publish continuation notes and
schedule after reset if work remains. All probes must finish before rebuilds.
