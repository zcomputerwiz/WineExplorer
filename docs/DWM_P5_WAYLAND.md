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

**Status (2026-11-03): wired through the shell, not the application.** The owner-only `set_corner_radius` stays for a window's own
connection. The shell, the one privileged client, sets every Wine window's effects with `wex_window_manager_v1.set_effects` (v13, section 6),
naming the window by the shared handle that `winewayland.drv` publishes in `__wine_wayland_window_handle`:
`dwm_bridge.c:apply_wayland` -> `WexWmSetEffects` -> unix `wm_effects` -> `waylandwm.c:wayland_effects`. The bridge starts on a Wayland
session (`desktop.c`), and `winewayland.drv` keeps per-pixel alpha for DwmAlpha windows (ARGB buffer, opaque outside the glass, opaque region
set). Verified end to end by `scripts/check-wayland-dwm.sh` (screenshots: Aero frames with rounded corners, shadow and tint; tinted blur,
Acrylic and gradient accents with the exact blends of X11). Open: real blur, acrylic and mica (the compositor draws the tint only; wlroots
0.18's scene has no blur), thumbnails and Peek, presentation feedback.

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

---

## 6. Version 13 Protocol: `set_effects`

Phase P5 v13 extends `wex_window_manager_v1` to version 13 with the `set_effects` request:

```xml
<request name="set_effects" since="13">
  <arg name="handle" type="string"/>
  <arg name="corner_radius" type="uint"/>
  <arg name="backdrop" type="uint" enum="backdrop"/>
  <arg name="flags" type="uint" enum="effect_flags"/>
  <arg name="tint" type="uint"/>
  <arg name="left" type="int"/><arg name="top" type="int"/>
  <arg name="right" type="int"/><arg name="bottom" type="int"/>
</request>
```

### 6.1 Semantics and Stacking

1. **Privileged Dispatch & Lookup**: Available only on the private endpoint (`wex-shell-0`). The target window is identified by its shared opaque handle (`wex_window_v1.handle`). Stale or unknown handles are harmless no-ops.
2. **Replacement and Reset**: Each request atomically replaces the window's effect parameters. An all-zero call clears all effects and destroys any active effect scene nodes.
3. **Corner Clipping**: Reuses `compute_corner_slices` (clamped to 0..32). An effect corner radius and an owner's `set_corner_radius` update the same property without conflict (last request wins).
4. **Soft Window Shadow (Flag 1)**: Rendered via six nested `wlr_scene_rect` nodes in a dedicated `shadow_tree` below the content/backdrop, spanning roughly 10 px of spread offset 2 px down with ~2.5% alpha accumulation.
5. **Backdrop Materials**: Rendered via `backdrop_tree` placed below the content/corner tree. When tint flag (4) is set, straight `0xAARRGGBB` is converted to premultiplied float. Otherwise, defaults are applied (Mica: F3F3F3 at 50% / dark 202020 at 80%; Acrylic: F3F3F3 at 70% / dark 202020 at 70%; Blur: white at 25% / dark 202020 at 25%). Optional region flag (8) bounds the backdrop to `[left, top, right, bottom]`. Without compositor blur shader passes, the material tint alone is drawn behind translucent pixels.
6. **Lifecycle & Input**: Effect trees are children of `view->scene_tree`, surviving buffer commits/resizes, hiding on minimize/unmap, and dropped on destruction without scene node leakage or accepting pointer input.

---

## 7. Version 14 Protocol: Live Thumbnails, Aero Peek, and Frame Events

Phase P5 v14 extends `wex_window_manager_v1` to version 14 with `set_thumbnails`, `set_peek`, and the `frame` event:

```xml
<request name="set_thumbnails" since="14">
  <arg name="records" type="array"/>
</request>
<request name="set_peek" since="14">
  <arg name="target" type="string"/>
  <arg name="keep" type="array"/>
</request>
<event name="frame" since="14">
  <arg name="frame" type="uint"/>
  <arg name="sec_hi" type="uint"/>
  <arg name="sec_lo" type="uint"/>
  <arg name="nsec" type="uint"/>
</event>
```

### 7.1 Live Window Thumbnails (`set_thumbnails`)

1. **Record Specification**: Records are packed at 104 bytes each (`dest[32]`, `src[32]`, `flags` uint32, `opacity` uint32, `dest_rect[4]` int32, `source_rect[4]` int32). Malformed array sizes (not a multiple of 104) are safely ignored without processing. Maximum 80 records per request.
2. **Lookup & Filtering**: Looks up destination and source windows by 32-byte shared handle via `find_window_handle`. Records referencing unknown/stale handles, empty rectangles, or `src == dest` are skipped.
3. **Scene Node Placement**: Built as a `thumbnail_tree` child of the destination view's `scene_tree`, placed above `content_tree` so thumbnails render over destination pixels and clip automatically with destination geometry.
4. **Source Scaling, Cropping & Filter Mode**: Uses `wlr_scene_surface_create` displaying the main source `wl_surface`. When `has_source` (flag 4) is set, crops via `wlr_scene_buffer_set_source_box`; otherwise uses the source surface dimensions. Scales to destination rect size via `wlr_scene_buffer_set_dest_size` and sets opacity (`opacity / 255.0f`). Explicitly sets `wlr_scene_buffer_set_filter_mode(scene_surface->buffer, WLR_SCALE_FILTER_BILINEAR)` to prevent downscaling aliasing and jagged text artifacts.
5. **Input Pass-Through**: Overrides `scene_surface->buffer->point_accepts_input` returning false, ensuring thumbnails do not intercept pointer or touch events.
6. **Lifecycle & Updates**: Listens to source surface commits to reapply crop/scale when source buffers change, and destination resizes. Minimized or hidden source windows keep showing their last committed buffer. Nodes are safely dropped when destination or source windows are destroyed or unmapped.
7. **Downscaling Quality and Architecture Evaluation**:
   - *Filter Mode*: Both wlroots 0.18 and SceneFX 0.3 support `WLR_SCALE_FILTER_BILINEAR` and `WLR_SCALE_FILTER_NEAREST`, translated directly to OpenGL `GL_LINEAR`/`GL_NEAREST` or Pixman `PIXMAN_FILTER_BILINEAR`/`PIXMAN_FILTER_NEAREST`.
   - *Multi-Pass & Mipmapping Prior Art*: In other compositors (Sway, KWin, Hyprland), live window thumbnails and overview effects rely on hardware bilinear filtering. While KWin historically experimented with Lanczos filtering, multi-tap convolution on full window textures on every frame is costly. Wayland client buffers (dmabuf / shm) cannot generate mipmaps without extra FBO texture blits. Gamescope implements NIS and AMD FSR, but these are spatial upscaling algorithms designed for lower-resolution games upscaled to high-resolution outputs, not thumbnail downscaling. SceneFX provides dual-Kawase blur downsampling, but this is an internal pipeline tied to backdrop blur framebuffers, not an exposed scene-graph buffer node filter.
   - *Scene Graph Constraints*: The wlroots and SceneFX scene-graph APIs operate directly on buffer nodes and do not provide intermediate pre-downsampling passes or FBO chain nodes without writing a custom renderer. Bilinear filtering provides the smallest, cleanest, and most efficient solution that eliminates sharp aliasing on downscaled windows.

### 7.2 Aero Peek (`set_peek`)

1. **Target and Keep Filtering**: Privileged request specifying target window handle and an array of 32-byte handles to keep (capped at 64 entries). Mapped application views not in the keep list and not matching the target are hidden (`wlr_scene_node_set_enabled` false on `view->scene_tree`).
2. **Shell Preservation**: Views with `wex_exclusive_edge != 0` or layer roles `WEX_WINDOW_V1_LAYER_SHELL` / `DESKTOP` remain visible unconditionally.
3. **Target Visibility**: Target window is kept visible even if minimized.
4. **Deactivation & Automatic Cleanup**: An empty target string (`""`) ends the peek and restores original view visibility. If the issuing privileged client disconnects while peek is active, peek is automatically terminated. Newly mapped views during peek are hidden unless in the keep list.

### 7.3 Output Frame Notification (`frame`)

1. **Primary Output Commit Hook**: Attached to `wlr_output->events.commit` on the compositor's primary output.
2. **Conditional Dispatch**: Dispatches only when at least one manager resource is bound at version $\ge 14$, imposing zero per-frame cost when inactive.
3. **Timestamp & Counter**: Sends 32-bit incremental frame counter along with `CLOCK_MONOTONIC` split into `sec_hi`, `sec_lo`, and `nsec`, providing a stable composed-frame clock for client-side DWM synchronization.

---

## 8. Frame Pacing, Presentation Feedback, and Latency Optimization

### 8.1 Presentation Feedback (`wp_presentation`)
1. **Global Creation**: `wlr_presentation` is initialized in `server.c` on compositor startup (`wlr_presentation_create(server->wl_display, server->backend)`), advertising the standard `wp_presentation` interface (v1) with `CLOCK_MONOTONIC`.
2. **Surface & Mirror Delivery**: Wlroots scene graph integrates presentation feedback through `output_sample` events emitted on each visible buffer node.
   - For regular windows, the main surface's buffer node receives the feedback event.
   - For live thumbnails, mirrors created via `wlr_scene_surface_create` are distinct scene buffer nodes. Minimized or hidden source windows keep receiving `frame_done` and presentation feedback when visible in an active thumbnail because their thumbnail mirror remains enabled on the scene output.
   - For windows visible both normally and as thumbnails, `wlr_surface_send_frame_done` drains the pending frame callback list once; redundant delivery attempts are harmless no-ops.

### 8.2 Frame Scheduling and Idle Zero-Damage Optimization
1. **Damage-Driven Scheduling**: Output frames are rendered exclusively when damage is present or when a client explicitly requests a frame callback via `wlr_output_schedule_frame`.
2. **Zero-Damage Thumbnail Refresh**: On every output commit, `wex_window_manager_on_output_commit` invokes `wex_window_manager_refresh_thumbnails` to sync subsurfaces and hosted native surfaces. To prevent busy-rendering loops:
   - `apply_thumbnail_geometry` inspects the mirror buffer's actual properties (`dst_width`, `dst_height`, `node.x`, `node.y`, `src_box`, `opacity`, `enabled`).
   - If geometry, crop, and opacity have not changed, all scene-graph node modification calls (`wlr_scene_buffer_set_*`, `wlr_scene_node_set_*`) are bypassed.
   - Consequently, no scene damage is posted and no extra output frames are scheduled when idle, ensuring 0 output commits during idle intervals.

### 8.3 Latency Reduction: Late-Frame Latch (`max_render_time`)
1. **Config Key**: Optional setting `<core><maxRenderTime>0</maxRenderTime></core>` (default `0` = off, preserving immediate rendering).
2. **Timing Delay**: When `max_render_time > 0`, the compositor measures the interval since the last output presentation (`output->last_presentation`) and the known refresh duration (`output->refresh_nsec`), calculating predicted milliseconds until next VBlank (`msec_until_refresh`).
3. **Timer Dispatch**: If `delay = msec_until_refresh - rc.max_render_time >= 1`, rendering is held back via a timer (`output->repaint_timer`) until `max_render_time` milliseconds before the expected VBlank, sampling the freshest client state immediately before scanout.

### 8.4 Content-Type Hints (`wp_content_type_v1`)
1. **Protocol Integration**: `wp_content_type_v1` is compiled into `server_protos` from the wayland-protocols staging specification.
2. **Global Advertisement**: `wlr_content_type_manager_v1_create(server->wl_display, 1)` in `server.c` advertises `wp_content_type_manager_v1` to all Wayland clients.
3. **Client Advertising**: Allows video players and Wine games to advertise content types (`video`, `game`, `photo`), enabling compositor and DRM-level display optimizations (e.g. ALLM / Game Mode on compatible HDMI/DP sinks).

### 8.5 Test Verification

| Test | Objective | Verification Method |
| --- | --- | --- |
| `Thumbnail Scaling: 1/4 scaled checker pattern average colour is mid-gray` | Verifies bilinear downscaling produces smooth interpolation on high-frequency content instead of hard black/white aliasing. | Scenario `thumbnails-scale` in `corner-client.c` scaling 200x200 1-pixel checkerboard to 50x50, verified via `verify.py check-average` within [115, 140]. |
| `Thumbnail Scaling: Centre pixel of 1/4 scaled checker is mid-gray` | Verifies individual sampled interior pixel is interpolated to mid-gray rather than aliasing to extreme 0 or 255. | Scenario `thumbnails-scale` in `corner-client.c`, verified via `verify.py check-range` within [115, 140]. |
| `Frame Pacing: Zero output commits when idle over ~1s` | Verifies the compositor generates zero damage and schedules zero frames when active windows and live thumbnails are idle over ~1 second, and immediately resumes scheduling on surface damage. | Scenario `idle` in `corner-client.c` monitoring `wex_window_manager_v1.frame` events during a 1.0 s sleep followed by a buffer commit. |
| `Content Type: wp_content_type_v1 global advertised` | Verifies `wp_content_type_manager_v1` is advertised by the compositor on the normal Wayland socket. | Scenario `content-type` in `corner-client.c` querying registry on `wayland-0`. |

---

## Fractional scale handling

- Thumbnail rectangles cross the protocol in `wl_surface` logical units. winewayland.drv publishes each
  window's `window.scale` as the existing `__wine_wayland_scale_120` property (1/120 units, set on creation
  and on every scale change). The DWM bridge defaults a missing property to 1,
  divides destination edges by the destination scale (nearest, ties away from zero), and source crop edges
  by the source scale (left/top down, right/bottom up). Both scales participate in sent-list change detection,
  so moving either window between scales resends even without a broker revision or rounded rectangle change.
- Effect corner radii use the affected window's scale and round to nearest after resolving square/default
  policy. Blur regions use that scale and expand outward after the client-to-window offset is applied.
  Already applied effects refresh on scale changes during bridge polling. Peek sends only target/keep handles
  and toggles existing scene visibility; it has no pixel geometry to convert. The protocol remains unchanged.
- This change has static validation only. Runtime checks still needed: WoW64 builds; destination/source at
  different 125%/150% scales; nonzero and negative crop origins; moving either window between outputs without
  updating thumbnail properties; corner/blur refresh; Peek activation/restoration; and scale-1 regression.

## Iconic Peek on Wayland (2026-11-04)

Iconic thumbnails and Peek bitmaps already use one backend-neutral path: `taskband_preview.c` copies the iconic
sections written by `DwmSetIconicThumbnail` / `DwmSetIconicLivePreviewBitmap` into GDI bitmaps and paints them in
ordinary shell popups (`show_iconic_peek`). On Wayland the popup's shared handle is published only after its first
commit, so `WexWmPeekWindow` returned `ERROR_NOT_READY` and Peek never started. `start_peek` now retries every 50 ms
for up to 1 s (`TIMER_PEEK_RETRY`); `end_peek` cancels it.

### Runtime verification (2026-11-04, WSL `wexbuild`, build `/root/WE-prefetch/build/wow64`)

Both backends PASS with `TaskbarPeek=1` and `TaskbarThumbnails=1`, probe
`scripts/dwm-iconic-visual-probe.c` (sets `DwmSetIconicThumbnail` +
`DwmSetIconicLivePreviewBitmap`, then minimizes).

**X11** (`/root/p4-visual.sh`, `HOVER='186 753' HOVER2='194 670'`):

```
PROBE=dwm-iconic-visual-probe PEEK=1 TB=1 COMP=1 FX=1 MAT=1 DA=1 \
    HOVER='186 753' HOVER2='194 670' bash /root/p4-visual.sh on
```

Probe log: `WM_DWMSENDICONICTHUMBNAIL`, `DwmSetIconicThumbnail hr 0`,
`DwmSetIconicLivePreviewBitmap hr 0`. Peek screenshot: bitmap `(250,200)=srgb(0,224,224)`
(cyan iconic bitmap shown), border `(75,200)=srgb(0,0,128)`, and the live probe
`(700,200)=srgb(37,111,149)` (desktop showing through → other window hidden). On
leave, the live probe returns to `(700,200)=srgb(255,255,255)` (visible) and the peek
area reverts to the desktop. The first `WexWmPeekWindow` succeeds on X11 (no retry
needed). PASS.

**Wayland** (`/root/p5-peek-lf.sh`, a `p5-wayland.sh` copy that also sets
`TaskbarPeek`/`TaskbarThumbnails` and keeps the compositor alive; hover driven by
`scripts/wayland-pointer-move.c`, a `zwlr_virtual_pointer_v1` client):

```
KEEP=1 HOLD=120 PROBE=dwm-iconic-visual-probe FX=1 MAT=1 DA=1 OUT=p5-peek \
    bash /root/p5-peek-lf.sh
# then, from the printed tempdir:
XDG_RUNTIME_DIR=$R WAYLAND_DISPLAY=wayland-0 /root/wayland-pointer-move 59,705,7000
```

Peek screenshot: the iconic bitmap fills the output (`(640,330)=(37,111,149)`, the
compositor's rendering of the cyan bitmap — identical with and without the DWM
`FX`/`MAT`/`DA` gates, so the shift is the pixman color path, not an effect), the
taskbar button is highlighted `(59,705)=(251,251,251)`, and the desktop is otherwise
hidden. On leave the output returns to black `(0,0,0)` and the button un-highlights.
Because the Wayland popup handle is published only after its first commit, the Peek
appearing at all exercises the `ERROR_NOT_READY` → 50 ms retry path in
`start_peek`/`retry_peek`. PASS.

Note: `scripts/wayland-pointer-move.c` initially failed with the compositor rejecting
`create_virtual_pointer` ("invalid arguments"). The cause was the request argument
order: the protocol is `create_virtual_pointer(seat, id)` (nullable `wl_seat` first,
then the new id), and the marshal call must pass the seat object before the trailing
NULL. Fixed in the probe; the compositor (`wexcomp` `src/seat.c:559`) accepts the
correctly-ordered request.
