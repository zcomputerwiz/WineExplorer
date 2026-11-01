<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native wexcomp presentation gate — 2026-10-21

**OPEN: a real native process-boundary presentation failure is reproduced.**
Wine54bc44a and pinned wexcomp acc46fe619ef1dd917ffd0b0aaf66ec13083290b
remain unmodified. Tests now run on our actual compositor/private shell
protocol, not Openbox. No existing VM, other worker checkout or OOBE/token
work was changed. No master merge or G4-complete claim.

## Owned compositor/build

Staged the clean pinned wexcomp sources in
`/var/tmp/wex-g4-tree-20261020/wexcomp-pinned-acc46fe`; built in
`.../wexcomp-build-acc46fe` with installed wlroots0.18.2 and normal enabled
Xwayland/icon features. Meson --wrap-mode=nodownload initially correctly failed
because libsfdo was absent. Explicitly cloned its declared v0.1.3
(`b3936ed9826375334dbbaa999e7216d1b36b445b`, BSD-style licence inspected) into
the owned staging subproject; identical Meson setup then succeeded. Ninja -j6
build PASS. Executable SHA256:
`e49b081e4139898897d8924371ae084281d7eb76169a94e6563f7feffa0a0a09`.
The earlier clean-source/wexcomp directory was empty; its subprojects listing
failed ENOENT, so it was not used as a compositor source tree.

Installed only Debian WSL testing packages xwayland, grim and wayland-utils
(about937kB downloads/2.65MB installed). No login session/VM was switched.

New graphics_wexcomp.py owns a fresh0700 runtime, headless pixman compositor,
project scripts/wayland config, and wex-shell-0 endpoint. It strips inherited
display/Wayland FD/DBus/DRM device settings for compositor startup, does not
update the user's DBus session environment, verifies actual private
wex_window_manager_v1 advertisement, and stops only its own process.
Headless mode logs a preferred-mode test failure but commits a working1280x720
output; wayland-info and successful grim captures verify it is not a blank
unconfigured output. This is a software compositor/backend control, not an
accelerated Wayland or installed-shell acceptance claim.

check-g4-webview2.py now supports --wexcomp-bin/--wexcomp-config, forces native
winewayland (X11 disabled), captures real compositor outputs via grim and
checks process mappings for winewayland.so without winex11.so. Incompatible
X11/GLX/restart options are rejected; existing Xvfb controls remain separate.
Both required Wayland protocol and native driver are actually observed.

## Real consumer negative evidence

Evidence root `/var/tmp/wex-g4-tree-20261020`:

- r4-wexcomp-native-vk-first: Electron60 seconds, Vulkan/lavapipe, eight held
  lifecycle captures; all API/RAF checks pass but front client is white/blank
  at ready/restored and absent at reload in inspected captures.
- r4-wexcomp-native-gl-first: same consumer using software GL, same blank front
  observations despite API progress. It is not Vulkan-only or an Openbox bug.

The runner intentionally records MANUAL_REVIEW_REQUIRED; exit0 means the API
and capture control completed, not that these visually blank windows passed.
Prior API-only acceptance is insufficient. All captures/runtime payloads remain
private/local, never committed or bundled in the ISO.

## Independent actual-pixel process-boundary reproducer

Original test_g4_foreign_present_probe.cpp + check-g4-foreign-present.py create
a white300x300 window, render an opaque64x64 red DComp image, and inspect real
grim pixels independently of Wine GetDC(NULL). The foreign case uses a
producer-owned rendering child under a browser-process-owned top-level, matching
the important process topology rather than directly targeting a foreign HWND.
The independent green foreign-HWND GDI control is also retained.

| Control | Own DComp | Foreign-root DComp child | Foreign GDI |
| --- | --- | --- | --- |
| r4-native-foreign-child-vk-x64 | PASS4096 red pixels | FAIL0 red pixels | FAIL0 green pixels |
| r4-native-foreign-child-vk-x86 | PASS4096 red pixels | FAIL0 red pixels | FAIL0 green pixels |
| r4-native-foreign-child-gl-x64 | PASS4096 red pixels | FAIL0 red pixels | FAIL0 green pixels |

Every final producer submits frames and exits0, including the invisible cases;
the harness returns1 for failed front pixels and records them, not a false pass.
This establishes the process boundary independent of Chromium. The initial
r4-native-foreign-vk-x64 diagnostic instead targeted a foreign root directly;
its DComp setup failed before any frame and the first harness stopped before
GDI. This was not the Chromium topology. Corrected the diagnostic to own the
rendering child and record nonzero setup failures without suppressing later
controls. No production behaviour was changed to obtain these results.

Relevant implementation: wayland_client_surface_attach() in Wine's
winewayland.drv/wayland_surface.c looks up the top-level in the current process
before obtaining a wl_subsurface; on absent local top-level it detaches. GPU
and UI processes do not share these driver-owned wl_surface objects.
The driver present callback also cannot manufacture that missing local parent.
Do not infer a working cross-process presentation bridge from successful Present
or increasing RAF counters. Foreign GDI likewise is not a ready-made fallback.

## Next implementation slice

Provide an explicit process-boundary render-surface attachment in our private
wexcomp/Wine interface, not a Chromium sandbox bypass or single-process mode.
Use the existing unpredictable parent-window handle, private endpoint/peer
checks and prefix-local Wine property exchange; validate all sizes/coordinates.
The compositor must attach producer-owned buffers to the correct frame subtree,
inherit parent visibility/minimize/clip/position, preserve frame and input, and
detach safely on parent/producer/window/device destruction. Stock/nonmatching
compositors must not falsely report a successful visible attachment.

Prove own/foreign pixels both PEs and both backends, wrong/stale parent rejection,
producer-before-parent and parent-before-producer cleanup, retained movement/
resize/hide/restore, and actual Electron/WebView2 stage screenshots after the
bridge. Do not broaden privileges or assume generic foreign GDI is fixed.
After that, resume installed Classic/open Luna/taskbar/toolbars/tray acceptance.
Four owned-compositor lifecycle/mock tests and ten consumer-parser tests PASS.
All runs have exited and owned servers/compositors are stopped; no VM changes.

Commands use run-isolated-graphics.py, the same combined-WoW64 build as earlier
reports and fresh per-run socket/output directories. No rebuilding during a
probe. New VM storage must be on D:. One-time reset credit remains untouched.
