<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Accelerated outer compositor: actual consumer checkpoint

Branch `codex/g4-r1-completion`, parent408b2aa5. Wine4315be8 and
wexcomp94faa651 remain unchanged. No merge or other worker imports.
The preceding accelerated-VM report proves DRM/GLES2 startup and24 native
SHM pixel/lifetime controls, **not Chromium readiness**. Actual consumers fail.

## Reproduction and evidence

Only disposable `WEX-G4-Acceleration-20261022` was used, SSH2234/debian.
All disks, payloads and host evidence live in
`D:/VirtualBox_VMs/WEX-G4-Acceleration-20261022/`.
Guest lab `/home/debian/wex-g4`; combined-WoW64 build retained at
`/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64`.
Original private runtime caches and original WSL build remain intact.

Explicit tests now support `--gl-driver svga --compositor-renderer gles2`.
They check the outer compositor's own accelerated renderer and native EGL
client independently, reject software fallback, and keep private prefixes.
Run using `run-isolated-graphics.py` unique filesystem X sockets and offline
network namespace; root bootstrap drops to debian with setpriv/init-groups.
Default renderer remains pixman; existing software/D3D12 paths are unchanged.
33 Wine-free theme/compositor/consumer/resource unit tests PASS.

Guest runs, each a fresh output directory/prefix:

| Directory | Actual outcome |
| --- | --- |
| consumer-gpu-electron-ready | Electron44.9.2 host0 but diagnostic4 FAIL; held front samples have blue0 despite caption/frame; scroll/restore/reload incomplete; RAF progress is not acceptance |
| consumer-gpu-webview2-first | WebView2 host0 but diagnostic4 FAIL; strict WebGL1/2 unavailable, GPU crashes/fatal errors |
| consumer-gpu-boundary-i686-first | Both own/foreign DComp controls fail device creation, zero pixels |
| consumer-gpu-device-caps-trace | x64 own DComp fails, trace proves maximum feature level0x9300 |
| consumer-gpu-mesa26-device | Same x64 failure after supported Mesa upgrade, maximum still0x9300 |

The first Electron sample used concurrent probes: its resource observations
are NOT a serial performance baseline. Test exit0 alone is not a pixel PASS.

## Distinct blockers

1. SVGA exposes GL4.1 but lacks ARB/EXT_polygon_offset_clamp. Wine's
   `dlls/wined3d/adapter_gl.c` feature-level gate requires that capability for
   feature10+. Both extension aliases are already recognized. The actual trace
   selects9.3; our probe requests10+, so creation fails. Do not spoof the extension,
   lower the assertion or claim that host D3D11 means guest Wine supports it.
2. Real WebView2 reports `importing the supplied dmabufs failed`, then the
   Wayland read thread loses the compositor. wexcomp/wlroots reports
   `Failed to close buffer handle: Invalid argument`. vmwgfx-specific surface
   handle semantics are a plausible cause, not yet a demonstrated patched fix.
   Kernel UAPI documents a separate DRM_VMW_UNREF_SURFACE ioctl:
   [upstream header](https://github.com/torvalds/linux/blob/master/include/uapi/drm/vmwgfx_drm.h).
   Do not suppress validation, leak handles or count recovery as correct rendering.

Debian's already-configured official trixie-backports was tested first:

```sh
sudo env DEBIAN_FRONTEND=noninteractive apt-get -y -t trixie-backports --no-install-recommends install libgl1-mesa-dri libegl-mesa0 libgbm1
```

Eight Mesa packages upgraded25.0.7 to26.1.6-1~bpo13+1, no removals (~58MB
growth); kernel stays6.12.113. No probes ran during installation. EGL remains
SVGA3D/GL4.1. Fresh device trace still fails: newer Mesa is not a solution here.

## Private staging and dependencies

Owned WoW64 package563326192bytes SHA256
`91b1b12efa0709bca19824dea955f9cc0a28fd6514a6f3f33baa302943a8dc33`;
private consumer package470752463bytes SHA256
`cc11cdd22cef90ccaeb1fb2fe3b4021d0bd89e7f36945925cdba6a86378287d6`.
Both verified after transfer. No proprietary payload is tracked/published.
Wine nls/fonts and internal symlinks were preserved; no WSL graphics libraries
were copied. Exact compositor binary SHA256 remains
`4a24dd13fed3d733d4edd6f213e4db284ff0dee9229e5df898d694d4446e0ec8`.
Guest runtime dependency libxkbregistry0 and both MinGW compilers installed.
Guest root has ~43GiB free; no shared user Wine installation replaced.

## Alternate backend feasibility, not acceptance

`scripts/probe-g4-virgl-host.py` starts a diskless, paused, networkless QEMU/KVM
with virtio-vga-gl/GTK on an owned Xvfb. Explicit WSL D3D12 GLX is accelerated
on Intel UHD750. QEMU survives5seconds and both owned processes stop cleanly.
Evidence: D: VM directory `evidence/virgl-host-inventory`.
**No guest DRM, guest VirGL capabilities or Chromium pixels proven by this.**
Next investigate a fresh D:-backed QEMU overlay with virtio_gpu, rather than
inventing Wine caps or broad driver patches. Preserve VirtualBox negatives.

## Failed commands and diagnostic repairs retained

- `Get-Content scripts/test_g3_theme_harness.py`: absent; rg located actual
  `test_g4_theme_harness.py`.
- `file .../tools/wine/wine`: WSL file utility absent. Following head printed
  public ELF bytes, not source; read tools/wine/wine.c instead, preserve symlinks.
- First multi-file apply_patch context mismatch on one-line wexcomp-config;
  no partial change, reread exact source and apply corrected patch.
- Unescalated Git status/diff: .git/modules/wexcomp Permission denied;
  approved same-scope Git access works.
- Initial consumer-gpu-electron-first: Wine Wayland driver cannot load
  libxkbregistry.so.0. SIGINT only owned Python PID1153; finally cleanup ran.
  Install libxkbregistry0, rerun fresh. KeyboardInterrupt is intentional stop,
  not a demonstrated renderer deadlock.
- Bare ldd also reports ntdll.so/win32u.so absent; supplying Wine's own private
  loader paths resolves those. Not additional missing system packages.
- Reading own-dcomp.stderr.log before wineboot completed: No such file;
  later file exists and supplies the actual feature-level trace.
- rg of wined3d/unixlib.c: nonexistent file, no missing shader compiler proven.
- wlroots GitLab issue3850: Anubis Access Denied; Debian source and Mesa source
  web fetches inaccessible. Do not claim their implementations were inspected.
- Diskless KVM timeout3seconds exit124 is expected bounded shutdown, not failure.

No Wine/compositor production fix or G4 completion claim in this checkpoint.
Existing WineExplorer-VM and Qwen sessions remain untouched. Private test outputs
stay out of Git; current machine/power state is recorded in the next continuation
entry when the alternative guest check finishes.
