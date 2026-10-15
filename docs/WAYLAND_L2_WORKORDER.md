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
the synthetic suite passes with and without XWayland. V6 supplies epoch-tagged compositor focus acknowledgements and
guards late frame-menu focus restoration; actual key delivery passes in both
compositor builds. V7 numbers group configure/uncommit completions. Wine now
consumes v7 under explicit hosting opt-in, with 32/64 tests for foreground
retention, temporary frame focus and resize mismatch recovery. Native size or
policy changes detach safely. wexhost now binds up to v7 and implements explicit
SSD HWND binding/lifetime/hit exclusion with 32/64 real API probes. Explorer's
explicit lab gate now passes actual 64/32 shell adoption, observed-state handling,
native click/rendering, real menu focus, two-way minimize/restore and withdrawal.
Real task-button activation/minimize/restore, visual active-state transitions,
and context-menu cancellation/Minimize/Restore/Close now pass with both actual
Explorer architectures. Initial owned SSD adoption, parent removal/reassignment,
owner frame withdrawal/return and native parent closure now pass with the same
child proxy surviving. Broader transient/application coverage remains.
Native resize negotiation and complete acceptance remain pending.
Private v8 now supplies acknowledged frame pointer capture. Real 64/32 shell
tests pass menu Move/Enter, Escape rollback, title-bar dragging and explicit
capture cancellation, including final native geometry/pixels/focus. Full
compositor suites pass 38/38 with and without XWayland. Native size negotiation
and the current theme-region restoration issue remain separate work below.
AUTO is still disabled. See WAYLAND_L2.md for limits.

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

## Next: native resize negotiation

Before enabling resize/maximize UI, repair the bridge's region bookkeeping:
the bind-time original region is a restoration snapshot, not a substitute for
the current theme's outer frame shape after resize or theme changes. Test both
the current rounded caption and client hit exclusion while bound, plus release.

Keep the existing ownership split. Wine supplies Win32 sizing loops, scalar
properties and DPI conversion; wexhost tracks requests separately from observed
native state; wexcomp enforces native size constraints and owns xdg configure /
buffer-commit completion and atomic scene changes. Use the v8 capture path for
move/size input, not a new window-manager process or class-name special cases.

The next protocol change must distinguish desired size, client configure
acknowledgement and actual committed buffer size. Account for min/max constraints,
fixed-size clients, client-selected sizes, delayed/ignored configures and Escape
rollback. Older completions must not override a newer request. Keep rendering
and input coherent while the native buffer and Wine frame catch up; do not
turn the current detach-on-mismatch safety path into resize flicker. Add bounded
recovery for a non-responsive client before exposing sizing affordances.

Acceptance must include both Explorer architectures, all edges/corners, commit
and cancellation, capture/focus release, constraints, live theme-region changes,
native pixels/input holes, then 100%/150% placement and actual GTK/foot clients.
Stock labwc, old private protocol peers and shipping X11 retain their behavior.
Resize/maximize remains disabled in the lab gate until these checks pass.

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
