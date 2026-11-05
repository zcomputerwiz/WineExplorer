<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# Windows 8 frame style design

Status: read-only design; no Win8 host is available and no code is changed here.
The item is a third `DwmFrameStyle` value, `2`: opaque accent frame. Taskbar and
Start-screen work are out of scope. Existing frame facts are in
`docs/DWM_ARCHITECTURE.md` P13 and `docs/CAPTION_BUTTONS.md`.

## 1. Measurements and evidence boundary

Measure on a real Win8 and Win8.1 host, at 96/144/192 dpi, with active and
inactive, light/dark, maximized/restored, thick/dialog/tool and RTL windows:

* `DwmGetColorizationColor` value/alpha and the presented RGB of frame, title
  bar, 1-pixel border, inactive frame, and disabled/hover/pressed glyphs;
* outer frame, client rect, caption height, border widths, button rectangles,
  button pitch, glyph footprint and glyph-to-button alignment;
* square-corner and shadow presence, including maximized and DPI transitions;
* hit-test pixels/regions for resize gutters and close/maximize/minimize, plus
  whether inactive buttons retain the same geometry.

Capture the whole desktop and a window-DC sample, then record coordinates,
scale, wallpaper, theme, active state and exact RGB values. Use a small probe
to print `DwmGetColorizationColor`, `GetWindowRect`, `GetClientRect`, DPI and
`WM_NCHITTEST`; use screen pixels to distinguish the presented frame from the
window DC. Repeat after changing the accent and activation state.

What the Windows 11 host can establish: current project host measurements in
`CAPTION_BUTTONS.md` (30 px system bar, 45–46 px pitch, measured inactive
colours and close hover) and the probe/pixel methodology. It cannot establish
Win8/8.1 colours, metrics, glyphs, inactive treatment, square corners or
shadow. Existing Win7 numbers are explicitly third-party/unverified.
Scaling from a measured 96-dpi value is an implementation hypothesis, not a
Win8 fact. Keep every Win8/8.1 number **Unverified** until a matching host is
measured. Add a row to `docs/CAPTION_BUTTONS.md` labelled “Windows 8/8.1
opaque accent frame” with separate Win8 and Win8.1 columns (or separate rows),
source/build/DPI, measured values, and an **Unverified** verdict for each
missing value; do not promote Win11 or Win7 estimates.

## 2. Minimal implementation plan

1. **dwmapi — Qwen shell+harness, medium/ABI risk.** Extend the existing
   `dwm_aero_frames`, `set_glass`, `WexDwmpRefreshFrame` path in
   `wine/dlls/dwmapi/dwmapi_main.c` to decode 0/1/2 and publish
   `DWM_ATTR_WINE_FRAME` (currently 0/1/2 means none/modern/Aero in
   `wine/include/wine/dwm_protocol.h` and `src/dlls/wexshell/dwm_bridge.c`).
   Style 2 must clear automatic blur and remain opaque; preserve default 0,
   style 1, margins, refresh and native/WoW64 table layouts.
2. **uxtheme — Codex static, medium risk.** Extend
   `get_dwm_aero_frames` in `wine/dlls/uxtheme/window.c` to an enum, select a
   new flat8 paint branch beside `paint_aero_nc` and the modern path, and use
   `DwmGetColorizationColor`/stored colorization for opaque frame and title
   colours. Centralize measured border/caption/button metrics and update
   `UXTHEME_NonClientFrameDraw` refresh logic. Do not reuse Aero blur or
   rounded-corner assumptions.
3. **win32u — Codex static, medium hit-test risk.** In
   `wine/dlls/win32u/defwnd.c`, make the frame-kind test used by hit testing
   agree with the flat8 rectangles. Reuse `__wine_nc_buttons` and its existing
   packing limits in `wine/include/winuser.h` (close width is 8 bits, 0–255;
   other width 7 bits; right 6; top 5; height 6). If measured close width
   exceeds 255 at high DPI, change the shared contract deliberately and test
   both architectures; do not silently truncate.
4. **X11 mapping — Qwen shell+harness, low/medium risk.** Audit
   `wine/dlls/winex11` for the frame property/flag consumed by the X11 path.
   A direct `DwmFrameStyle`/`DWM_ATTR_WINE_FRAME` mapping was not found in the
   inspected tree; current evidence routes the value through `dwm_bridge.c`.
   Add only the missing mapping after confirming the actual X11 consumer.
5. **Compositor — Antigravity compositor-only, medium visual risk.** The
   bridge currently maps frame kind to default radius/shadow in
   `src/dlls/wexshell/dwm_bridge.c` (`frame ? 6/8 radius` and shadow on).
   Add explicit style-2 handling: radius 0, no shadow, no blur/material, and
   preserve a 1-pixel opaque border. No direct style-2 branch was found in
   `wexcomp`; verify the bridge-to-scene consumer before editing it. Keep
   Wayland protocol changes compositor-only and equivalent to X11.
6. **Theme/package — Qwen shell+harness, medium packaging risk.** Extend the
   verified `src/dlls/themeui/theme_engine.c:apply_dwm_settings` mapping with
   `FrameStyle=flat8` (and numeric `2`) while missing/unknown remains 0.
   Preserve `[VisualStyles] ColorizationColor`. The open Aero package exists at
   `src/resources/themes/aero-open`/`AeroOpen.theme`; a flat8 style/package is
   not found and must use original/free assets only. Missing parts fall back
   to the flat8 procedural frame, not Aero or proprietary material.

## 3. Test and acceptance plan

* Gates off first: absent/0 `DwmFrameStyle`, `DwmFrameEffects=0`,
  `DwmAlpha=0`, `DwmMaterials=0`, and `Composition=0` preserve existing
  modern/XP behavior. Test registry `HKCU\Software\WineExplorer\Shell` and
  INI `FrameStyle` independently; unknown values fall back to 0.
* X11: use `scripts/theme-gallery-probe.c` and the existing frame probes with
  styles 0/1/2, live switch, active/inactive, DPI, maximize, resize, RTL and
  missing-theme parts. Save whole-screen screenshots and compare border,
  caption, button and corner pixels; exercise close and all resize hit tests.
* Wayland: use `scripts/check-wayland-dwm.sh` with `DwmFrameStyle=2` and the
  same gates; verify square scene geometry, no shadow/blur, opaque accent
  pixels, focus changes and input. Do not claim display-presentation parity.
* Acceptance requires fresh-prefix X11 and Wayland screenshots plus presented
  screen-pixel checks. Compile/static evidence alone is not acceptance.

## 4. Slice ownership, effort and risks

| Slice | Owner | Effort | Main risk |
|---|---|---:|---|
| Host probe and CAPTION_BUTTONS evidence | Qwen shell+harness | 1–2 d | no Win8 host; measurements stay unverified |
| Dwmapi enum/refresh and gates | Qwen shell+harness | 1–2 d | stale frame/blur state or ABI drift |
| uxtheme paint/metrics | Codex static | 2–3 d | colour/active-state and DPI mismatch |
| defwnd packing/hit tests | Codex static | 1–2 d | truncated geometry or resize regressions |
| X11/property audit and tests | Qwen shell+harness | 1–2 d | consumer is elsewhere than expected |
| Wayland square/no-shadow mapping | Antigravity compositor-only | 1–2 d | scene/input edge mismatch |
| flat8 theme/package and integration review | Qwen + Codex | 1–2 d | missing open assets/licensing |

