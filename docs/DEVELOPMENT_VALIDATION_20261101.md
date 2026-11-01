<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Unified development validation — 2026-11-01

**Accepted for the measured Chromium and desktop functionality.** The preserved
Wine v11 render bridge fixes an actual visible regression. No master/default
branch is changed. Development integrates the assembly/package fixes and pins
Wine b5e05301; compositor 94faa651 remains unchanged.

## Inputs and build

WineExplorer base 4a722b72; baseline Wine 67317644; preserved feature b5e05301.
An isolated D-backed Debian build uses normal project assembly and
--enable-archs=i386,x86_64 --disable-tests. The full production build completes,
including both PE architectures and the merged wexhost Unix bridge. The full
incremental build after selecting b5e05301 also exits 0.

The clean combined build found and repaired three packaging/assembly defects:

- Cross-filesystem stock-module preservation used os.rename, which failed
  between the Linux source checkout and Windows project backup. shutil.move
  preserves the existing pristine-module checks while supporting that layout.
- The selected clean Wine build lists omitted project modules and headers.
  Assembly now registers missing modules and headers idempotently, preserving
  optional arguments on existing configure declarations.
- The R6 runtime omitted the six Wine TrueType fonts. Packaging now requires
  them and dereferences build-tree font symlinks into actual archived files.

Fourteen packager fixtures pass on Linux without skips; the focused assembly
registration regression passes. The actual complete package also passes the
font checker. These are upstream Wine's freely licensed fonts, not host fonts.

## Actual A/B in the existing 8 GiB R4 VM

Both runs initialize a fresh prefix and new browser profile, use private
loopback/X sockets, the exact newly built compositor and actual VirGL, and run
normal Chromium with the original eight app/logging arguments. No browser
sandbox or feature bypass is used. Every one of 1,710 deployed runtime files
matches its expected hash. The feature runtime is an independent copy, with
exactly one changed file: dlls/winewayland.drv/winewayland.so.

| Observation | Wine development 67317644 | Preserved bridge b5e05301 |
|---|---|---|
| Fresh Wine initialization; 64/32-bit process startup | Pass | Pass |
| HTML and scene.js HTTP responses | 200 / 200 | 200 / 200 |
| Exact nonce fixture and WebGL1/2 readbacks | passed=true | passed=true |
| Actual screenshot | White Chromium content | Correct green background, blue marker, magenta/cyan canvases |
| Network service command | sandbox-type=network | sandbox-type=network |

This establishes the value of the bridge: JS/readback success alone concealed
missing front presentation. Parent inspected both actual screenshots.

The bridge run also passes visible taskbar/desktop detection, Start-menu
open/close, Notepad text roundtrip and maximize/restore, and File Explorer
opening C:\ with five listed items. The shell remains alive after Chromium.
The first probe incorrectly expected Progman; actual project desktop is
SHELLDLL_DefView. That probe assumption was corrected before the bridge run;
its other baseline GUI checks already passed. No desktop product change was
needed for that assertion.

The exact pinned compositor separately passes all 24 existing render-role
pixel/geometry stages, probe exit 0, with both owned processes stopped. This
is a pixman functional control, distinct from the actual GPU VM runs.

Owned servers stop/wait successfully. Final VM shutdown has QMP acknowledgement,
guest shutdown, QEMU exit 0, no fallback, and stopped QEMU/Xvfb. All prior
attempts, original runtimes and evidence are retained.

## Review and limits

Claude supplied the font-package edit and reviewed assembly. Its shell command
permissions prevented building/testing; parent performed all actual commands,
tests, artifact verification and runtime inspection. Qwen's review was checked
against the actual patch and wlroots ABI: current.width/height are explicitly
surface-local coordinates (buffer_width/height are separate), so its proposed
scale-division correction is incorrect. wlr_surface_set_role explicitly posts
an error on incompatible roles; the existing invalid-role pixel test passes.

A general transition from an already established local subsurface role to a
cross-process role remains a separate surface-lifetime case; the tested
Chromium producer starts with the cross-process role. Do not claim arbitrary
OpenGL reparenting conformance from this run. Normal network-service sandbox
mode is observed, but complete lowbox/MIC/broker isolation is not certified.
Testing covers a disposable Wayland desktop at 100% scale, not login/installer
acceptance, every shell applet, mixed-DPI monitors or all Chromium workloads.

Evidence: evidence/development-functional-20261101.json and
 evidence/development-render-role-20261101.json. Raw logs, original probes and
screenshots remain private under
D:/DeepThought/ChromiumLab/development-validation-20261101.
Build and candidate files remain under
/var/tmp/wex-development-validation-20261101 in D-backed Debian WSL.
