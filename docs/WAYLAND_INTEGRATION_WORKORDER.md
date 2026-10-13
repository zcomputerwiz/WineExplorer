# Work order (Codex): Wayland L1 integration — Wine and the shell on wexcomp

Status: ready, 2026-10-13. This is the **Wine half** of `docs/WAYLAND_L1_WORKORDER.md` (its tasks 4–6), updated
for what the compositor actually provides and for the post-P4 layout. Read first: `docs/WAYLAND_PLAN.md`,
`docs/WAYLAND_L0.md` (spike), `docs/WAYLAND_COMPOSITOR.md` (compositor report + reviews). The X11 session stays the
shipping stack; nothing here may change its behavior.

## What the compositor gives you (labwc fork, `zcomputerwiz/labwc` branch `wexcomp`, verified 25/25)

- **Two sockets.** `wayland-0` (normal clients) and a privileged socket, default `wex-shell-0`
  (`LABWC_WEX_SHELL_SOCKET`). The privileged socket offers every normal global **plus** `wex_window_manager_v1`.
  Wayland objects cannot cross connections, so **every Wine process of the shell session connects only to
  `wex-shell-0`** (`WAYLAND_DISPLAY=wex-shell-0`).
- **`xx-zones-v1`** (standard, experimental): one zone per output; `get_zone(output)` → events `size`, `handle`
  (an **opaque random token**), `done`; `get_zone_from_handle(token)` joins it (an unknown token fails);
  `get_zone_item(xdg_toplevel)` + `add_item`; `set_position(x, y)` in zone coordinates (top-left of the frame) →
  `position` / `position_failed`; `frame_extents` are 0 for wex windows (no SSD). Zone-positioned windows skip
  labwc's placement policy.
- **`wex-window-v1`** (private, `protocols/wex-window-v1.xml`, MIT): `get_window(xdg_toplevel)`; `place_above`,
  `place_below`, `place_bottom`; `set_layer(normal|topmost|desktop|shell)`; `set_exclusive_edge(edge, size)` for
  `shell` windows (shrinks the usable area; maximize stops there); `activate()` (focus + raise in its layer;
  restores a minimized window first).
- **Policy for wex windows:** the compositor never focuses or raises them by itself (clicks are delivered, focus and
  stacking only change through `activate()`/`place_*`), never focuses one on map or after another closes, and none
  of labwc's own actions (snap, maximize/minimize keybindings, window menu, move/resize bindings, window switcher)
  apply to them. The client's own `xdg_toplevel` requests (`set_maximized`, `set_minimized`, `set_fullscreen`,
  `move`, `resize`) still work. Also offered: `wp_alpha_modifier_v1`, `wp_pointer_warp_v1`.

## Where the work goes

- **Wine fork** (`zcomputerwiz/wine`):
  - `upstream/winewayland-zones` from the fork's `master` (current upstream): xx-zones support in
    `dlls/winewayland.drv` — upstreamable. Vendor `xx-zones-v1.xml` into the driver directory as Wine does with its
    other protocol XMLs. Ready-to-paste MR text in `docs/UPSTREAM.md`; the user opens MRs.
  - `wineexplorer/11.19`: the same xx-zones commit (until upstream merges it) plus fork-only commits for
    `wex-window-v1` (vendor the XML from the labwc fork) and the role property below. Each listed in
    `docs/STOCK_CHANGES.md`.
- **Our modules:** `src/dlls/wexshell` (roles for desktop/taskbar), the stock explorer host commit on the fork
  (`/shell` parsing), session scripts under `scripts/`.

## Tasks

### 1. Wine driver: zones (upstreamable)
- Bind `xx_zone_manager_v1` when offered. One zone per Wine session, shared by all processes:
  the first process that needs it (normally the desktop/explorer process) calls `get_zone` for the primary output
  and publishes the token where every process of the session can read it — e.g. a property on the desktop window
  or a volatile key under `HKCU\Software\Wine\Wayland`; other processes `get_zone_from_handle(token)`. (Environment
  variables are not enough: programs started from a terminal or by other processes would not see them.)
- Every top-level window becomes a zone item; on map and on every `WindowPosChanged` with a new position, call
  `set_position` with the Win32 window rect converted to zone coordinates (the driver already has the rect; mind
  DPI scaling — see the FIXME in `wayland_pointer.c` about window vs. client DPI). Handle `position` (no-op when it
  matches), `position_failed` (log; leave the Win32 position), `item_left`/`size` (re-add or re-place).
- Without xx-zones, behave exactly as today.

### 2. Wine driver: `wex-window-v1` (fork-only)
- Bind `wex_window_manager_v1` when offered (only on `wex-shell-0`); `get_window` for each top-level.
- **Z-order:** in `WindowPosChanged`, map the Win32 z-order (`insert_after`, `SWP_NOZORDER`) to
  `place_above`/`place_below`/`place_bottom` against the neighbouring Wine top-level; `WS_EX_TOPMOST` →
  `set_layer(topmost)` (and back to `normal` when cleared).
- **Activation:** the compositor no longer focuses Wine windows, so the driver must: implement the user-driver
  `ActivateWindow` entry point (winewayland has none today; winex11's `X11DRV_ActivateWindow` is the model) and call
  `activate()` for the window win32u activates (click → `WM_MOUSEACTIVATE` → activation → `activate()`; keyboard
  focus follows from the compositor's `wl_keyboard.enter`). Windows that are not activated (`WS_EX_NOACTIVATE`,
  `MA_NOACTIVATE`, tooltips) must simply never call it.
- **Minimize/restore:** Win32 minimize → `xdg_toplevel_set_minimized` (exists); Win32 restore of a minimized window →
  `activate()` (the only way to un-minimize under Wayland).
- **Roles:** a documented window property set by the shell, read by the driver when the window maps and when it
  changes: e.g. `__wine_wayland_role` = `desktop` | `shell`, and for `shell` the appbar edge and size
  (`__wine_wayland_exclusive` = edge, size). Driver maps them to `set_layer(desktop|shell)` and
  `set_exclusive_edge`. Keep it generic (no class-name checks for `Shell_TrayWnd`/`Progman` in the driver).

### 3. Shell (`src/dlls/wexshell`, stock explorer host)
- `explorer /shell` without `/desktop=` currently takes `/shell` as a folder path (`Failed to create PIDL for
  Z:\shell`); only the virtual-desktop path in `programs/explorer/desktop.c` parses it. Make the host start the shell
  on the driver's own desktop when `/shell` is given without `/desktop=` (fork commit on the stock host; keep it tiny).
- On a non-virtual desktop, skip display-mode changes (`NtUserChangeDisplaySettings` returned -2 in L0 and explorer
  logged "Failed to set primary display settings") — the compositor owns modes.
- The desktop window sets role `desktop`; the taskbar sets role `shell` and its appbar edge/size from the existing
  appbar code (`src/dlls/wexshell/appbar.c`), updating when it moves, resizes or auto-hides.
- The work area under Wayland comes from the compositor's usable area (the exclusive edge); make sure
  `SPI_GETWORKAREA` and maximized Win32 windows agree with it.

### 4. Session for testing (not the ISO default)
- `scripts/wayland-session.sh`: start wexcomp (from `~/wexcomp-build` or an env-given path) with a session
  `rc.xml` that **removes labwc's default keybindings** (Alt+Tab, Super+A, A-Space etc. — Explorer owns Alt+Tab and
  shell shortcuts; global hotkey forwarding is L4) and no labwc menus; then
  `WAYLAND_DISPLAY=wex-shell-0 wine explorer /shell` with the Wayland driver forced
  (`HKCU\Software\Wine\Drivers` `Graphics`=`wayland`, `DISPLAY` unset). Optionally a
  `/usr/share/wayland-sessions/wine-explorer-wayland.desktop` for LightDM, installed only by a dev script.

## Environment

Use the Proton VM as for the refactor (separate directories): build wexcomp there in `~/wexcomp-build`
(`git clone -b wexcomp https://github.com/zcomputerwiz/labwc`, meson/ninja; deps as in
`docs/WAYLAND_COMPOSITOR_WORKORDER.md`; Debian 13 = wlroots 0.18.2), your Wine build with the driver changes, and
test nested like L0 (`scripts/wayland-spike-env.sh`, `Xvfb :9`, `WLR_BACKENDS=x11 WLR_RENDERER=pixman`) with the
fork binary and `LABWC_WEX_SHELL_SOCKET=wex-shell-0`. Do not use the WSL distributions `wexcomp` (Gemini's
compositor) or `wexbuild` (Gemini's installer work). Never use the dev VM's genuine overlay.

## Acceptance (record in `docs/WAYLAND_L1.md`, screenshots in `docs/wayland-l1/`)

Re-run the L0 matrix (`docs/WAYLAND_L0.md`) and add:
- `compose-probe` windows at their requested positions, within 1 px of the same probe on X11 Xvfb, including a
  second process;
- `explorer /shell` without `/desktop=`: desktop under everything; taskbar at the bottom edge above normal and
  topmost windows; Start menu opens from the taskbar; a maximized Notepad stops above the taskbar;
  `SPI_GETWORKAREA` matches;
- clicking a background Win32 window activates and raises it (through Win32 → `activate()`); clicking a
  `WS_EX_NOACTIVATE` window does not; clicking the desktop does not raise it over anything;
- taskbar button minimize and restore of Notepad; topmost window stays above normal ones;
- 50% `LWA_ALPHA` window translucent; `SetCursorPos` moves the pointer;
- menus, keyboard, per-pixel alpha, foot/XWayland apps alongside: no regressions from L0;
- the same Wine build under stock labwc (no zones, no wex-window) behaves as in L0, and under X11 the session is
  unchanged (`vm.sh` dev VM session, D1/D2 probes).

Rules as always: commit after each working step, GPL headers, `STOCK_CHANGES.md` for every fork commit, never
pass C escapes through heredocs/sed/`python -c`.
