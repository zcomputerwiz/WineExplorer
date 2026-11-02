# Desktop Window Manager & Aero/Fluent Visual System Plan (D5–D12)

This document extends `docs/WINLOGON_AND_DWM.md` (phases D1–D4), `docs/DWMAPI_BEHAVIOR.md`, and `docs/CHROMIUM_GRAPHICS_PLAN.md` into an actionable implementation plan for full Desktop Window Manager (DWM) support and Windows Vista through Windows 11 desktop visuals in WineExplorer.

**Core invariants (`CLAUDE.md`):**
- **XP parity stays the default:** `HKCU\Software\WineExplorer\Shell\Composition` defaults to `0`. When `0`, no compositor selection is claimed, `DwmIsCompositionEnabled` reports `FALSE` (or follows the XP/Vista toggle), and `uxtheme`/`win32u` render classic/Luna frames with zero regressions.
- **Open art only:** Proprietary Microsoft `.msstyles`, bitmaps, or binaries never enter this repo and are never named in commits (local-only reference files live in `../WineExplorer-local`).
- **Small stock-Wine delta:** Every change under `wine/` is recorded in `docs/STOCK_CHANGES.md` and built as combined WoW64 (`--enable-archs=i386,x86_64`).

---

## 1. Architecture

### 1.0 Decision: a thin `dwm.exe` service (2026-11-02)

The earlier rule "no `dwm.exe`" (`WINLOGON_AND_DWM.md`, `CHROMIUM_GRAPHICS_PLAN.md`) was made for XP parity. The owner now wants
the Windows Vista to Windows 11 look and full support for applications that use DWM, so the rule is replaced by this split:

- **`dwm.exe` (new, `src/programs/dwm`) is the Windows-facing state and policy service of a session.** It owns what Windows
  keeps in DWM: composition on/off and the colorization parameters, per-window DWM attributes (dark mode, backdrop, corner
  preference, caption/border/text colour, margins, blur-behind, cloak, iconic and peek flags), the thumbnail registry (a
  thumbnail registered by one process is visible to the compositor and to the shell), the frame clock behind `DwmFlush` and
  `DwmGetCompositionTimingInfo`, and, later, DirectComposition visual trees that are shared between processes.
- **The pixel compositor stays where it is**: `wexhost/compose.c` on X11, `wexcomp` on Wayland. `dwm.exe` tells it what to draw
  and receives frame and damage timing back; it never draws desktop pixels, never owns an X Composite selection and never
  owns a Wayland scene.
- **Applications must not depend on it.** `dwmapi.dll` and `dcomp.dll` work when `dwm.exe` is absent (composition reported off,
  state kept in-process, as on Windows with composition disabled), so the standalone Wine API paths still work outside our shell.
- Everything below (X property bridge, `compose.c` changes, thumbnails) describes the first target; where the plan stores
  state in window properties or a per-process table, that is the interim form that `dwm.exe` takes over. The transport between
  `dwmapi.dll` and `dwm.exe`, and between `dwm.exe` and the compositor, is part of the architecture work in
  `DWM_ARCHITECTURE.md`.

### 1.1 Where the Compositor Lives

| Backend | Process / Thread | Files & Key Functions Found in Repo | Role |
|---|---|---|---|
| **X11 (now: XRender)** | `explorer.exe` -> `wexhost.dll` (`wexhost.so` Unix thread shared with `x11wm.c`) | `src/dlls/wexhost/compose.c` (`compose_init_atoms`, `compose_set_enabled`, `compose_handle_event`, `compose_tick`, `compose_paint`, `draw_shadow`), `src/dlls/wexhost/x11wm.c` (`wm_thread`, `x_mutex`, `display`) | Claims `_NET_WM_CM_S<screen>`, redirects `root` and Wine `desktop` (`CompositeRedirectManual`), tracks `struct window` stacking list (`windows_head`/`windows_tail`), listens to `XDamageNotifyEvent`, composites window `Picture`s (`IncludeInferiors`) to `back_picture` -> `overlay_picture` (`XCompositeGetOverlayWindow` with empty `ShapeInput` via `XFixesSetWindowShapeRegion`). |
| **X11 (next: OpenGL)** | Same `wexhost.so` Unix thread in `compose.c` | **Not found** yet (`compose.c` is XRender-only today). Planned in `src/dlls/wexhost/compose_gl.c` called from `compose.c`. | Uses `GLX_EXT_texture_from_pixmap` (or EGL image) on the existing `NamedPixmap`s in `struct window` to run 2-pass dual-Kawase blur shaders (Aero Glass, Acrylic, Mica), rounded-corner SDF clipping, and optional Flip3D on the `overlay_window` GLX context, with automatic fallback to the existing XRender path. |
| **Wayland (`wexcomp`)** | External `wexcomp` process (labwc / wlroots fork) + `wexhost.so` bridge | `src/wexcomp/src/wex_wm.c`, `src/wexcomp/protocols/wex-window-manager-unstable-v1.xml` (v11), `src/dlls/wexhost/waylandwm.c` (`wayland_wm_start`), `src/dlls/wexhost/waylandhost.c` (`host_binding`, `update_mask`) | `wexcomp` owns the wlroots `wlr_scene_tree`. `waylandhost.c` punches a client-area hole (`WEX_WAYLAND_FRAME_OUTER_PROP` + `SetWindowRgn`) in the Win32 frame so `wexcomp` composites native surfaces inside Win32 frames. DWM attributes will extend `zwex_window_v1` in `wex-window-manager-unstable-v1.xml`. |

**Entry points found:** `src/dlls/wexhost/wexhost.c` exports `WexCompositionSetEnabled`, `WexCompositionGetEnabled`, and `WexCompositionSetShadow`, bridged via `__wine_unix_call` (`unix_composition_set`, `unix_composition_get`, `unix_composition_shadow` in `src/dlls/wexhost/wm.c` and `src/include/wexhost.h`). `src/dlls/wexshell/desktop.c:WexShellStart` (line 1342) reads `HKCU\Software\WineExplorer\Shell\Composition` and calls `WexCompositionSetEnabled(TRUE)` when non-zero.

### 1.2 How Win32 Window State (`dwmapi`) Reaches the Compositor

**Current state in `wine/dlls/dwmapi/dwmapi_main.c`:**
- `DwmSetWindowAttribute` / `DwmGetWindowAttribute` (D4 step 1, merged on `dwmapi-d4`) validate arguments and store 4-byte attributes as Win32 window properties via `SetPropW(hwnd, L"WexDwmAttr%d", (HANDLE)((ULONG_PTR)value + 1))` (`set_stored_attribute` / `get_stored_attribute`, lines 77–102).
- Stored attribute IDs: `DWMWA_NCRENDERING_POLICY` (2), `DWMWA_TRANSITIONS_FORCEDISABLED` (3), `DWMWA_ALLOW_NCPAINT` (4), `DWMWA_FORCE_ICONIC_REPRESENTATION` (7), `DWMWA_FLIP3D_POLICY` (8), `DWMWA_HAS_ICONIC_BITMAP` (10), `DWMWA_DISALLOW_PEEK` (11), `DWMWA_EXCLUDED_FROM_PEEK` (12), `DWMWA_CLOAK` (13), `DWMWA_FREEZE_REPRESENTATION` (15), `DWMWA_PASSIVE_UPDATE_MODE` (16), `DWMWA_USE_HOSTBACKDROPBRUSH` (17), `DWMWA_USE_IMMERSIVE_DARK_MODE` (19 & 20), `DWMWA_WINDOW_CORNER_PREFERENCE` (33), `DWMWA_BORDER_COLOR` (34), `DWMWA_CAPTION_COLOR` (35), `DWMWA_TEXT_COLOR` (36), `DWMWA_SYSTEMBACKDROP_TYPE` (38), and `DWMWA_MICA_EFFECT` (1029).
- `DwmExtendFrameIntoClientArea` (line 202) and `DwmEnableBlurBehindWindow` (line 132) currently only validate pointers/handles and return `S_OK` without persisting `MARGINS` or `DWM_BLURBEHIND`.
- Undocumented `SetWindowCompositionAttribute` / `NtUserSetWindowCompositionAttribute` (`WCA_ACCENT_POLICY`, used by Windows 10 Acrylic apps and Chromium): **not found** in `wine/dlls/user32` or `wine/dlls/win32u`.

**Transport Decision: X11 Window Properties set by `winex11.drv` (NOT D-Bus to `wexhost`).**

**Justification:**
1. **Cross-process HWND-to-XID resolution:** `dwmapi.dll` runs inside every client application process (`chrome.exe`, `notepad.exe`, etc.), whereas `compose.c` runs inside `explorer.exe`. As already encountered in D2 (`docs/STOCK_CHANGES.md`, `src/dlls/wexshell/desktop.c:67`, `wine/dlls/winex11.drv/window.c:2454` `sync_window_shadow`), cross-process `GetPropW(hwnd, L"__wine_x11_whole_window")` is not reliably accessible from `explorer.exe`, whereas `winex11.drv` inside the owning process always holds `struct x11drv_win_data *data` and `data->whole_window`.
2. **D-Bus in `wexhost` is for Linux system daemons, not display sync:** `src/dlls/wexhost/unixlib.c` connects to `DBUS_BUS_SYSTEM` (`login1`, `timedate1`, `UPower`, `NetworkManager`, `AccountsService`, `polkit`). Using D-Bus for per-window geometry/margins/blur regions would require a new session-bus service, extra thread locking, and out-of-band XID correlation that races with X11 `MapNotify`, `ConfigureNotify`, and `DamageNotify`.
3. **Atomic X stream ordering & existing pattern:** `winex11.drv/window.c:sync_window_shadow` already sets `_WEX_DROPSHADOW` on `data->whole_window`, and `compose.c:compose_handle_event` (line 663) already handles `PropertyNotify` (`refresh_window_shadow`). Extending this pattern to `_WEX_DWM_STATE`, `_WEX_DWM_MARGINS`, and `_WEX_DWM_BLUR` keeps all window state synchronized with X11 drawing in the same protocol stream.

**Concrete Property Contract (`winex11.drv` -> `compose.c`):**
- When `dwmapi.dll` updates an attribute, `MARGINS`, or `DWM_BLURBEHIND`, it writes `SetPropW(hwnd, L"WexDwm...", ...)` and triggers a lightweight `SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED)` (or `NtUserSetProp` hook in `winex11.drv/window.c:X11DRV_WindowPosChanged`).
- `winex11.drv/window.c` (`sync_window_dwm_state`, alongside `sync_window_shadow` at line 2454) reads the `WexDwm*` properties via `NtUserGetProp` and updates X11 atoms on `data->whole_window`:
  - `_WEX_DWM_STATE` (`XA_CARDINAL`, 10-element `uint32_t` array): `[0] flags` (bitmask: `NCRP_ENABLED`, `CLOAKED`, `DARK_MODE`, `FORCE_ICONIC`, `HAS_ICONIC_BMP`, `DISALLOW_PEEK`, `EXCLUDED_FROM_PEEK`, `FREEZE_REP`, `HOST_BACKDROP`, `TRANSITIONS_DISABLED`), `[1] corner_pref` (`DWM_WINDOW_CORNER_PREFERENCE`), `[2] backdrop_type` (`DWM_SYSTEMBACKDROP_TYPE`), `[3] border_color` (`COLORREF`), `[4] caption_color` (`COLORREF`), `[5] text_color` (`COLORREF`), `[6..9] margins` (`cxLeftWidth`, `cxRightWidth`, `cyTopHeight`, `cyBottomHeight` as signed `int32_t`).
  - `_WEX_DWM_BLUR` (`XA_CARDINAL` array): `[0] enable_flags`, `[1..4*N] blur_rects` extracted via `GetRegionData` when `DWM_BB_BLURREGION` is set.
- In `compose.c`, `struct window` gains `dwm_flags`, `corner_pref`, `backdrop_type`, `border_color`, `caption_color`, `text_color`, `margins[4]`, and `blur_region`. If `dwm_flags & WEX_DWM_CLOAKED`, `compose_paint` skips the window (`!w->mapped || w->cloaked`).

### 1.3 Non-Client Area Drawing (`uxtheme`/`win32u` vs DWM Frame)

**How it works today:**
- `wine/dlls/win32u` delegates themed non-client painting to `wine/dlls/uxtheme/window.c:UXTHEME_NonClientFrameDraw` (line 218) and window shaping to `UXTHEME_SetWindowRgn` (`nc_update_region`, line 130).
- `UXTHEME_NonClientFrameDraw` opens `OpenThemeDataEx(hwnd, L"Window", OTD_NONCLIENT)` and draws `WP_CAPTION`/`WP_MAXCAPTION`/`WP_SMALLCAPTION`, `WP_FRAMELEFT`, `WP_FRAMERIGHT`, `WP_FRAMEBOTTOM`, and 3 caption buttons (`WP_CLOSEBUTTON`, `WP_MAXBUTTON`/`WP_RESTOREBUTTON`, `WP_MINBUTTON`).
- At `wine/dlls/uxtheme/window.c:250`, if `IsThemeBackgroundPartiallyTransparent(hTheme, caption_part, active_state)` is true, it fills behind the caption with `GetSysColorBrush(COLOR_BTNFACE)` (line 258), destroying per-pixel alpha! Furthermore, `nc_update_region` applies a 1-bit `GetThemeBackgroundRegion` via `SetWindowRgn`.
- `uxtheme!IsCompositionActive` (`wine/dlls/uxtheme/system.c:577`) is currently a stub returning `FALSE`.
- Support for the `DWMWindow` theme class (`OpenThemeData(hwnd, L"DWMWindow")`, used by Vista/7/8/10/11 `.msstyles` for composited frames, button glyphs, and title glow): **not found** in `wine/dlls/uxtheme/`.

**Target DWM-Style Frame Split:**
- **Keep hit-testing, caption layout, and button state tracking in `uxtheme/window.c` + `win32u`** so Win32 messages (`WM_NCHITTEST`, `WM_NCPAINT`, `WM_NCACTIVATE`, `DwmDefWindowProc`) remain 100% compatible with applications that subclass `WM_NCCALCSIZE` (e.g. Chromium, VS Code, Office).
- **Make `IsCompositionActive()` in `wine/dlls/uxtheme/system.c` real:** Check `HKCU\Software\Microsoft\Windows\DWM\Composition` (synced by `wexshell` when `WexCompositionSetEnabled` runs).
- **When `IsCompositionActive()` is `TRUE` and `DWMNCRP_DISABLED` is not set on `hwnd`:**
  1. `nc_update_region` (`uxtheme/window.c:130`) skips the 1-bit `SetWindowRgn` call so `compose.c` can apply smooth 8-bit anti-aliased corner masks.
  2. `winex11.drv/window.c` selects the 32-bit `argb_visual` (`set_window_visual(data, &argb_visual, TRUE)`) for top-level windows with translucent frames, extended margins (`MARGINS != 0`), or blur-behind.
  3. In `UXTHEME_NonClientFrameDraw` (`uxtheme/window.c`):
     - If `DWMWA_CAPTION_COLOR` (`WexDwmAttr35`) is set (and not `DWMWA_COLOR_DEFAULT`/`DWMWA_COLOR_NONE`), or `DWMWA_USE_IMMERSIVE_DARK_MODE` (`WexDwmAttr20`) is `1` on a flat/Fluent theme, fill the frame/caption with the solid dark/custom `COLORREF` and draw caption text using `DWMWA_TEXT_COLOR` (`WexDwmAttr36`).
     - If the active `.msstyles` defines `DWMWindow` or has translucent `Window` parts (Aero Glass / Mica / Acrylic), clear the frame/extended margin rects to `0x00000000` (transparent black) instead of `COLOR_BTNFACE` before calling `DrawThemeBackground`, so the alpha channel passes through to `compose.c`.

### 1.4 Shadows, Rounded Corners, Mica/Acrylic Blur, and Vista/7 Translucent Glass

- **Shadows (`src/dlls/wexhost/compose.c:167-252`):**
  - Today, `make_shadow_tile` and `draw_shadow` draw a 9-slice `PictStandardA8` shadow (`SHADOW_RADIUS 12`, `SHADOW_OFFSET 4`, `SHADOW_ALPHA 0x59`) only when `w->has_shadow` (`_WEX_DROPSHADOW`) is set (`CS_DROPSHADOW` popups).
  - Under DWM mode, `winex11.drv/window.c:sync_window_shadow` also sets `_WEX_DROPSHADOW = 1` (active) or `2` (inactive) on normal decorated top-level windows (`WS_CAPTION | WS_THICKFRAME`, not maximized, not `WS_CHILD`, not `DWMNCRP_DISABLED`, not `DWMWA_BORDER_COLOR == DWMWA_COLOR_NONE`). `compose.c` maintains two shadow tile sets: active (`radius=18, offset_y=6, alpha=0x66`) and inactive (`radius=12, offset_y=3, alpha=0x38`).
- **Rounded Corners (Windows 11, `DWMWA_WINDOW_CORNER_PREFERENCE`):**
  - Radii at 96 DPI (scaled by DPI): `DWMWCP_DEFAULT` (0) / `DWMWCP_ROUND` (2) -> `8px` for standard top-level windows; `DWMWCP_ROUNDSMALL` (3) -> `4px` (toolwindows/popups); `DWMWCP_DONOTROUND` (1) -> `0px`.
  - In `compose.c`, pre-render four `PictStandardA8` anti-aliased quarter-circle corner masks for `4px` and `8px`. During `compose_paint`, copy the window into a scratch ARGB picture (or apply `PictOpIn` with the 9-slice corner mask) and stroke a 1px anti-aliased border (`DWMWA_BORDER_COLOR`, or theme default `#75757540` dark / `#20202020` light).
- **Mica (`DWMSBT_MAINWINDOW = 2`, `DWMSBT_TABBEDWINDOW = 4`, `DWMWA_MICA_EFFECT = 1029`):**
  - Mica samples **only the desktop wallpaper** (not windows behind the app). `compose.c` captures the desktop background once whenever the wallpaper or screen size changes, downscales it `8x` (`1/8` width and height), runs a 3-pass separable box blur, blends the theme base tint (`#F3F3F3` at 80% in light mode, `#202020` at 80% in dark mode; `DWMSBT_TABBEDWINDOW` mixes `ColorizationColor` at 15%), and caches the full-screen `mica_light_picture` / `mica_dark_picture`.
  - Per frame, drawing Mica behind a window with extended margins or `DWMWA_SYSTEMBACKDROP_TYPE` is a single `XRenderComposite(PictOpOver, mica_picture, None, back_picture, w->x, w->y, ...)` clipped to the window's glass/margin region — **zero per-frame blur cost** even on XRender!
- **Translucent Glass (Vista/7 Aero) & Acrylic (`DWMSBT_TRANSIENTWINDOW = 3`, `DwmEnableBlurBehindWindow`, `SetWindowCompositionAttribute`):**
  - Requires blurring the current `back_picture` (which already contains lower windows rendered earlier in the bottom-to-top pass).
  - For the window's glass region (non-client frame + `MARGINS` from `_WEX_DWM_STATE` + `_WEX_DWM_BLUR` rects, plus `wexshell` Start menu/taskbar when `Main_opacity=glass/fullglass` in `src/dlls/wexshell/startskin.c`), `compose.c` copies the bounding box from `back_picture` into a `1/4`-scale scratch `Picture`, runs horizontal + vertical blur passes (XRender convolution filter `XRenderSetPictureFilter(..., "convolution", ...)` or GL dual-Kawase shader), upscales back with `FilterBilinear`, composites `ColorizationColor` (`HKCU\Software\Microsoft\Windows\DWM\ColorizationColor`), and for Vista/7 Aero overlays a diagonal specular reflection strip (`PictOpAdd`, alpha `0x18`) before `PictOpOver` compositing the window's own `Picture`.

### 1.5 Visual-Style Files Needed (Existing vs Open-Art Authoring)

**What exists in this repo today:**
- `wine/dlls/uxtheme/msstyles_v4.c`: Native loader for Vista/7/8/10/11 v4 `.msstyles` (`PACKTHEM_VERSION = 4`, `CMAP`/`VMAP`/`AMAP`/`RMAP`/`BCMAP`).
- Stock Wine `light.msstyles` (v3 format) and `assets/iconpacks/fluent.json` (69 shell32 icons).
- Open-art `.msstyles` for Aero, Aero Basic, Windows 8/10 flat, and Windows 11 Fluent: **not found** in the repo (reference themes exist only outside the repo in `../WineExplorer-local`).

**What must be authored with open art (`assets/themes/` + build tool `tools/mkmsstyles`):**
We will add a small clean-room packager (`tools/mkmsstyles.py`) that compiles SVG-rendered PNG atlases + INI metric tables into redistributable v3/v4 `.msstyles` and `.theme` files under `assets/themes/`:

| Theme File | Target Era | Non-Client & Control Characteristics (Open Art) |
|---|---|---|
| `aerobasic.msstyles` + `aerobasic.theme` | Vista / 7 Basic (`Composition=0` or `Tier 0`) | Opaque slate-blue top caption (`26px` height, `8px` side borders), pill-shaped caption button group (min/max/red-close glow), Vista toolbar/button/tab/progress metrics. |
| `aero.msstyles` + `aero.theme` | Vista / 7 Aero Glass (`DwmMode=2`) | Semi-transparent alpha-channel frame (`WP_CAPTION` alpha `0x40` with 1px inner white highlight and outer dark rim), glowing red close button (`WP_CLOSEBUTTON`), white/black text glow (`DWMWindow` / `TEXTGLOWSIZE=12`), taskbar & Start menu glass (`Main_opacity=glass` in `startskin.c`). |
| `flat10.msstyles` + `flat10.theme` | Windows 8 / 10 (`DwmMode=3`) | `1px` top/side/bottom visible border (`7px` invisible resize gutter handled in `x11wm.c`), flat `30px` caption bar, wide `45x29` rectangular caption buttons (hover `#E5E5E5` / close `#E81123`), flat controls. |
| `fluent11.msstyles` + `fluent11.theme` | Windows 11 (`DwmMode=4`) | `32px` Mica-ready caption bar (`0x00000000` fill when Mica active), Segoe Fluent-style geometric vector button glyphs (`46x30`), `4px`-rounded pushbuttons/edits/combos, dark & light color variants. |

---

## 2. Thumbnails, Previews, Aero Peek, Flip3D, and Iconic Bitmaps

### 2.1 Live `DwmRegisterThumbnail` Rendering
- **Current state (`wine/dlls/dwmapi/dwmapi_main.c:41-61, 275-425`):** `thumbnail_table[64]` (`THUMBNAIL_ENTRY`) is a process-local array in `dwmapi.dll`. `DwmRegisterThumbnail`, `DwmUpdateThumbnailProperties`, and `DwmQueryThumbnailSourceSize` validate `dest`/`src` `HWND`s and store `DWM_THUMBNAIL_PROPERTIES` in memory, with **no compositor output**.
- **Design:**
  1. **Retain minimized window pixmaps in `compose.c`:** Currently `compose_handle_event` (`UnmapNotify`, line 628) calls `free_window_picture(w)`. Change `UnmapNotify` so that if a window was previously mapped and is only minimized (`_NET_WM_STATE_HIDDEN` / iconic, not `DestroyNotify`), `w->pixmap` and `w->picture` are kept alive as `w->last_picture` until the window is destroyed or remapped.
  2. **Publish thumbnail table on `dest` X11 window:** When `DwmUpdateThumbnailProperties` or `DwmUnregisterThumbnail` is called in `dwmapi_main.c`, serialize all active visible `thumbnail_table` entries targeting `dest` (`fVisible != FALSE`) into a window property `WexDwmThumbnails` on `dest`. `winex11.drv/window.c` resolves each `src` `HWND` to its X11 `whole_window` XID (`NtUserGetProp(src, L"__wine_x11_whole_window")`) and writes `_WEX_DWM_THUMBNAILS` (`XA_CARDINAL`, 10 `uint32_t`s per thumbnail: `src_xid, dst_x, dst_y, dst_w, dst_h, src_x, src_y, src_w, src_h, opacity_and_flags`) onto `dest`'s `whole_window`.
  3. **Composite scaled thumbnails in `compose_paint` (`compose.c`):** Right after compositing `dest`'s `w->picture` onto `back_picture`, iterate `w->thumbnails[i]`. Find `src_w = find_window(thumb->src_xid)` (using `src_w->picture` or `src_w->last_picture` or `src_w->iconic_picture`). Set `XRenderSetPictureFilter(display, src_pic, FilterBilinear, NULL, 0)` and an `XTransform` scaling `(src_rect.w / dst_rect.w, src_rect.h / dst_rect.h)` (if `DWM_TNP_SOURCECLIENTAREAONLY`, offset `src_rect` by `src_w`'s client inset), and call `XRenderComposite(PictOpOver, src_pic, alpha_mask, back_picture, ..., w->x + dst_x, w->y + dst_y, dst_w, dst_h)`.

### 2.2 Taskbar & Alt+Tab Previews (`wexshell`)
- **Taskbar hover previews (`src/dlls/wexshell/taskband.c`):**
  - Today, `taskband.c` has no hover thumbnail popup, and clicking a grouped button opens a classic popup menu (`show_group_list` at line 682).
  - Add a popup window class `WineExplorerTaskPreview` in `taskband.c`. When `WexCompositionGetEnabled()` is `TRUE` and `TaskbarThumbnails != 0`, `WM_MOUSEMOVE` starts a `400ms` hover timer (`ID_TIMER_PREVIEW`). When it fires (or when clicking a grouped button), show a glass/Mica-styled `WineExplorerTaskPreview` card (`200x140` per window in the group, with `16x16` icon, title, close `[X]` button on hover, and a live `DwmRegisterThumbnail(preview_hwnd, item->hwnd, &hThumb)` inside `rcDestination`).
- **Alt+Tab previews (`src/dlls/wexshell/alttab.c`):**
  - Today, `alttab.c` renders the XP 7x3 `32x32` icon grid (`COLS_MAX 7`, `CELL_SIZE 43`, `paint` at line 153).
  - When `WexCompositionGetEnabled()` is `TRUE` and `AltTabStyle == 1`, `open_switcher` (`alttab.c:218`) switches layout metrics to `CELL_W = 168, CELL_H = 124` (`COLS_MAX = 5`), registers a `HTHUMBNAIL` for each `windows[i].hwnd` onto `switcher_hwnd` (`rcDestination` = inner `152x92` rect of cell `i`), and unregisters all thumbnails in `close_switcher` (`alttab.c:296`).

### 2.3 Aero Peek & Flip3D
- **Aero Peek:**
  - Add a `12px` "Show desktop" strip (`DesktopPeekRect`) at the far right of `src/dlls/wexshell/desktop.c` (next to the tray clock) when `DwmMode >= 2`, and hook `WM_MOUSEMOVE` dwell (`500ms`) on taskbar thumbnail cards in `taskband.c`.
  - When Peek activates, `wexshell` sets `_WEX_DWM_PEEK_TARGET` (`XA_WINDOW`) on the Wine `desktop` X11 window (`0xFFFFFFFF` for "Show desktop", or `target_xid` for single-window peek).
  - In `compose.c:compose_paint`, when `peek_target != None`, any top-level window `w` whose XID `!= peek_target` (unless `w->dwm_flags & (WEX_DWM_EXCLUDED_FROM_PEEK | WEX_DWM_DISALLOW_PEEK)`) is skipped for interior fill and drawn only as a `1px` translucent glass wireframe outline (`alpha = 0x40`).
- **Flip3D (Optional, OpenGL backend only):**
  - In `src/dlls/wexshell/desktop.c:winkey_hook` (line 97), add `Win+Tab` handling. When the GL backend in `compose_gl.c` is active, `Win+Tab` toggles `_WEX_DWM_FLIP3D` on `desktop`; `compose_gl.c` renders the `windows_head` stack as a diagonal 3D cascade (`glUniformMatrix4fv` perspective projection) cycled by `MouseWheel` / `Tab` / `Left` / `Right`.

### 2.4 Iconic Bitmaps (`DwmSetIconicThumbnail` / `DwmSetIconicLivePreviewBitmap`)
- **Current state (`wine/dlls/dwmapi/dwmapi_main.c:487-527`):** `DwmSetIconicThumbnail`, `DwmSetIconicLivePreviewBitmap`, and `DwmInvalidateIconicBitmaps` check `DWMWA_FORCE_ICONIC_REPRESENTATION` (`WexDwmAttr7 == 1`) and return `S_OK` (or `DWM_E_COMPOSITIONDISABLED` / `E_INVALIDARG`), discarding `hbmp`.
- **Implementation:**
  - When `DwmInvalidateIconicBitmaps(hwnd)` is called or `taskband.c` opens a preview for a window with `WexDwmAttr7 == 1` (or `WexDwmAttr10 == 1`), `dwmapi`/`taskband.c` posts `WM_DWMSENDICONICTHUMBNAIL` (`0x0323`, `lParam = MAKELPARAM(max_w, max_h)`) and `WM_DWMSENDICONICLIVEPREVIEWBITMAP` (`0x0326`) to `hwnd`.
  - In `DwmSetIconicThumbnail(hwnd, hbmp, flags)` and `DwmSetIconicLivePreviewBitmap(hwnd, hbmp, pptClient, flags)`, call `GetDIBits` to extract the 32-bit BGRA pixels from `hbmp`, store them in a shared property/pixmap (`_WEX_DWM_ICONIC_PIXMAP`) via `winex11.drv`, and bind that pixmap as `w->iconic_picture` in `compose.c`.

---

## 3. Application Surface Path (DirectComposition, DXGI, GL/Vulkan Child Windows)

### 3.1 What Exists in the Fork Today (`CHROMIUM_GRAPHICS_PLAN.md` G3/G4)
- **DirectComposition (`wine/dlls/dcomp/device.c`, `wine/include/wine/dcomp_layer.h`, `wine/include/wine/dcomp_underlay.h`):**
  - `dcomp_device_internal_commit` (`device.c:830`) walks the `IDCompositionVisual2` tree and flattens up to `WINE_DCOMP_MAX_LAYERS` (128) and `WINE_DCOMP_MAX_CONTENTS` (32) into `struct wine_dcomp_target_scene`, including 3x2 transforms, `IDCompositionRectangleClip` rounded corner radii (`radius_tl_x`..`radius_br_y`), and `IDCompositionEffectGroup` opacity.
  - `dcomp_target_create` sets `WINE_DCOMP_UNDERLAY_PROP` (`L"__wine_dcomp_underlay"`) on the target `HWND` and sends `WM_WINE_DCOMP_SET_TARGET` (`0x8000 + 0x5d1`) to attach DXGI swapchains (`IWineDCompSwapChain::set_target_scene`).
  - `wine/dlls/win32u/dce.c:882` checks `NtUserGetProp(dce->hwnd, WINE_DCOMP_UNDERLAY_PROP)` so Win32/GDI drawing on that `HWND` renders into `win->surface` as an overlay while the DComp swapchain composites underneath.
- **X11 GL/Vulkan client surfaces (`wine/dlls/winex11.drv/init.c:313-410`):**
  - `X11DRV_CreateClientSurface` creates a child X11 `client_window` inside `whole_window`.
  - `client_surface_update_offscreen` (`init.c:313`) only calls `pXCompositeRedirectWindow(gdi_display, surface->window, CompositeRedirectManual)` and reparents `surface->window` to `get_dummy_parent()` when `needs_offscreen_rendering` is true (child window clipping, DPI scaling, or window region). When `X11DRV_client_surface_present` runs, it blits the redirected offscreen `client_window` into `toplevel`'s `whole_window` via `NtGdiStretchBlt`.

### 3.2 Coexistence with `wexhost/compose.c` (Zero Second Compositor Selection Owner)
1. **Why there is no XComposite conflict:**
   - X11 forbids two clients from owning `_NET_WM_CM_S0` or calling `XCompositeRedirectSubwindows(root, CompositeRedirectManual)`. **Only `wexhost/compose.c` does either.**
   - `winex11.drv/init.c:340` calls `XCompositeRedirectWindow(gdi_display, surface->window, CompositeRedirectManual)` on an individual internal `client_window` reparented under `get_dummy_parent()`. XComposite allows per-window manual redirection on non-root subtrees concurrently with a root compositor!
2. **Direct-attached vs offscreen client surfaces:**
   - When `needs_offscreen_rendering` is `FALSE` (common for top-level GL/Vulkan/DXGI swapchains), `client_window` is an unredirected child of `whole_window`. Because `compose.c:ensure_window_picture` (line 368) sets `pa.subwindow_mode = IncludeInferiors`, `whole_window`'s redirected pixmap automatically contains `client_window`'s rendered pixels and delivers `DamageNotify` to `compose.c` on every `glXSwapBuffers` / `vkQueuePresentKHR`.
   - When `needs_offscreen_rendering` is `TRUE` (or when `dcomp` composites a multi-layer `wine_dcomp_target_scene`), `X11DRV_client_surface_present` blits into `whole_window`, which immediately fires `DamageNotify` on `whole_window` in `compose.c`.
3. **Required bridge for transparent DComp / Mica / Acrylic windows (G5):**
   - Today, `winex11.drv` creates normal top-level `whole_window`s with 24-bit `default_visual` unless layered (`WS_EX_LAYERED`) or per-pixel alpha is active. When Chromium/Edge or a WinUI app calls `DwmExtendFrameIntoClientArea` (`MARGINS = {-1,-1,-1,-1}`) or sets `DWMWA_SYSTEMBACKDROP_TYPE` (`DWMSBT_MAINWINDOW` / `DWMSBT_TABBEDWINDOW`) + `IDCompositionDevice::CreateTargetForHwnd`, `whole_window` **must** use the 32-bit `argb_visual` so premultiplied alpha (`0x00000000` in the tab strip) lets `compose.c`'s Mica/Acrylic/Glass backdrop show through behind the DComp visual tree.

---

## 4. Settings Model & GPU Capability Gating

### 4.1 Registry Schema
We maintain two synchronized registry keys so both WineExplorer shell components (`wexshell`, `wexhost`) and third-party Windows apps querying standard DWM keys see consistent values:

**1. `HKCU\Software\WineExplorer\Shell` (Master WineExplorer configuration):**
| Value Name | Type | Default | Meaning |
|---|---|---|---|
| `Composition` | `REG_DWORD` | `0` | `0` = XP parity (compositor off, default); `1` = DWM compositor enabled. (Already read in `src/dlls/wexshell/desktop.c:1342`.) |
| `DwmMode` | `REG_DWORD` | `0` | `0` = Auto (from active `.theme`), `1` = XP/Basic (shadows only), `2` = Vista/7 Aero Glass, `3` = Win8/10 Flat + Acrylic, `4` = Win11 Fluent (Mica + Rounded Corners). |
| `CompositionBackend` | `REG_DWORD` | `0` | `0` = Auto (OpenGL if Tier 2 GPU, else XRender), `1` = Force XRender, `2` = Force OpenGL. |
| `EnableBlur` | `REG_DWORD` | `1` | `0` = Replace live glass/Acrylic blur with translucent tint (`ColorizationOpaqueBlend`); `1` = Enable blur (subject to GPU tier). |
| `TaskbarThumbnails` | `REG_DWORD` | `1` | `0` = Classic text group menu in `taskband.c`; `1` = Live DWM hover thumbnails when `Composition=1`. |
| `AltTabStyle` | `REG_DWORD` | `1` | `0` = XP 32x32 icon grid in `alttab.c`; `1` = Live DWM thumbnail grid when `Composition=1`. |

**2. `HKCU\Software\Microsoft\Windows\DWM` (Standard Windows DWM key, already read by `wine/dlls/dwmapi/dwmapi_main.c:366`):**
- `Composition` (`REG_DWORD`, synced to `Shell\Composition` by `wexshell`), `ColorizationColor` (`REG_DWORD`, default `0xC40078D7`), `ColorizationAfterglow` (`REG_DWORD`, default `0xC40078D7`), `ColorizationOpaqueBlend` (`REG_DWORD`, `0` when blur active, `1` when opaque/low-GPU), `EnableAeroPeek` (`REG_DWORD`, default `1`), `ColorPrevalence` (`REG_DWORD`, default `0`).

### 4.2 Per-Theme Defaults & XP Mode vs Aero Mode Switch
- When the user applies a theme in Display Properties (`src/dlls/themeui` / `wextheme`):
  - **Luna / Classic / Royale / Zune / Embedded / Aero Basic:** Sets `Composition = 0` (unless user explicitly checked "Enable desktop composition" in Advanced settings), `DwmMode = 1`, `uxtheme` uses classic/v3 path, and `wexshell` broadcasts `WM_DWMCOMPOSITIONCHANGED` (`0x031E`).
  - **Aero (Vista/7):** Sets `Composition = 1`, `DwmMode = 2`, `ColorizationColor = 0x6B74B8FC`.
  - **Windows 8 / 10 Flat:** Sets `Composition = 1`, `DwmMode = 3`, `ColorizationColor = 0xC40078D7`.
  - **Windows 11 Fluent:** Sets `Composition = 1`, `DwmMode = 4`, `ColorizationColor = 0xC40078D7`.

### 4.3 Graphics-Capability Gating (Old Intel / Low-End GPUs)
In `src/dlls/wexhost/compose.c:compose_set_enabled`, classify the display server into three capability tiers before enabling expensive passes:
- **Tier 0 (No XComposite 0.2+, XRender, XDamage, or XFixes):** `compose_set_enabled` returns `FALSE`, `Composition` falls back to `0`, and `DwmIsCompositionEnabled` reports `FALSE`.
- **Tier 1 (Software X11 / `llvmpipe` / Xephyr / no `GLX_EXT_texture_from_pixmap`):** Enables XRender composition, 9-slice drop shadows, 8-bit rounded corner masks, live `DwmRegisterThumbnail`, Aero Peek, and **cached Mica** (since Mica blurs the static wallpaper only once on wallpaper change). Disables per-frame behind-window Gaussian blur (Aero Glass and Acrylic automatically fall back to translucent `ColorizationColor` alpha blend with `ColorizationOpaqueBlend = 1`).
- **Tier 2 (Hardware OpenGL >= 3.0 with `GLX_EXT_texture_from_pixmap`, including older Intel Gen7 Ivy Bridge / Haswell / Bay Trail / HD 4000+ iGPUs):** Enables `compose_gl.c` with full 60 Hz dual-Kawase behind-window blur for Vista/7 Aero Glass and Win10/11 Acrylic, plus Flip3D.

---

## 5. Phased Roadmap (D5 Onward)

Each phase is scoped to a single session, ordered by strict dependency.
- **Agent roles:**
  - **Antigravity:** Architecture, cross-process protocol design, complex Wine driver/compositor integration, review, and running conformance/VM tests.
  - **Claude:** Multi-file C implementation across `wine/` and `src/` where shell/test execution is helpful.
  - **Qwen (local model):** Self-contained C/Python coding tasks from exact struct/function specifications (no shell required).

### Phase D5: `dwmapi` State Completion & X11 Property Bridge (`winex11.drv` -> `compose.c`)
- **Scope:**
  1. In `wine/dlls/dwmapi/dwmapi_main.c`, store `MARGINS` in `DwmExtendFrameIntoClientArea` (`WexDwmMarginsL/R/T/B`) and `DWM_BLURBEHIND` in `DwmEnableBlurBehindWindow` (`WexDwmBlurFlags`, `WexDwmBlurRgn`), make `DwmIsCompositionEnabled` read `HKCU\Software\Microsoft\Windows\DWM\Composition` (defaulting to `FALSE` when key is absent or `0`, while keeping a test-override env var `WEX_FORCE_DWM_ENABLED=1` for headless tests), and broadcast `WM_DWMCOMPOSITIONCHANGED` when `wexshell` toggles composition.
  2. Add `NtUserSetWindowCompositionAttribute` / `SetWindowCompositionAttribute` (`WCA_ACCENT_POLICY = 19`) in `wine/dlls/user32` / `dwmapi` mapping `ACCENT_ENABLE_BLURBEHIND` (3) and `ACCENT_ENABLE_ACRYLICBLURBEHIND` (4) to `WexDwmAttr38` (`DWMSBT_TRANSIENTWINDOW`).
  3. In `wine/dlls/winex11.drv/window.c`, add `sync_window_dwm_state(Display *display, struct x11drv_win_data *data)` next to `sync_window_shadow` (line 2454) to publish `_WEX_DWM_STATE` and `_WEX_DWM_BLUR` on `data->whole_window`, and upgrade `data->whole_window` to `argb_visual` when `MARGINS != 0` or `backdrop_type >= DWMSBT_MAINWINDOW`.
  4. In `src/dlls/wexhost/compose.c`, parse `_WEX_DWM_STATE` and `_WEX_DWM_BLUR` in `compose_init_atoms`, `add_window`, and `PropertyNotify`, and skip painting windows with `WEX_DWM_CLOAKED`.
- **Files Touched:** `wine/dlls/dwmapi/dwmapi_main.c`, `wine/dlls/dwmapi/tests/dwmapi.c`, `wine/dlls/winex11.drv/window.c`, `src/dlls/wexhost/compose.c`, `src/dlls/wexshell/desktop.c`, `docs/STOCK_CHANGES.md`.
- **Acceptance Tests:**
  - Run `x86_64-w64-mingw32-gcc` build of `wine/dlls/dwmapi/tests/dwmapi.c` on both Windows 11 host and Wine (`WINEDEBUG=-all wine dwmapi_test.exe`).
  - Run `compose-probe` on the VM verifying `DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &one, 4)` hides the window from `compose.c`'s overlay capture (`WexCompositionCaptureDesktop`) and uncloaking restores it.
- **Assigned To:** **Claude** (implementation) + **Antigravity** (review & host/VM test verification).

### Phase D6: DWM Non-Client Frame, Top-Level Shadows, Rounded Corners & Custom Caption/Border Colors
- **Scope:**
  1. Implement `IsCompositionActive()` in `wine/dlls/uxtheme/system.c:577`.
  2. Update `wine/dlls/uxtheme/window.c:UXTHEME_NonClientFrameDraw` and `nc_update_region`: when `IsCompositionActive()` is true and `DWMNCRP_DISABLED` is not set, skip binary `SetWindowRgn`, honor `DWMWA_USE_IMMERSIVE_DARK_MODE` (`WexDwmAttr20`), `DWMWA_CAPTION_COLOR` (`WexDwmAttr35`), and `DWMWA_TEXT_COLOR` (`WexDwmAttr36`), and clear translucent caption/margin areas to `0x00000000` instead of `COLOR_BTNFACE`.
  3. In `wine/dlls/winex11.drv/window.c:sync_window_shadow`, enable `_WEX_DROPSHADOW` on decorated top-level windows when composition is active.
  4. In `src/dlls/wexhost/compose.c`, add active/inactive shadow tiles, 8-bit `PictStandardA8` rounded corner clipping (`8px` for `DWMWCP_DEFAULT`/`DWMWCP_ROUND`, `4px` for `DWMWCP_ROUNDSMALL`, `0px` for `DWMWCP_DONOTROUND`), and `1px` border rendering using `DWMWA_BORDER_COLOR`.
- **Files Touched:** `wine/dlls/uxtheme/system.c`, `wine/dlls/uxtheme/window.c`, `wine/dlls/winex11.drv/window.c`, `src/dlls/wexhost/compose.c`, `docs/STOCK_CHANGES.md`.
- **Acceptance Tests:**
  - VM visual probe (`compose-probe`): create a window with `DWMWA_CAPTION_COLOR = 0x00202020`, `DWMWA_BORDER_COLOR = 0x0000FF00`, and `DWMWCP_ROUND`; call `WexCompositionCaptureDesktop` and verify corner pixels are blended wallpaper, border pixels are `0x00FF00`, and caption pixels are `0x202020`.
  - Verify XP mode (`Composition = 0`) still passes the existing non-client region and Luna visual checks.
- **Assigned To:** **Qwen** (writing the rounded-corner mask generator and color override helpers in `compose.c` and `uxtheme/window.c` from Antigravity's spec) + **Antigravity** (integration & VM probe).

### Phase D7: Mica, Aero Glass, and Acrylic Backdrops (XRender Baseline + GPU Tier Gating)
- **Scope:**
  1. Add GPU tier detection (`Tier 0/1/2`) and `HKCU\Software\WineExplorer\Shell` (`DwmMode`, `EnableBlur`) + `HKCU\Software\Microsoft\Windows\DWM` (`ColorizationColor`) loading in `src/dlls/wexhost/compose.c`.
  2. Implement cached wallpaper Mica blur (`mica_light_picture`, `mica_dark_picture`) for `DWMSBT_MAINWINDOW` (2), `DWMSBT_TABBEDWINDOW` (4), and `DWMWA_MICA_EFFECT` (1029).
  3. Implement downscaled XRender box/convolution blur + `ColorizationColor` tint + Aero specular reflection strip for `DwmExtendFrameIntoClientArea` (`MARGINS`), `DwmEnableBlurBehindWindow`, and `DWMSBT_TRANSIENTWINDOW` (3) (with automatic opaque-tint fallback on Tier 1 when `EnableBlur == 0`).
  4. Connect `src/dlls/wexshell/startskin.c` (`Main_opacity=glass/fullglass`) and `src/dlls/wexshell/desktop.c` (taskbar) to `DwmEnableBlurBehindWindow` when `DwmMode == 2` (Aero).
- **Files Touched:** `src/dlls/wexhost/compose.c`, `src/dlls/wexshell/startskin.c`, `src/dlls/wexshell/desktop.c`.
- **Acceptance Tests:**
  - VM `compose-probe`: place a high-contrast red/white checkerboard window behind a test window calling `DwmExtendFrameIntoClientArea(hwnd, {-1,-1,-1,-1})`; capture desktop via `WexCompositionCaptureDesktop` and verify interior pixels have intermediate blurred RGB values rather than raw checkerboard or solid black.
- **Assigned To:** **Claude** (implementation) + **Antigravity** (review & verification).

### Phase D8: Live `DwmRegisterThumbnail` & Iconic Bitmaps
- **Scope:**
  1. In `src/dlls/wexhost/compose.c`, retain `w->last_picture` when a mapped window is minimized (`UnmapNotify` without `DestroyNotify`).
  2. In `wine/dlls/dwmapi/dwmapi_main.c` and `wine/dlls/winex11.drv/window.c`, serialize active `thumbnail_table` entries onto `dest`'s `_WEX_DWM_THUMBNAILS` X11 property, and implement `DwmSetIconicThumbnail` / `DwmSetIconicLivePreviewBitmap` / `DwmInvalidateIconicBitmaps` (`WM_DWMSENDICONICTHUMBNAIL` / `WM_DWMSENDICONICLIVEPREVIEWBITMAP` + `_WEX_DWM_ICONIC_PIXMAP`).
  3. In `src/dlls/wexhost/compose.c:compose_paint`, render each `_WEX_DWM_THUMBNAILS` entry with `XTransform` scaling and `FilterBilinear` onto `back_picture`.
- **Files Touched:** `wine/dlls/dwmapi/dwmapi_main.c`, `wine/dlls/dwmapi/tests/dwmapi.c`, `wine/dlls/winex11.drv/window.c`, `src/dlls/wexhost/compose.c`, `docs/STOCK_CHANGES.md`.
- **Acceptance Tests:**
  - `dwmapi_test.exe` conformance test for thumbnail registration, `DwmQueryThumbnailSourceSize`, and `WM_DWMSENDICONICTHUMBNAIL` delivery on Windows 11 host and Wine.
  - VM `compose-probe`: register a thumbnail of a solid-green source window onto a solid-blue destination window, capture the desktop, and assert the destination sub-rect contains green pixels even after minimizing the source window.
- **Assigned To:** **Claude** (implementation) + **Antigravity** (test authoring & review).

### Phase D9: Taskbar Previews, Alt+Tab Thumbnails, and Aero Peek in `wexshell`
- **Scope:**
  1. Add `WineExplorerTaskPreview` hover/group preview popup in `src/dlls/wexshell/taskband.c` using `DwmRegisterThumbnail`.
  2. Add DWM thumbnail switcher mode (`AltTabStyle == 1`) in `src/dlls/wexshell/alttab.c`.
  3. Add the far-right "Show desktop" Aero Peek button in `src/dlls/wexshell/desktop.c`, hover-peek in `taskband.c`, and `_WEX_DWM_PEEK_TARGET` glass-outline rendering in `src/dlls/wexhost/compose.c`.
  4. Add DWM controls (Composition toggle, DwmMode era selector, Taskbar thumbnails checkbox) in `src/dlls/wexshell/taskbar_props.c`.
- **Files Touched:** `src/dlls/wexshell/taskband.c`, `src/dlls/wexshell/alttab.c`, `src/dlls/wexshell/desktop.c`, `src/dlls/wexshell/taskbar_props.c`, `src/dlls/wexhost/compose.c`.
- **Acceptance Tests:**
  - VM interactive/automated test: open two windows, trigger Alt+Tab (`VK_MENU + VK_TAB`) and taskbar hover, capture desktop, and verify non-zero scaled thumbnail regions and Peek outline behavior.
- **Assigned To:** **Qwen** (`alttab.c` and `taskbar_props.c` UI changes from exact spec) + **Claude** (`taskband.c` preview popup & Peek wiring) + **Antigravity** (review).

### Phase D10: DirectComposition Underlay & Transparent Swapchain Integration
- **Scope:**
  1. In `wine/dlls/dcomp/device.c` and `wine/dlls/winex11.drv/init.c` (`client_surface_update_offscreen`, `X11DRV_client_surface_present`), ensure that when an `HWND` has `WINE_DCOMP_UNDERLAY_PROP` (`__wine_dcomp_underlay`) and an active DWM backdrop (`MARGINS` or `DWMWA_SYSTEMBACKDROP_TYPE`), `whole_window` uses `argb_visual` and `dcomp`'s layer composition preserves premultiplied alpha (`0x00000000` background clear instead of opaque black) when presenting into `whole_window`.
  2. Honor `IDCompositionRectangleClip` rounded corner radii (`radius_tl_x`..`radius_br_y` in `struct wine_dcomp_layer`) during DComp target blitting.
- **Files Touched:** `wine/dlls/dcomp/device.c`, `wine/dlls/winex11.drv/init.c`, `wine/dlls/winex11.drv/window.c`, `docs/STOCK_CHANGES.md`.
- **Acceptance Tests:**
  - Extend `dcomp` test / probe (`dcomp-underlay-probe.exe`): create an `HWND` with `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW`, attach an `IDCompositionTarget` with a half-transparent visual, and verify via `WexCompositionCaptureDesktop` that the Mica backdrop is visible through the transparent half of the DComp visual.
- **Assigned To:** **Antigravity** (design & implementation) + **Claude** (test harness).

### Phase D11: OpenGL Compositor Backend (`compose_gl.c`) & Optional Flip3D
- **Scope:**
  1. Add `src/dlls/wexhost/compose_gl.c` using `GLX_EXT_texture_from_pixmap`, GLSL 1.30 dual-Kawase blur (2 downsamples + 2 upsamples) for high-FPS Vista/7 Aero Glass and Win10/11 Acrylic on Tier 2 GPUs, and optional `Win+Tab` Flip3D stack rendering.
  2. Keep seamless fallback to `compose.c`'s XRender path when `CompositionBackend == 1` or GLX TFP is unavailable.
- **Files Touched:** `src/dlls/wexhost/compose_gl.c`, `src/dlls/wexhost/compose.c`, `src/dlls/wexhost/wexhost_unix.h`, `src/dlls/wexhost/Makefile.in`, `src/dlls/wexshell/desktop.c`.
- **Acceptance Tests:**
  - Run `compose-probe` on VM with both `CompositionBackend = 1` (XRender) and `CompositionBackend = 2` (OpenGL) and verify identical window stacking, thumbnails, and non-black blurred regions.
- **Assigned To:** **Claude** (GLX + shader implementation) + **Antigravity** (review & fallback testing).

### Phase D12: Open-Art Visual Style Packager & Theme Suite (`Aero`, `Aero Basic`, `Flat10`, `Fluent11`) + Wayland `wexcomp` Parity
- **Scope:**
  1. Write `tools/mkmsstyles.py` and author clean-room SVG/PNG assets + metric tables to build `aerobasic.msstyles`, `aero.msstyles`, `flat10.msstyles`, and `fluent11.msstyles` (+ `.theme` files) under `assets/themes/` using only open art and host-measured numbers from Section 6.
  2. Extend `src/wexcomp/protocols/wex-window-manager-unstable-v1.xml` (v12), `src/dlls/wexhost/waylandwm.c`, and `src/wexcomp/src/wex_wm.c` to forward `_WEX_DWM_STATE` (backdrop, corner radius, border color, cloak) to the Wayland scene graph.
  3. Package themes in the installer and run full verification.
- **Files Touched:** `tools/mkmsstyles.py`, `assets/themes/*`, `src/wexcomp/protocols/wex-window-manager-unstable-v1.xml`, `src/wexcomp/src/wex_wm.c`, `src/dlls/wexhost/waylandwm.c`.
- **Acceptance Tests:**
  - Load each of the 4 new `.msstyles` with `uxtheme` v4 loader tests, switch themes in Display Properties on the VM, and run the **full installer test** (`scripts/test-installer.sh`) to verify clean installation and zero regressions in default XP mode.
- **Assigned To:** **Qwen** (`tools/mkmsstyles.py` and theme INI definitions from measured numbers) + **Claude** (`wexcomp` protocol extension & installer packaging) + **Antigravity** (final review & installer test).

---

## 6. Host-Windows Measurement Tasks (Authorised Clean-Room Probes)

To achieve pixel-accurate Vista/7/8/10/11 geometry and timing without ever copying Microsoft theme files or binaries, we will write standalone Win32 C probe programs under `scripts/` (following the pattern of `scripts/dwmapi-probe.c`), compile them with `x86_64-w64-mingw32-gcc`, run them on the Windows 11 host, and record **only numeric tables** in `docs/DWM_MEASUREMENTS.md`:

1. **`scripts/dwm-frame-metrics-probe.c` — Frame, Extended Border, and Caption Geometry per DPI:**
   - Queries `GetSystemMetricsForDpi` (`SM_CXFRAME`, `SM_CYFRAME`, `SM_CXPADDEDBORDER`, `SM_CYCAPTION`, `SM_CYSMCAPTION`, `SM_CXSIZE`, `SM_CYSIZE`), `GetWindowRect`, `GetClientRect`, `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`, and `DwmGetWindowAttribute(DWMWA_CAPTION_BUTTON_BOUNDS)` at 96, 120, 144, and 192 DPI across `WS_OVERLAPPEDWINDOW`, `WS_POPUPWINDOW`, `WS_EX_TOOLWINDOW`, and maximized states.
   - Outputs exact pixel deltas between `GetWindowRect` (which includes the invisible resize border on Win10/11) and `DWMWA_EXTENDED_FRAME_BOUNDS` (the visible composited rect).

2. **`scripts/dwm-corner-shadow-probe.c` — Corner Radii, Border Width, and Drop-Shadow Falloff:**
   - Creates a borderless white backdrop window and a black test window on top of it, cycles `DWMWA_WINDOW_CORNER_PREFERENCE` (`DWMWCP_DEFAULT`, `DWMWCP_DONOTROUND`, `DWMWCP_ROUND`, `DWMWCP_ROUNDSMALL`) and active/inactive states (`WM_NCACTIVATE`), captures the screen region via `BitBlt(SRCCOPY)` from `GetDC(NULL)`, and fits:
     - Effective corner radius in pixels and 8-bit alpha coverage along the corner arc.
     - Active vs inactive shadow extent (top, bottom, left, right in pixels), vertical offset, peak alpha, and Gaussian sigma.

3. **`scripts/dwm-backdrop-color-probe.c` — Mica, Acrylic, and Colorization Transfer Function:**
   - Places a calibrated RGB/checkerboard background window at known coordinates, opens a foreground test window with `DWMWA_SYSTEMBACKDROP_TYPE` (`DWMSBT_MAINWINDOW`, `DWMSBT_TRANSIENTWINDOW`, `DWMSBT_TABBEDWINDOW`) across light and dark mode (`DWMWA_USE_IMMERSIVE_DARK_MODE = 0 / 1`), samples the composited screen pixels via `GetDC(NULL)`, and computes the effective blur spatial frequency cutoff (blur radius) and base-tint / accent-color blend weights.

4. **`scripts/dwm-timing-iconic-probe.c` — Animation Timings, Thumbnail Clamp, and Iconic Message Sequence:**
   - Calls `DwmGetCompositionTimingInfo` (`DWM_TIMING_INFO`) to record tick rate and frame pipeline counts.
   - Enables `DWMWA_FORCE_ICONIC_REPRESENTATION = 1` and `DWMWA_HAS_ICONIC_BITMAP = 1`, logs the exact `WM_DWMSENDICONICTHUMBNAIL` `HIWORD(lParam)` / `LOWORD(lParam)` requested dimensions and message order when hovering the taskbar, and measures `DwmQueryThumbnailSourceSize` on minimized vs cloaked vs normal windows.

---

## 7. Risks and Open Questions for the Owner

### Technical Risks & Mitigations
1. **24-bit vs 32-bit X11 Visual Switching on Live HWNDs:**
   - *Risk:* X11 does not allow changing an existing `Window`'s `Visual` in place (`XChangeWindowAttributes` cannot switch depth 24 to depth 32). Many Win32 apps create their `HWND` first and call `DwmExtendFrameIntoClientArea` or `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)` during `WM_CREATE` or after `ShowWindow`.
   - *Mitigation:* In `wine/dlls/winex11.drv/window.c:create_whole_window`, when composition is active (`IsCompositionActive()`), create all top-level windows using the 32-bit `argb_visual` from the start (filling the background alpha to `0xFF` when opaque), or call `set_window_visual(data, &argb_visual, TRUE)` (which already recreates `whole_window` cleanly in `winex11.drv`) when `sync_window_dwm_state` first sees extended margins or a translucent backdrop.
2. **XRender Full-Screen Blur Performance on Software X11 (`llvmpipe` / VM):**
   - *Risk:* Running multi-pass behind-window blur at 1080p in software XRender can drop below 20 FPS during window dragging.
   - *Mitigation:* Cached Mica (`Phase D7`) blurs the wallpaper once at `1/8` resolution (`~240x135`) on wallpaper load (`0 ms` per frame), and behind-window Aero/Acrylic blur downscales the dirty rect `4x` before blurring and automatically degrades to a fast alpha tint on Tier 1 software renderers when moving windows (`WM_ENTERSIZEMOVE`..`WM_EXITSIZEMOVE`).
3. **Invisible Resize Borders on Windows 10/11 Flat & Fluent Themes:**
   - *Risk:* Real Windows 10/11 themes use a `1px` visible border plus a `7px` invisible resize gutter outside `DWMWA_EXTENDED_FRAME_BOUNDS`. If `GetWindowRect` and `DWMWA_EXTENDED_FRAME_BOUNDS` differ, apps that position windows edge-to-edge leave `14px` gaps unless they query `DWMWA_EXTENDED_FRAME_BOUNDS`.
   - *Mitigation:* In `flat10.msstyles` and `fluent11.msstyles`, we can either keep the resize border inside the visible frame (with `x11wm.c` providing an outer 6px invisible X11 input-only grab border) or replicate the exact 7px `DWMWA_EXTENDED_FRAME_BOUNDS` inset measured in Section 6.1.

### Open Questions for the Owner
1. **Default behavior of `DwmIsCompositionEnabled()` when `Composition == 0` (XP Mode):**
   - Currently on `dwmapi-d4` (`wine/dlls/dwmapi/dwmapi_main.c:262`), `DwmIsCompositionEnabled` always writes `*enabled = TRUE` so modern apps (and `dwmapi-probe.exe`) exercise the DWM code paths even when the shell is in XP mode. Once `D5` lands, should `DwmIsCompositionEnabled` strictly return `FALSE` when `Composition == 0` (true XP/Vista-Basic behavior), or should there be a separate `ReportDwmEnabled` registry override for apps that refuse to start when `DwmIsCompositionEnabled` returns `FALSE`?
2. **Windows 10/11 Invisible Resize Border Strategy:**
   - For the `flat10` and `fluent11` themes, do you prefer **(A)** `GetWindowRect == DWMWA_EXTENDED_FRAME_BOUNDS` with `x11wm.c` handling outer resize grabs (simpler window snapping, no 7px `GetWindowRect` offset), or **(B)** exact Windows 10/11 `7px` transparent margin inside `GetWindowRect` so `GetWindowRect` vs `DWMWA_EXTENDED_FRAME_BOUNDS` matches host Windows 11 pixel-for-pixel?
3. **Flip3D Priority (`Phase D11`):**
   - Should `Win+Tab` in `wexshell` open **Vista/7 Flip3D** when `DwmMode == 2` and a **2D Task View thumbnail grid** when `DwmMode >= 3` (Win10/11), or is a single 2D thumbnail overview sufficient for both?
