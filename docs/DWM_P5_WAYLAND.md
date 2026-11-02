<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# DWM Phase P5: Wayland Channel and Surface Corner Radius Protocol

This document specifies the Wayland protocol extension and compositor architecture implemented for DWM roadmap Phase P5 ("Wayland channel/capabilities" in `wexcomp`), along with exact integration instructions for the Wine-side client (`wexhost` and `winewayland.drv`).

---

## 1. Context and Problem Statement

On X11, DWM Phase P4 delivers window corner attributes (`DWMWA_WINDOW_CORNER_PREFERENCE`) to the compositor via the PE bridge and wexhost:
- `DWMWCP_DEFAULT` (0) / `DWMWCP_DONOTROUND` (1) $\to$ `radius = 0` (sharp)
- `DWMWCP_ROUND` (2) $\to$ `radius = 8` px
- `DWMWCP_ROUNDSMALL` (3) $\to$ `radius = 4` px
via `WexCompositionSetCorner(xwindow, radius)`.

Under `wexcomp` (the standalone Wayland compositor based on labwc), the compositor owns the scene graph and output rendering. Phase P5 defines the Wayland protocol request, version negotiation, compositor scene clipping, validation, and security model so that the identical corner preferences work seamlessly under Wayland.

---

## 2. Protocol Specification (`wex-window-v1.xml`)

The `wex_window_manager_v1` and `wex_window_v1` interfaces are bumped from version **11** to version **12**.

### 2.1 Interface Definition

In `wexcomp/protocols/wex-window-v1.xml`:

```xml
<interface name="wex_window_manager_v1" version="12">
  ...
</interface>

<interface name="wex_window_v1" version="12">
  ...
  <request name="set_corner_radius" since="12">
    <description summary="set window corner radius for composition">
      Requests the compositor to clip the window's outer corners using the
      given radius in surface coordinates (pixels).

      A radius of 0 disables corner clipping and renders unchanged sharp
      corners. The compositor clamps the radius to [0, 32].

      Only privileged clients owning the window may issue this request.
    </description>
    <arg name="radius" type="uint" summary="corner radius in surface pixels (clamped to 0..32)"/>
  </request>
</interface>
```

### 2.2 Wire Semantics & Validation Rules

1. **Scalar Value**: `radius` is an unsigned 32-bit integer representing the outer corner radius in surface pixels.
2. **Clamping**: The compositor clamps incoming values strictly to `[0, 32]`.
   - `radius == 0`: Corner clipping is disabled; stock rectangular rendering is used.
   - `1 <= radius <= 32`: Corners are clipped according to the specified radius.
   - `radius > 32`: The compositor automatically clamps the radius to `32` without issuing a protocol error or terminating the client.
3. **Authorization & Socket Privilege**:
   - `wex_window_manager_v1` is advertised exclusively on the private privileged socket `LABWC_WEX_SHELL_SOCKET` (`wex-shell-0`). Unprivileged clients connecting on `wayland-0` never receive the global.
   - Any attempt to bind `wex_window_manager_v1` from an unprivileged client is rejected with `WL_DISPLAY_ERROR_INVALID_OBJECT` ("permission denied").
4. **Owner-Only Enforcement**:
   - `set_corner_radius` validates that the caller owns the underlying `wl_surface` (`wl_resource_get_client(win->view->surface->resource) == client`).
   - If a privileged client attempts to invoke `set_corner_radius` on another client's surface, the compositor raises `WL_DISPLAY_ERROR_INVALID_OBJECT` ("permission denied: not surface owner").
5. **Version Negotiation & Backwards Compatibility**:
   - An older client binding `wex_window_manager_v1` at version 11 or lower receives a v11 proxy and never invokes `set_corner_radius`. The compositor maintains full backwards compatibility with v1–v11 peers.
   - An older compositor advertising version 11 causes a newer client to bind `min(advertised, 12) = 11`. The client detects version $< 12$ and falls back silently to sharp rendering without protocol violation.

---

## 3. Compositor Implementation (`wexcomp`)

### 3.1 Scene Architecture

`wexcomp` manages view content through wlroots scene graphs (`view->content_tree`).

1. **When `radius == 0` (Default / Sharp)**:
   - Any existing `corner_tree` is destroyed.
   - The unclipped stock scene buffer node (`root->node`) is enabled.
   - 100% of stock rendering, input routing, native frame holes, and resize behavior is preserved.
2. **When `radius > 0` (Rounded Corners)**:
   - The unclipped stock scene buffer node (`root->node`) is disabled (`wlr_scene_node_set_enabled(&root->node, false)`).
   - A dedicated `corner_tree` (`wlr_scene_tree`) is created beneath `view->content_tree`.
   - Horizontal rectangular slices are calculated using midpoint-circle rasterization (`compute_corner_inset(r, y)`):
     - Rows $y \in [0, r)$: top rounded corner rows, inset by $inset(r, y)$ on both left and right.
     - Rows $y \in [r, height - r)$: middle body rectangle spanning full width $[0, width]$.
     - Rows $y \in [height - r, height)$: bottom rounded corner rows, inset by $inset(r, height - 1 - y)$.
   - Each slice is backed by a `wlr_scene_buffer` referencing the surface texture, configured via `wlr_scene_buffer_set_source_box` and `wlr_scene_buffer_set_dest_size`.
3. **Input Routing**:
   - `corner_piece_accepts_input` maps slice-relative pointer coordinates back to surface coordinates and checks `wlr_surface_point_accepts_input(surface, sx, sy)`. Clicks outside the rounded arc fall through to underlying windows.
4. **Buffer Commits & Lifecycle**:
   - Surface commits are hooked via `handle_window_surface_commit`. When the client attaches and commits a resized buffer, slices are recomputed.
   - Window destruction (`wex_window_resource_destroy`) and view unmapping (`wex_window_on_view_destroy`) cleanly drop corner pieces, remove the commit listener, and destroy `corner_tree`.

---

## 4. Wine-Side Integration (`wexhost` & `winewayland.drv`)

To integrate the Wayland corner preference on the Wine host side, apply the following steps in `src/dlls/wexhost/` and `wine/dlls/winewayland.drv/`:

**Status (2026-11-02): compositor side only.** The Wine side below is not wired yet. Because the request is owner-only, the shell's bridge
(`src/dlls/wexshell/dwm_bridge.c`, which serves X11) cannot set it for another process's window: the application's own `winewayland.drv`
connection has to send it, and that connection must be offered `wex_window_manager_v1`. Deciding how an ordinary application process obtains
that (or a narrower, unprivileged request for its own surfaces) is the open integration step.

### 4.1 Synchronize Protocol XML

Copy the updated `wexcomp/protocols/wex-window-v1.xml` (version 12) to:
- `src/dlls/wexhost/wex-window-v1.xml`
- `wine/dlls/winewayland.drv/wex-window-v1.xml`

Regenerate client headers using `wayland-scanner client-header` and `wayland-scanner private-code`.

### 4.2 Version Negotiation

When binding `wex_window_manager_v1` in the Wayland driver registry handler:

```c
static void
registry_handle_global(void *data, struct wl_registry *registry,
                       uint32_t name, const char *interface, uint32_t version)
{
    struct wayland_context *ctx = data;
    if (strcmp(interface, wex_window_manager_v1_interface.name) == 0) {
        uint32_t bind_version = version < 12 ? version : 12;
        ctx->wex_window_manager = wl_registry_bind(registry, name,
            &wex_window_manager_v1_interface, bind_version);
        ctx->wex_window_manager_version = bind_version;
    }
}
```

### 4.3 Mapping `DWMWA_WINDOW_CORNER_PREFERENCE`

In `dlls/dwmapi` and the `wexhost` / `winewayland.drv` window attribute bridge:

```c
enum DWM_WINDOW_CORNER_PREFERENCE {
    DWMWCP_DEFAULT    = 0,
    DWMWCP_DONOTROUND = 1,
    DWMWCP_ROUND      = 2,
    DWMWCP_ROUNDSMALL = 3,
};

static uint32_t
corner_preference_to_radius(DWORD preference)
{
    switch (preference) {
    case DWMWCP_ROUND:
        return 8;
    case DWMWCP_ROUNDSMALL:
        return 4;
    case DWMWCP_DEFAULT:
    case DWMWCP_DONOTROUND:
    default:
        return 0;
    }
}
```

### 4.4 Dispatched Wayland Call

When setting corner radius on a Wayland window:

```c
void
wayland_window_set_corner_preference(struct wayland_win_data *data, DWORD preference)
{
    if (!data || !data->wex_window)
        return;

    /* Check if compositor supports version 12 (set_corner_radius) */
    if (wl_proxy_get_version((struct wl_proxy *)data->wex_window) < 12) {
        /* Fall back silently: compositor is older peer without corner clipping */
        return;
    }

    uint32_t radius = corner_preference_to_radius(preference);
    wex_window_v1_set_corner_radius(data->wex_window, radius);

    /* Commit the surface to apply geometry changes */
    wl_surface_commit(data->wayland_surface);
    wl_display_flush(data->process_display);
}
```

### 4.5 Threading and Lock Safety

As documented in `docs/DWM_ARCHITECTURE.md` Section 4.2:
- All Wayland protocol requests must be dispatched from the thread owning the `wl_display` connection.
- Avoid holding Win32 USER window locks across Wayland roundtrips or flushes.
- Ensure the `wex_window_v1` reference is torn down when the Win32 window is destroyed or unmapped.

---

## 5. Test Suite Verification

Compositor changes are validated by the test suite under `wexcomp/tests/wex/`:

| Test | Objective | Verification Method |
| --- | --- | --- |
| `Corner Radius: Version negotiation` | Advertised version is $\ge 12$; v11 binding succeeds and operates as old peer; v12 binding succeeds. | Proxy version checks and protocol roundtrips. |
| `Corner Radius: Denial on unprivileged socket` | Unprivileged connection on `wayland-0` cannot access `wex_window_manager_v1`. | Registry discovery confirms interface is withheld. |
| `Corner Radius: Surface destruction` | Window destroyed while radius 16 is active. | Verify compositor stability and subsequent window creation without fault. |
| `Corner Radius: Visual clipping (16px)` | Window top-left corner at $(200, 200)$ is clipped to background color; interior is filled. | Pixel check via `verify.py check-pixel` (Navy background at $(200, 200)$, Red at $(300, 300)$). |
| `Corner Radius: Visual clearing (0px)` | Setting radius back to 0 restores sharp corners. | Pixel check via `verify.py check-pixel` (Red at $(200, 200)$). |
| `Corner Radius: Clamping (> 32px)` | Radius 100 is clamped to 32px by compositor without error. | Pixel check at corner (clipped) and $(240, 240)$ (inside $>32$ px boundary, Red). |
