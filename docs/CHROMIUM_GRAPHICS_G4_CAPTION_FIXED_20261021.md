<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 startup caption: diagnosed and repaired, 2026-10-21

The previously open headless startup caption defect is fixed by Wine `54bc44a`
on `codex/g4-r1-completion`. This is scoped X11/software evidence, not G4 merge
acceptance. No master merge, sandbox bypass, VM changes, proprietary payload/art
publication, or OOBE/token imports occurred.

## Cause and minimal fix

`X11DRV_WindowPosChanged()` applied the legacy window-region origin adjustment
to a client-side surface shape already expressed in drawable coordinates.
Backing GDI pixels and the Win32 region were correct, but the actual X11
bounding region was shifted and clipped the caption. Resize/restore could hide
the defect by replacing the shape; neither is used as startup acceptance.

Read-only Xlib/Xext inspection of the actual Electron HWND before/after:

| Property | Before | After |
| --- | --- | --- |
| X geometry | 25,25,740,520 | 25,25,740,520 |
| Bounding rectangle | 4,30,740,520 | 0,0,740,520 |
| Clip extents | 0,0,740,520 | 0,0,740,520 |

The five-line `dlls/winex11.drv/window.c` diff retains the legacy adjustment
only when `!client_side_graphics`, consistent with `sync_window_region()`'s
existing surface-shape distinction. No speculative win32u fix remains.

## Reproduction and verified results

Owned Debian combined-WoW64 build:
`/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64`.
Evidence root: `/var/tmp/wex-g4-tree-20261020`.
All runs use `scripts/run-isolated-graphics.py` with unique per-run filesystem
X sockets, fresh prefixes and no inherited live display. No rebuild occurred
while an owned probe was active.

| Directory under evidence root | Result |
| --- | --- |
| r1-caption-backing | Before fix: both PEs fail; backing pixel ffffff, screen black |
| r1-caption-xshape-fixed | Default theme, both PEs pass exact caption preservation |
| r1-caption-fixed-luna | Current source-built open Luna, both PEs pass; ca4205 pixel |
| r1-caption-fixed-classic | Managed/no host WM: both fail setup, not waived |
| r1-caption-fixed-classic-unmanaged | Classic with Wine-drawn frame: both PEs pass; fa9632 pixel |
| r1-caption-final-28-gl | All 28 correctness groups, both PEs, memory profile: pass |
| r1-caption-final-28-vk | All 28 groups, both PEs, explicit lavapipe, memory profile: pass |
| r1-caption-final-electron | Real Electron startup, no resize/fault, inspected screen-8.png and X shape; exit 0 |
| r1-caption-final-lifecycle-gl | Real Electron seven lifecycle checks pass; exit 0 |
| r1-caption-final-recovery-vk | One owned GPU-child termination/recreation: all ten checks pass; exit 0 |
| r1-caption-final-webview2-gl | Real WebView2 strict WebGL1/2 default/FBO pixel checks pass; exit 0 |

Caption tests check the baseline before D3D setup, then exact screen pixels
after plain GDI, graphics setup, first Present and four changing Presents.
The runner now explicitly configures and verifies Classic/open Luna in each
owned prefix, records the selected style hash, and rejects theme controls with
suites which do not propagate them. `--x11-unmanaged` is an explicit private
control, not a production setting change. Classic's managed/no-WM negative
control expects decoration from the missing host WM: its client-only GDI clip
was 4,30,252,252 and its caption backing read CLR_INVALID. Unmanaged Classic
has a full GDI clip and passes. Installed managed Classic still needs a real WM.

Example (append the flags on the same command line):

```text
python3 -B scripts/run-isolated-graphics.py --socket-root /var/tmp/wex-g4-tree-20261020/isolated-sockets -- python3 -B scripts/check-g3-correctness.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --output-dir /var/tmp/wex-g4-tree-20261020/UNIQUE --nonclient-diagnostic --theme-mode classic --x11-unmanaged
```

Open Luna instead uses `--theme-mode open-luna --theme-style
/var/tmp/wex-g4-tree-20261020/caption-current-open-luna/Luna.msstyles` without
the unmanaged flag. Built with `scripts/build_msstyles.py` from the current
`src/resources/themes/luna-open`, not the older Windows build artifact.

Identity SHA-256:

- `dlls/winex11.drv/winex11.so`: `442fa2d447b6065016a40d7b0d6557124b9cdd69d9c805285dc5d6fd20915238`
- `dlls/win32u/win32u.so`: `f450466f4a2d7584051de1cc9bce99436aff84813632f506ef876afcfad62dce`
- current open Luna style: `7202b8a1e04dc5d7cc1fb2e67617252f739095be4b3525974a14356bb9c0c628`
- private Electron executable: `448da7f0a402b7299f87dc7dc896d48fd58a726ccbcf65b69820745f1bd38bf2`

The consumer runner now includes the X11 Unix driver in stack hashes, so future
restart comparisons reject changed drivers. The final WebView2 run records
the new driver hash in its own JSON. The three Electron runs above
preceded this manifest improvement; their driver identity is recorded here,
not retroactively inserted into their JSON. Four new theme-harness unit tests,
nine consumer-harness tests and four graphics-harness tests pass.

## Rejected edits and failed commands (retained diagnostic history)

- `rg` probes for `wine/dlls/win32u/nonclient.c` and
  `wine/dlls/uxtheme/nonclient.c` failed: those paths do not exist. Actual code
  is `win32u/defwnd.c` and `uxtheme/window.c`.
- `r1-caption-trace` and `r1-caption-backing` failed both PEs before the fix.
  `+nonclient`/`+dce` did not provide the expected painting channel;
  `+win,+bitblt,+x11drv` and backing/screen/region probes distinguished it.
- Trial win32u/dce.c newly exposed-clip dirtying failed in
  `r1-caption-clip-fix-gl`/`-vk`. Full-bitmap dirtying on clip change failed in
  `r1-caption-fullclip-fix`; the corresponding Electron animation ran but the
  frame stayed absent. Both edits were removed and win32u rebuilt. None is
  committed, and animation is not visual acceptance.
- PowerShell `rg` arguments `scripts/check*`, `scripts/build*`,
  `docs/CHROMIUM_GRAPHICS_G4*` and `Makefile*` were not shell-expanded and
  produced invalid filename errors. Use directories with `-g` filters.
  Root `CMakeLists.txt` also does not exist; the actual theme build entry is
  `scripts/build_msstyles.py`, called by `installer/package-wine.sh`.
- Unescalated Git diff checks hit `.git/modules/wexcomp`/`wine` permissions;
  scoped elevated checks succeeded. No metadata repair/reset was attempted.
- Private Xlib inspection initially assumed the Python script was argv[1];
  `python3 -B` puts it at argv[2]. Fixed that owner selector. `nsenter ...
  xwininfo -root -tree` failed 127 because xwininfo is absent; replaced with
  read-only ctypes Xlib/Xext inspection, installing nothing. A later inspection
  after the 45-second run finished correctly found no owner; reran a bounded
  live test. No unrelated process was selected or killed.
- `sha256sum .../dlls/winex11.drv/x86_64-unix/winex11.drv.so` failed: this build
  uses `dlls/winex11.drv/winex11.so`; `find` resolved it and hash above succeeded.

## Remaining scope and continuation

Startup caption repair is complete for these tested configurations. Generic
deferred HWND integration, remaining R2 fault/lifetime and R3 efficient sharing
gates are unchanged. The inherited DXGI 67 failures and D3D11 legacy-sharing
exit 5 remain open, not rerun or reclassified by this change. Actual installed
Classic/open Luna shell, taskbar/toolbars/tray, host compositor off/on, Wayland
and physical old Intel/AMD/Nvidia acceptance remain unproven; WSL lacks /dev/dri.
No new claim of hardware acceleration, security isolation or G4 merge readiness.
Do not disturb Qwen's OOBE work or the existing VirtualBox VM.
All owned runs have exited; final Debian process inventory shows no Wine,
wineserver, Xvfb or graphics test runners remaining. Private caches/evidence
remain in the named lab; no VM/session/login/network settings were changed.
