<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# L2: Native applications in Explorer

Status: implementation started. Accepted plan, 2026-10-14.

Explorer owns shell behavior and Wine-rendered non-client frames; wexhost bridges
host protocols; wexcomp owns composition, native surfaces and physical input.
Extend that existing split, rather than introduce dwm.exe. Honor client-side
decorations: Wine frames replace server decorations only, without duplicate
title bars. Wayland remains opt-in and shipping X11 keeps its backend.

## Checkpoints

1. Backend contracts, foreign-toplevel inventory/control, adoption and taskbar.
2. Atomic native-content/frame groups, geometry and logical/physical focus.
3. Recovery, application launch environment and complete regression acceptance.

Use wlr-foreign-toplevel-management for metadata and ordinary controls. Extend
the privileged wex-window protocol to v3 for missing geometry, decoration and
frame binding capabilities. Keep all v1/v2 requests. wexhost and Wine use separate
connections; associate views with compositor-generated lifetime tokens, never
foreign connection-local objects, titles, app IDs, pointers or process IDs.

Implementation versioning note: committed v3 supplies supplemental observation
and native endpoints. v4 adds the controller-lifetime handle and prepared frame
association without breaking v3 clients. PREPARED reserves identity/lifetime only;
v5 adds explicit active scene grouping and frame root render/input exclusion;
the synthetic suite passes with and without XWayland. Wine still binds v4 and
the bridge v3. Focus acknowledgements, resize/policy recovery and driver/bridge
hosting remain pending. See WAYLAND_L2.md for the verified checkpoint and limits.

Reuse proxy HWNDs and existing taskbar/window-state/dialog handling. Owned foreign
dialogs retain Win32 ownership but map as independent Wayland toplevels. Exclude
the host client area from frame rendering and input. Native buffers stay in the
compositor's normal scene graph; no pixel-copy hosting path. CSD/borderless proxies
have no visible frame. Preserve native popups and Wine frame-menu surfaces.

Logical proxy activation must not steal keyboard input from native content.
Compositor acknowledgements identify the actual focused associated view; retain
L1 activation-epoch guards. Frame menu and move/resize loops temporarily request
Wine keyboard focus, then restore native focus only if the proxy is foreground.
Separate observed states from pending requests to avoid configure/activation loops.

Use L1 driver scale/geometry conversions. Reuse existing size limits and dialog
placement, native fullscreen policy and graceful close. Binding failure or a
disconnected shell restores native decorations while the compositor is alive.
Full compositor/session termination keeps normal Wayland disconnect semantics.

Native launches use the normal Wayland socket and compositor XWayland display;
strip shell Wine variables. Tests use a private D-Bus session so activated apps
cannot escape into the production desktop. Icons reuse desktop-entry PNG lookup,
XWayland window icons and the existing generic fallback.

## Acceptance

- Synthetic SSD/CSD clients, foot, GTK, XWayland xterm/dialogs and independent
  Wine/Proton; one taskbar item per eligible toplevel and correct owners/icons/titles.
- Frame/client/taskbar activation, keyboard roundtrips, frame menus, minimize,
  restore, work-area maximize, topmost and fullscreen restoration; no feedback loops.
- Transparent content and input holes, popup trees, resizing and repaint without
  frame occlusion; placement within one pixel at 100% and 150% scaling.
- Existing/late windows, fixed/oversized dialogs, stale tokens, app exit and
  bridge/shell disconnection with the compositor independently running.
- L1 protocol regression, 32-bit applications and shipping X11/D1/D2 checks.

Global native hotkey forwarding remains L4; effects/capture/thumbnails remain L3.
Use separate Proton VM directories and WoW64 builds. Do not enter Gemini WSL,
reset assembled source, install in ~/inst/build, replace real sessions or include
private assets. Commit verified checkpoints and record every failure/correction
in WAYLAND_L2.md. User's .gitignore change remains untouched.
