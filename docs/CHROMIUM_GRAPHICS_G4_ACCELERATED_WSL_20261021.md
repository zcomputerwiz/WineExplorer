<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Accelerated virtual-GPU validation — 2026-10-21

The user clarified that functional hardware-accelerated virtual GPU drivers are
sufficient: direct physical-GPU testing is no longer a merge prerequisite. This
revises the older physical Intel/AMD/Nvidia matrix requirement; it does not
waive rendering correctness, desktop integration or silently allow software
fallback. Any fresh testing VM/disks must reside on host D:, not C:.

## Newly available accelerated path

Default Debian WSL GL was llvmpipe, despite `/dev/dxg` being available.
Explicit process-local selection:

```text
GALLIUM_DRIVER=d3d12
LD_LIBRARY_PATH=/usr/lib/wsl/lib
LIBGL_ALWAYS_SOFTWARE=0
```

`glxinfo -B` on WSLg and independently on our owned isolated Xvfb reports:
Mesa 25.0.7, `Accelerated: yes`, renderer
`D3D12 (Intel(R) UHD Graphics 750)`, GL 4.1 and GLES 3.0.
This is Mesa GL -> WSL D3D12 GPU virtualization, not native Linux DRM/Vulkan,
not a direct physical GPU test and not a VirtualBox VMSVGA result.
The absence of `/dev/dri` alone therefore does not establish that WSL cannot
accelerate GL. Previous software results remain software results, not upgrades.

Wine's ANGLE-visible adapter string is synthesized (`Intel G45/G43`, shader
model 4.1 in WebView2); do not infer the actual adapter from that string.
The live Electron graphics process maps `/usr/lib/wsl/lib/libd3d12.so`,
`libd3d12core.so`, and `libdxcore.so`. These process observations plus actual
pixel tests support use of the accelerated route, rather than relying on flags.

## Reproducible harness changes

`check-g3-correctness.py` and `check-g4-webview2.py` now accept the explicit
`--gl-driver d3d12` control. Software remains the default. Driver configuration
clears inherited Gallium/loader overrides and sets the WSL library path only
for this opt-in run. No installed configuration or user prefix is changed.
The accelerated option requires GL, captures owned-display `glxinfo` logs,
requires a D3D12 renderer and acceleration=yes, and rejects llvmpipe/softpipe/
SwiftShader or failed probes before launching Wine. JSON records the selection,
environment and verification. Restart state cannot cross software/D3D12 runs;
consumer process-map logs now include the D3D12/DXCore/Gallium libraries.
The full-suite child does not propagate this driver mode, so that combination
is explicitly rejected rather than mislabeling its results.

No production Wine edit: still `54bc44a`, same owned combined-WoW64 build and
binary hashes as CHROMIUM_GRAPHICS_G4_CAPTION_FIXED_20261021.md. No build occurred
during probes. Each uses a fresh prefix and unique isolated filesystem X socket.

Evidence root `/var/tmp/wex-g4-tree-20261020`:

| Directory | Result |
| --- | --- |
| r4-d3d12-28 | All28 groups, both x64/i686 PEs, memory profiling: PASS |
| r4-d3d12-webview2 | Real WebView2 strict WebGL1/2 default/FBO red pixels: PASS |
| r4-d3d12-electron-lifecycle | All seven real Electron input/window/reload checks: PASS |
| r4-d3d12-caption-luna | Current source-built open Luna, both PEs, exact caption pixel preservation: PASS |
| r4-d3d12-caption-classic | Explicit unmanaged Classic, both PEs, exact caption pixel preservation: PASS |
| r4-d3d12-electron-recovery | All ten owned GPU-child fault/recreation/pixel checks: PASS |

Electron `screen-8.png` was visually inspected: title bar/frame, menu, text,
moving square, input and clicked button are visible. This supplements lifecycle
and pixel assertions; animation alone is not visual acceptance. This screenshot
is during input lifecycle, not a new no-action startup proof (prior report has
that separate control). All runtime payloads and captures remain private/local.
The separate GPU-recovery run preserves the renderer process and backend,
observes context-loss events and one expected owned GPU-child exit, then reads
correct WebGL1/2 default/FBO pixels after recreation. It is not normal-startup,
repeated device-reset or sandbox-containment certification.

Command skeleton (append suite/runtime flags as documented in the prior report):

```text
python3 -B scripts/run-isolated-graphics.py --socket-root /var/tmp/wex-g4-tree-20261020/isolated-sockets -- python3 -B scripts/check-g3-correctness.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --output-dir /var/tmp/wex-g4-tree-20261020/UNIQUE --gl-driver d3d12 --g4-correctness --memory-profile
```

Six theme/driver harness tests, nine consumer harness tests (including driver
mismatch rejection), and four generic graphics harness tests pass.

## Environment checks and failures

- `VBoxManage showvminfo WineExplorer-VM --machinereadable` reports VMSVGA,
  128MB VRAM, 3D enabled, existing SSH port127.0.0.1:2222. SSH through `localhost`
  failed with `banner exchange: Connection to UNKNOWN port -1: Connection
  refused`. Explicit `ssh -F NUL ... wineuser@127.0.0.1` succeeds without any
  guest/host/NAT/service changes. Guest root remains97% used,1.9GB free; no
  glxinfo executable was found. Do not fill it with another build/prefix.
- The WSL inventory compound command returned1 because optional `/dev/dri`,
  Weston, Xwayland, openbox/xcompmgr paths were absent; `/dev/dxg` and WSLg's
  Wayland socket do exist. A separate explicit GL probe succeeded. WSLg's
  presence is not an actual Wine Wayland/wexcomp acceptance run.
- A read-only log command ended1 trying `r4-d3d12-webview2/host.stderr.log`,
  which the harness does not create. `host.log` and `results.json` contain the
  actual diagnostics; final consumer exits and strict checks pass.
- No new VM was needed for this accelerated cell. If a fresh VM is needed for
  installed desktop tests, put its registration directory, disks and snapshots
  on D:. Do not change existing VM sessions or interfere with Qwen.

## Remaining gates

The accelerated WSL GL rendering cell now passes. Do not claim full R4 or merge
readiness: installed shell Classic/open Luna, locked/unlocked taskbar, toolbars,
tray chevron, managed Classic with an actual host WM, X11 compositor OFF/ON and
actual Wine Wayland/wexcomp remain untested here. R2/R3 limitations and inherited
upstream failures remain as documented. No physical matrix is required under
the clarified user criterion; no new blanket hardware/performance guarantee.
Final Debian process inventory confirms no owned Wine/wineserver/Xvfb/test
processes remain. Existing VirtualBox VM is unchanged and remains running.
