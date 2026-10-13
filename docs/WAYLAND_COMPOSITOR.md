# Wayland Compositor Report: labwc Fork "wexcomp"

**Status:** Complete  
**Date:** 2026-10-13  
**Repository:** [https://github.com/zcomputerwiz/labwc](https://github.com/zcomputerwiz/labwc)  
**Branch:** `wexcomp`  
**Base:** Upstream labwc tag `0.8.3` (commit `1fe4797a9d29`) on Debian 13 (wlroots 0.18.2)

---

## 1. Executive Summary

This report documents the completion of the **compositor half** of `docs/WAYLAND_L1_WORKORDER.md` (Tasks 1–4 from `docs/WAYLAND_COMPOSITOR_WORKORDER.md`). The compositor fork (`wexcomp`) adds the necessary Wayland protocols and window management semantics required for WineExplorer and Wine's Wayland driver:

1. **`xx-zones-v1`**: Staging protocol from wayland-protocols for explicit coordinate placement, zone sharing by handle across multiple processes, and placement-policy bypassing.
2. **`wex-window-v1`**: Private privileged protocol for Win32-style window layering (`desktop`, `normal`, `topmost`, `shell`), relative stacking reordering (`place_above`, `place_below`, `place_bottom`), exclusive edge panel reservation (taskbars), and unminimize-on-activate semantics. Exclusively exposed on a dedicated UNIX socket (`wex-shell-0`).
3. **Missing Globals**:
   - `wp_alpha_modifier_v1`: Wired to scene graph buffer opacities on commit to support uniform surface transparency (Win32 `LWA_ALPHA`).
   - `wp_pointer_warp_v1`: Repositions the pointer and emits pointer motion events.
   - Headless seat pointer & keyboard capability exposure for headless automation.
4. **Automated Test Suite**: Complete standalone C test clients and runner (`tests/wex/run.sh`) executed headlessly in WSL2 (`1280x800`), verified with `grim` screencopy and Python Pillow pixel assertions. All 16 automated tests pass with 100% success.

---

## 2. Commit Log

All commits have been pushed to branch `wexcomp` at `https://github.com/zcomputerwiz/labwc`:

| Commit SHA | Commit Summary |
|---|---|
| `7fb86a18` | `protocols: vendor xx-zones-v1.xml from wayland-protocols` (vendored from upstream commit `35d9638cf0c3f0bbf0c42996580a284f3d8ce564`) |
| `06db581a` | `xx-zones-v1: implement zone manager, zones, and zone items` (`include/protocols/xx-zones.h`, `src/protocols/xx-zones.c`, integrated into `xdg.c`, `view.c`, `server.c`) |
| `b1c5fddd` | `protocols: add wex-window-v1.xml private protocol` (MIT protocol specification for manager and window interfaces) |
| `cbca3438` | `wex-window-v1: implement stacking, layers, exclusive edge, and activation` (`include/protocols/wex-window.h`, `src/protocols/wex-window.c`, integrated into `server.c`, `layers.c`, `desktop.c`, `view.c`, `xdg.c`) |
| `b3cc84f2` | `protocols: vendor pointer-warp-v1.xml from wayland-protocols` (vendored from upstream commit `35d9638cf0c3f0bbf0c42996580a284f3d8ce564`) |
| `cee16321` | `protocols: implement wp_alpha_modifier_v1 and wp_pointer_warp_v1` (`include/protocols/pointer-warp.h`, `src/protocols/pointer-warp.c`, wired `wlr_alpha_modifier_v1` to scene buffer opacity in `src/xdg.c`) |
| `ac9128c4` | `wex-window-v1: unminimize window before activating` (ensures `activate()` restores minimized windows per Win32 requirement) |
| `d3d498eb` | `seat: expose pointer and keyboard capabilities on headless backend` |
| `f153f019` | `seat: properly detect headless backend and empty inputs` |
| `43d20d84` | `tests/wex: add automated protocol test suite and clients` (`zones-client`, `stack-client`, `alpha-client`, `warp-client`, `shm-common`, `verify.py`, `run.sh`, `Makefile`) |

---

## 3. Protocol Architecture & Implementation Details

### 3.1 `xx-zones-v1`
- **Zone Management**: Outputs correspond to zones. Zone handles are keyed to output names (e.g. `HEADLESS-1`). When cooperating Wine processes join using `get_zone_from_handle(handle)`, they connect to the exact same coordinate zone.
- **Explicit Placement**: Window positions requested via `xx_zone_item_v1.set_position(x, y)` explicitly set `view->pending.x` and `view->pending.y` on mapping, setting `view->zone_positioned = true`. This deliberately bypasses labwc's `rc.placement_policy` (e.g. automatic centering).
- **Extents & Offsets**: Server-side decorations and client-side margins are factored using `ssd_get_margin()`. Zone item coordinates refer strictly to the top-left corner of the window frame.
- **Bounds Checking**: If a client requests a window position where the entire window would lie outside the zone boundaries, the request is rejected with a `position_failed` event without changing window geometry.

### 3.2 `wex-window-v1`
- **Privileged Socket Security**: The compositor creates a secondary privileged socket (`wex-shell-0` by default or configurable via `LABWC_WEX_SHELL_SOCKET`). In `src/server.c:server_global_filter`, incoming client file descriptors are inspected via `getsockname(wl_client_get_fd(client))` in `wex_client_is_privileged()`. If a client connected through standard `wayland-0`, `wex_window_manager_v1` is filtered from registry advertisements.
- **Window Layering**:
  - `desktop` (enum 2): Reparented to `server->desktop_tree` (placed below workspaces/normal windows), SSD disabled (`LAB_SSD_MODE_NONE`), excluded from Alt+Tab window cycling.
  - `normal` (enum 0): Attached to the workspace tree.
  - `topmost` (enum 1): Reparented to `server->view_tree_always_on_top`.
  - `shell` (enum 3): Reparented to `server->shell_tree` (above topmost/popups), SSD disabled, excluded from window cycling.
- **Exclusive Edge Reservations**: Shell windows call `set_exclusive_edge(edge, size)`. For example, a bottom taskbar of 30px calls `set_exclusive_edge(edge=bottom, size=30)`. This updates `output->usable_area`, reducing the available workspace height from 800px to 770px. When normal windows are maximized (`xdg_toplevel.set_maximized`), their configure geometry conforms to 1280x770, cleanly stopping above the taskbar.
- **Stacking Reordering**: `place_above` and `place_below` manipulate the scene graph using `wlr_scene_node_place_above()` / `wlr_scene_node_place_below()` and reorder `view->link` within `server->views`.
- **Activation & Restore**: Calling `activate()` raises the window to the top of its layer and assigns keyboard focus (`seat_focus_surface()`). If `view->minimized` is true, it calls `view_minimize(view, false)` first, guaranteeing that minimized windows are restored upon activation.
- **Passive Focus Policy**: Windows associated with `wex_window_v1` do not steal focus on clicks; focus and stacking are governed exclusively by client requests (`activate()`, `place_*`).

### 3.3 Missing Globals
- **`wp_alpha_modifier_v1`**: Supported via `wlr_alpha_modifier_v1`. On commit in `src/xdg.c:handle_commit`, the view scene tree is traversed with `wlr_scene_node_for_each_buffer()`. If an alpha modifier multiplier is set for the surface, `wlr_scene_buffer_set_opacity(buffer, (float)state->multiplier)` is applied.
- **`wp_pointer_warp_v1`**: Implemented in `src/protocols/pointer-warp.c`. When requested for a focused surface within surface bounds, calls `wlr_cursor_warp(seat->cursor, NULL, lx, ly)`, followed by `wlr_seat_pointer_notify_motion()` and `wlr_seat_pointer_notify_frame()`.
- **Headless Capabilities**: Updated `src/seat.c` to advertise `WL_SEAT_CAPABILITY_POINTER` and `WL_SEAT_CAPABILITY_KEYBOARD` when running on headless backends, enabling full pointer enter, motion, and warp interactions in automated testing.

---

## 4. Test Suite & Verification Results

The test suite in `tests/wex/` was built from scratch and executed in headless mode with:
```bash
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 tests/wex/run.sh
```

### 4.1 Test Results Table

| # | Test Name | Scenario / Operation | Expected Result | Status |
|---|---|---|---|---|
| 1 | Socket Isolation | Client queries `wayland-0` vs `wex-shell-0` | `wex_window_manager_v1` absent on `wayland-0`, present on `wex-shell-0` | **PASS** |
| 2 | Explicit Placement 1 | Red window at (100, 150) size 200x200 | Pixels at (100, 150, 200, 200) match `#FF0000`; labwc center policy ignored | **PASS** |
| 3 | Explicit Placement 2 | Blue window at (400, 200) size 250x180 | Pixels at (400, 200, 250, 180) match `#0000FF` | **PASS** |
| 4 | Off-Zone Rejection | Request placement at (2000, 2000) | `position_failed` received, window position unaltered | **PASS** |
| 5 | Join by Handle | Second process joins via handle `HEADLESS-1` | Green window at (700, 100, 200, 200) matches `#00FF00` in shared coordinate zone | **PASS** |
| 6 | Layer Desktop | Desktop window size 1280x800 Navy (`#112233`) | Pixel at (50, 50) matches `#112233` below all layers | **PASS** |
| 7 | Layer Normal 1 | Normal window 1 Red (`#FF0000`) at (100, 100) | Pixel at (120, 120) matches `#FF0000` | **PASS** |
| 8 | Layer Normal 2 | Normal window 2 Blue (`#0000FF`) at (150, 150) | Overlap at (160, 160) matches `#0000FF` (Blue above Red) | **PASS** |
| 9 | Layer Topmost | Topmost Yellow (`#FFFF00`) at (200, 200) | Overlap at (210, 210) matches `#FFFF00` (Yellow above Normal) | **PASS** |
| 10 | Layer Shell | Shell Green (`#00AA00`) at (0, 770) size 1280x30 | Taskbar pixel at (640, 785) matches `#00AA00` above all windows | **PASS** |
| 11 | Layer Reorder | `place_above(Normal1, Normal2)` | Overlap at (160, 160) matches `#FF0000` (Red raised above Blue) | **PASS** |
| 12 | Shell Exclusive Edge | Bottom edge reserved 30px | Shell window at bottom intact, usable area shrunk to 1280x770 | **PASS** |
| 13 | Window Maximization | Normal window requests maximize | Configured size is 1280x770 (800 - 30); pixel (640, 400) matches `#4488CC` | **PASS** |
| 14 | Unminimize on Activate | Window minimized then activated via `activate()` | Compositor unminimizes, restores, and focuses window | **PASS** |
| 15 | Alpha Modifier 50% | Red window with multiplier `0x7fffffff` over white background | Blended pixel at (100, 100, 200, 200) matches RGB (255, 128, 128) | **PASS** |
| 16 | Pointer Warp | Warp pointer to (350, 250) on focused surface | Client receives `wl_pointer.motion` at exact warped coordinates (350, 250) | **PASS** |

---

## 5. Visual Artifacts

The verification screenshots captured by `grim` during headless test execution are stored in `docs/wayland-compositor/`:

### 5.1 Zone Explicit Placement (`01_zones_placement.png`)
Window 1 placed at (100, 150) (Red) and Window 2 placed at (400, 200) (Blue), overriding compositor centering:  
![01_zones_placement](wayland-compositor/01_zones_placement.png)

### 5.2 Zone Shared Handle (`02_zones_join_handle.png`)
Second client joins the existing zone handle and places Window 3 at (700, 100) (Green):  
![02_zones_join_handle](wayland-compositor/02_zones_join_handle.png)

### 5.3 Layer Stacking Hierarchy (`03_stack_layers.png`)
Desktop (Navy `#112233`) < Normal 1 (Red `#FF0000`) < Normal 2 (Blue `#0000FF`) < Topmost (Yellow `#FFFF00`) < Shell (Green `#00AA00` at bottom):  
![03_stack_layers](wayland-compositor/03_stack_layers.png)

### 5.4 Stacking Reorder via `place_above` (`04_stack_reorder.png`)
Normal 1 (Red) reordered above Normal 2 (Blue):  
![04_stack_reorder](wayland-compositor/04_stack_reorder.png)

### 5.5 Shell Exclusive Edge & Maximized Bounds (`05_stack_maximize.png`)
Maximized normal window cleanly stops at the 30px shell panel boundary (height 770px instead of 800px):  
![05_stack_maximize](wayland-compositor/05_stack_maximize.png)

### 5.6 Alpha Modifier 50% Blending (`06_alpha_50.png`)
Red surface rendered at 50% uniform opacity over white background, producing exact RGB (255, 128, 128):  
![06_alpha_50](wayland-compositor/06_alpha_50.png)

---

## 6. How to Build & Run

### 6.1 Entering the Environment
On the host Windows machine:
```powershell
wsl -d wexcomp -u root
```

### 6.2 Building labwc (`wexcomp`)
Inside the `wexcomp` WSL2 environment:
```bash
cd /root/wexcomp
meson setup build  # if not already setup
ninja -C build
```

### 6.3 Running Automated Tests
```bash
cd /root/wexcomp
./tests/wex/run.sh
```
All tests execute headlessly without requiring a display or GPU, taking ~15 seconds to complete.

---

## 7. Hand-off for Wine & Codex (Tasks 4–6)

The compositor half of the Wayland architecture is complete, verified, and pushed. When Codex integrates Wine's Wayland driver and WineExplorer:
- Wine processes should connect to `wayland-0` for standard surfaces and connect to `wex-shell-0` for privileged shell management (`wex_window_manager_v1`).
- Explorer's desktop window should bind to `wex-window-v1` and call `set_layer(desktop)`.
- The Explorer taskbar should bind to `wex-window-v1` and call `set_layer(shell)` and `set_exclusive_edge(edge=bottom, size=30)`.
- Normal Wine windows can be arranged within output zones via `xx-zones-v1` (`xx_zone_item_v1.set_position`).
- Windows with `WS_EX_TOPMOST` should call `wex_window_v1.set_layer(topmost)`.
- Transparent windows with `LWA_ALPHA` should bind `wp_alpha_modifier_v1` on their `wl_surface`.
