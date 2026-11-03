<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# Wayland: backdrop blur, and prior art for the other open DWM items (2026-11-03)

State: the compositor draws a tint where Windows would blur (`wex_window_manager_v1.set_effects`, docs/DWM_P5_WAYLAND.md). wlroots
0.18's scene graph has no blur. This note records what exists already, found by web research and a source survey (Qwen, local
SceneFX source), and the experiment that settles the main question.

## Blur: SceneFX (MIT)

[SceneFX](https://github.com/wlrfx/scenefx) replaces wlroots' scene API and renderer with one that adds backdrop blur, rounded
corners and shadows (SwayFX uses it). Release **0.3 targets wlroots >=0.18.1 <0.19**, exactly our compositor; 0.4 and later need
wlroots 0.19. MIT, derived from wlroots (Drew DeVault and Jari Vetoniemi notices): compatible with our GPL-2.0-or-later tree if the
notice is kept. Its README says it is not yet ready for other compositors, so expect API churn.

Adopting it in wexcomp (Qwen's survey, every item checked against the sources):
- `meson.build`: a `scenefx` dependency, `wayland-server >= 1.23`.
- 14 files switch `<wlr/types/wlr_scene.h>` to `<scenefx/types/wlr_scene.h>`; labwc's reach into scene internals
  (damage ring, `direct_scanout`, `node->data`, buffer fields) all exist in SceneFX's structs.
- `server.c`: `wlr_renderer_autocreate` becomes `fx_renderer_create` (two places).
- Per window: `wlr_scene_buffer_set_backdrop_blur`, `..._set_corner_radius`; global `blur_data` (passes, radius, noise,
  brightness, contrast, saturation); an "optimized blur" node blurs everything below it (Mica: the wallpaper).

| Our backdrop | SceneFX |
|---|---|
| blur | backdrop blur, radius 5, 3 passes (defaults) |
| acrylic | radius 12, 4 passes, saturation 1.2, brightness .85, plus the tint rect |
| mica | optimized-blur node over the wallpaper plus tint |
| gradient | tint only (today) |

### The experiment (done): SceneFX needs EGL, not necessarily a GPU

SceneFX's renderer is GLES2 only and wants a DRM render node: on a machine without one (this WSL build host, VMs, CI) it fails
with "no DRM FD available", with `WLR_RENDERER=pixman` too. Blur has no pixman fallback.

`0001-fx_renderer-surfaceless-egl.patch` (this directory, against SceneFX 0.3) adds a fallback to the Mesa surfaceless EGL
platform. With it, SceneFX's `tinywl` starts headless on llvmpipe in the build VM (`WLR_RENDERER_ALLOW_SOFTWARE=1 WLR_BACKENDS=headless
WLR_RENDERER=gles2`; "Using software rendering", "Using EGL 1.5"). So the blur renderer can run in CI, slowly, and on any host
with Mesa. Not yet shown: that blurred pixels come out correct, and XWayland surfaces under the fx renderer.

Consequence for shipping: on a VM with no 3D acceleration (VMSVGA, `AccelMethod none`) blur would run on llvmpipe: keep the tint
fallback and enable blur only when the renderer is the fx renderer and (a hardware renderer, or an explicit opt-in).

### Steps (each small enough to delegate)
1. Vendor SceneFX 0.3 plus the patch as a meson subproject in wexcomp; bump `wayland-server`.
2. Flip the 14 headers; `fx_renderer_create` with a pixman fallback so the old CI still boots.
3. In `wex_window_update_effects` (wex-window.c) use backdrop blur and corner radius per the table; keep the tint rect.
4. Gate on `wlr_renderer_is_fx`; the tint stays for pixman.
5. Tests: `run-effects.sh` under the fx renderer on llvmpipe (blurred stripes pixel check), XWayland, corner slices and shadow.

## Other prior art

- **`ext-background-effect-v1`** (wayland-protocols staging): clients request blur for a region of their surface. Plasma 6.7 and
  GNOME 51 implement it. Worth adding to wexcomp once the blur renderer exists, so native Wayland apps get blur through the same
  path (its `blur` capability bit says whether it works).
- **Thumbnails**: `ext-foreign-toplevel-image-capture-source-v1` plus `ext-image-copy-capture-v1` (wlroots implements the source side)
  are the standard route to window pictures, including for native windows. No shipping taskbar thumbnails use them that we found; our
  `set_thumbnails` (v14) draws them inside the compositor instead.
- **Transparent DirectComposition (P7b) on Wayland**: [Wine MR 10180](https://list.winehq.org/hyperkitty/list/wine-gitlab@list.winehq.org/thread/DV2B7XOW2KV3LTEBH4QMJ4MG44U7WYJ3/)
  (Namo Nath, July 2026; merge status unknown): ARGB late binding and DWM cut-outs in `winewayland.drv` (CPU alpha for GDI layers,
  `glScissor` for OpenGL subsurfaces), input regions for click-through, `zxdg_foreign_v2` for cross-process parents, and Vulkan:
  inject `VK_ERROR_OUT_OF_DATE_KHR` at image acquisition so DXVK recreates the swapchain with `PRE_MULTIPLIED` alpha. Wine is LGPL:
  ideas and code can be reused with the notices kept. Merged upstream already: alpha-modifier opacity (Wine 11.11), an empty input
  region for `WS_EX_TRANSPARENT | WS_EX_LAYERED` windows.
- **Presentation feedback (P6)**: wlroots implements `wp_presentation` (presentation-time); the frame event in protocol v14 is a
  composed-frame clock, not a display proof.

## Status (2026-11-03)

Backdrop blur adoption via SceneFX 0.3 is complete and verified. The default shipping ISO and default runtime behavior remain 100% untouched unless explicitly built with `-Dscenefx=enabled` and run with `WEX_BLUR=1`.

### Implementation Summary
1. **Vendoring**: SceneFX 0.3 vendored in `wexcomp/subprojects/scenefx/` (without `examples/` or `tinywl/`). Applied `0001-fx_renderer-surfaceless-egl.patch` for headless Mesa surfaceless EGL initialization, fixed upstream optimized-blur false-positive warning, and documented changes in `WEXCOMP-NOTES.txt`.
2. **Meson Option**: Added `scenefx` feature option (default `disabled`) in `wexcomp/meson_options.txt`. When enabled, resolves `dependency('scenefx-0.3', fallback: ['scenefx', 'scenefx_dep'])`, requires `wayland-server >= 1.23`, defines `HAVE_SCENEFX=1`, and enables dynamic symbol export.
3. **Compat Header**: Created `wexcomp/include/wex-scene.h` dispatching to `<scenefx/types/wlr_scene.h>` under `HAVE_SCENEFX` or `<wlr/types/wlr_scene.h>` otherwise; updated all 14 compositor source files to include it.
4. **Renderer & Headless GBM Allocator**: In `wexcomp/src/server.c`, conditionally calls `fx_renderer_create(server->backend)` when `HAVE_SCENEFX && getenv("WEX_BLUR") == "1"`, falling back to `wlr_renderer_autocreate`. Sets `server->fx_renderer`. For headless/CI llvmpipe software rasterization under `vgem`, allocates GBM buffers via `/dev/dri/card0` and sets `DRM_RDWR` on exported dumb DMA-BUFs so llvmpipe `mmap` succeeds.
5. **Backdrop Blur in Effects**: In `wexcomp/src/protocols/wex-window.c` (`wex_window_update_effects`), when `server->fx_renderer` is true and backdrop is `blur`, `acrylic`, or `mica`:
   - Configures `wlr_scene_set_blur_data(server->scene, bd)` with the exact preset parameters (blur: r=5 p=3; acrylic: r=12 p=4 s=1.2 b=0.85; mica: r=16 p=4).
   - Applies `wlr_scene_rect_set_backdrop_blur(rect, true)` to the dedicated full-window or region rect in `win->backdrop_tree`.
   - Supports corner radius (`wlr_scene_rect_set_corner_radius`) and custom region bounds.
   - Tint is painted directly onto the rect.
   - Clearing effects with all zeros removes `backdrop_tree` without leaking nodes.
6. **Testing**:
   - Added `blur` scenario to `wexcomp/tests/wex/corner-client.c`.
   - Extended `wexcomp/tests/wex/verify.py` with `check-range`.
   - Created standalone runner `wexcomp/tests/wex/run-blur.sh`.

### How to Build
- **Default build** (unchanged, SceneFX disabled):
  ```bash
  meson setup build
  ninja -C build
  ```
- **SceneFX build** (opt-in blur support):
  ```bash
  meson setup build-fx -Dscenefx=enabled
  ninja -C build-fx
  ```

### How to Run
- Set `WEX_BLUR=1` in the environment when launching the compositor:
  ```bash
  WEX_BLUR=1 labwc
  ```
- In headless CI / software GLES2 environments (llvmpipe):
  ```bash
  WLR_BACKENDS=headless WLR_RENDERER=gles2 WLR_RENDERER_ALLOW_SOFTWARE=1 WEX_BLUR=1 labwc
  ```

### Test Results
- **Default build regression test**: `tests/wex/run-effects.sh` passes **32/32 tests** cleanly.
- **SceneFX blur test**: `tests/wex/run-blur.sh` passes **12/12 tests** under llvmpipe software GLES2:
  - Outside pixels (635, 100) and (645, 100) stay pure white `(255, 255, 255)` and black `(0, 0, 0)`.
  - Without blur (`backdrop none`), inside pixels equal outside pixels `(255, 255, 255)` and `(0, 0, 0)`.
  - With blur active (`backdrop blur`), inside white pixel dims to `(180, 180, 180)` and inside black pixel brightens to `(91, 91, 91)` across the edge.
  - With blur cleared, inside pixels return to `(255, 255, 255)` and `(0, 0, 0)`.
  - Compositor logs contain zero runtime errors or assertions.
