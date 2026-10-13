# Work order: Wayland L1 — Wine places and stacks its windows under our compositor

Status: ready, 2026-10-13. **Split:** tasks 1–3 (compositor) are assigned to Gemini in
`docs/WAYLAND_COMPOSITOR_WORKORDER.md`; tasks 4–6 (Wine driver, Explorer) are Codex's, after the refactor's P4.
Background: `docs/WAYLAND_PLAN.md` (design, prior art), `docs/WAYLAND_L0.md` (spike
results and screenshots). The X11 session stays the shipping stack; nothing here changes it.

## Goal

Under our own Wayland compositor, Wine's Wayland driver puts every top-level window where Win32 says and stacks it as
Win32 says, so our shell (`explorer /shell`) works: desktop at the bottom, taskbar at the bottom edge and above
windows, dialogs and probe windows at their requested positions. Measured by re-running the L0 matrix.

Out of scope (later phases): frames for Linux app windows (L2), shadows/fades/capture in the compositor (L3), lock
screen, greeter, display settings, multi-monitor (L4).

## Decisions

- **Compositor = a fork of labwc**, pinned to the version Debian 13 ships with its wlroots: **labwc 0.8.3 on wlroots
  0.18.2** (`libwlroots-0.18-dev` 0.18.2-3). Repository: the user forks `labwc/labwc` to their GitHub account (ask
  before creating it); work on branch `wexcomp`, one commit per change on top of the `0.8.3` tag. The compositor is a
  separate program and stays GPL-2.0-only (labwc's license). Working name **wexcomp** (binary may stay `labwc` until a
  rename is worth it).
- **Positions: `xx-zones-v1`** (wayland-protocols, experimental, merged 2026-02) — the standard protocol. Its use in
  Wine's driver is meant to go upstream.
- **Stacking and roles: a small private protocol `wex-window-v1`** (xx-zones has no stacking). Design reference: KDE's
  plasma-shell / plasma-window-management XML (plasma-wayland-protocols). This part stays in our Wine fork branch.
- Wine side follows `docs/REFACTOR_WORKORDER.md`: upstreamable work on `upstream/<topic>` branches of
  `zcomputerwiz/wine`, our private part as commits on `wineexplorer/<version>`. If that refactor has not landed yet,
  develop in `wine/` here and move the commits when it does.

## Tasks

### 1. Compositor: xx-zones-v1 server
- Vendor `xx-zones-v1.xml` from wayland-protocols (record the commit) into the fork; generate with wayland-scanner.
- One zone per output covering the output's usable area; `size` = output logical size; `get_zone` with a preferred
  output; `get_zone_from_handle` shares a zone (every Wine process of a session must land in the same zone: use the
  `handle` event, passed to child processes through the environment, e.g. `WINE_XX_ZONE_HANDLE`).
- `set_position` places the toplevel at that zone position (frame extents: Wine draws its own frames, so extents are 0
  unless server-side decoration is on); send `position` back; `position_failed` only when the window would leave the
  zone entirely.
- Interaction with labwc's placement: a window in a zone with a requested position skips labwc's placement policy.

### 2. Compositor: `wex-window-v1`
Write `protocol/wex-window-v1.xml` (GPL-2.0-or-later or MIT — MIT preferred for a protocol). Minimum:
- `get_window(xdg_toplevel)` → `wex_window_v1`;
- `place_above(sibling)` / `place_below(sibling)` / `place_bottom` — stacking relative to another Wine window;
- `set_layer(enum: normal, topmost, desktop, shell)` — `desktop` under everything, `topmost` band above normal
  windows (`WS_EX_TOPMOST`), `shell` for the taskbar (topmost + excluded from labwc's own window cycling);
- `activate` — focus a window Wine activates itself (Wine keeps `SetForegroundWindow` rules);
- `set_work_area` or an exclusive zone for `shell` windows anchored to an edge, so maximized windows stop at the
  taskbar (mirror layer-shell's exclusive zone semantics).
- **Trust**: offer the global only to the shell session's clients. Simplest: a second listening socket
  (`WAYLAND_DISPLAY` for the session's Wine is that socket) that the compositor marks privileged; document the choice.

### 3. Compositor: missing standard globals from L0
- `wp_alpha_modifier_v1` (wlroots 0.18 has `wlr_alpha_modifier_v1`; wire it to the scene) — uniform layered alpha.
- `wp_pointer_warp_v1` (not in wlroots 0.18; small; implement in the fork) — `SetCursorPos`.
- Optional: `xdg_toplevel_icon_v1` (window icons in the compositor's own UI).

### 4. Wine: Wayland driver uses xx-zones (upstreamable, `upstream/winewayland-zones`)
- In `winewayland.drv`: bind `xx_zone_manager_v1` when offered; join/create the session zone; make each toplevel a
  zone item; on map and on every position change (`WAYLAND_WindowPosChanged` already has the Win32 rect) send
  `set_position` in zone coordinates; handle `position`/`position_failed`/`item_left`.
- Fall back to today's behavior when the compositor lacks xx-zones.
- Wine style, tests where possible (the driver has few unit tests; add a manual test note in the MR text instead),
  ready-to-paste MR description in `docs/UPSTREAM.md`. Do not submit; the user opens MRs.

### 5. Wine: `wex-window-v1` in the driver (fork-only commit)
- Bind when offered; map the Win32 z-order to `place_above`/`place_below` on `WindowPosChanged` (the `insert_after`
  argument), `WS_EX_TOPMOST` to `set_layer(topmost)`, the desktop window to `desktop`.
- Activation: call `activate` from the driver's `SetForegroundWindow`/activation path.

### 6. Explorer: shell without a virtual desktop
- `explorer /shell` without `/desktop=` currently treats `/shell` as a folder path (`Failed to create PIDL for
  Z:\shell`). Make the switch work on the driver's real desktop; the taskbar asks for `shell` layer + bottom-edge
  exclusive zone; the desktop window asks for `desktop`.
- `NtUserChangeDisplaySettings` failing on the Wayland desktop (`-2`) must not stop the shell: skip mode setting when
  there is no virtual desktop.

## Environment

Proton VM (build box). Install build deps: `meson`, `ninja-build`, `libwlroots-0.18-dev`, `wayland-protocols`,
`libxkbcommon-dev`, `libinput-dev`, `libxml2-dev`, `libpango1.0-dev`, `libcairo2-dev`, `libglib2.0-dev`,
`scdoc` (labwc's build list). Test nested exactly like L0: `scripts/wayland-spike-env.sh` (Xvfb `:9`,
`WLR_BACKENDS=x11 WLR_RENDERER=pixman`), with the fork's binary instead of Debian's labwc. Add a headless run
(`WLR_BACKENDS=headless`) with screenshots via screencopy (`grim`) for automation. Never use the dev VM's genuine
overlay; separate prefix (`~/l0-prefix`).

## Acceptance

Re-run the L0 matrix (`docs/WAYLAND_L0.md`) with the fork and record a new table in `docs/WAYLAND_L1.md`:
- compose-probe windows appear at their requested positions (compare against the same probe on X11 Xvfb; positions
  within 1 px);
- `explorer /shell` without `/desktop=`: desktop under everything, taskbar at the bottom edge above normal windows,
  maximized windows stop above the taskbar, Start menu opens from the taskbar;
- clicking the desktop does not raise it over the taskbar or windows;
- 50% uniform alpha window is translucent; `SetCursorPos` moves the pointer;
- menus, keyboard, per-pixel alpha, and foot/XWayland apps alongside still pass (no regressions from L0);
- the driver still works unchanged under stock labwc (no xx-zones/wex-window) and under KWin if available.

Commit after each working step; GPL headers on new files; never pass C escapes through heredocs/sed/`python -c`.
