<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DWM measurements on Windows 11 (numbers only)

Recorded 2026-11-02 on Windows 11 build 26300, composition on, one 1920x1080 monitor at 96 DPI, by the clean-room probes in
`scripts/dwm-*-probe.c` (`scripts/dwm-probes-README.md`). Only numbers are kept here; nothing from Microsoft files is copied.
Raw outputs are local lab files (`ChromiumLab/dwm-host-out/`). These numbers are inputs for D6 and D12 in `DWM_PLAN.md`, not
yet verified against a second machine.

## Frame geometry at 96 DPI (`dwm-frame-metrics-probe`)

`WS_OVERLAPPEDWINDOW` created at (80,80)-(480,380):

| Quantity | Value |
| --- | --- |
| `GetWindowRect` | 80,80 - 480,380 |
| Client rect (screen) | 88,111 - 472,372 (borders: left 8, right 8, bottom 8, top 31 = caption 23 + frame 8) |
| `DWMWA_EXTENDED_FRAME_BOUNDS` | 87,80 - 473,373: the visible frame is inset by 7 on the left, right and bottom and 0 on top |
| `DWMWA_CAPTION_BUTTON_BOUNDS` (window coordinates) | left 247, top 0, right 393, bottom 30 (three buttons, 146 wide total; the right edge is 7 short of the window width: the invisible border) |
| System metrics | `SM_CXSIZEFRAME` 4, `SM_CXPADDEDBORDER` 4, `SM_CYCAPTION` 23, `SM_CYSMCAPTION` 23, `SM_CXSIZE` 36, `SM_CYSIZE` 22, `SM_CXMINTRACK` 136 |

Other window kinds (same position): `WS_POPUPWINDOW` and `WS_POPUP|WS_BORDER`: extended bounds equal the window rect (no invisible
border) and the caption button bounds are empty. `WS_EX_TOOLWINDOW` overlapped: same frame as the normal window, caption
button bounds 344 to 393 (one button, 49 wide). Maximized overlapped window: window rect -8,-8 - 1928,1040, client 0,23 -
1920,1032, extended bounds 0,0 - 1920,1032 (work area), caption buttons 1781 to 1927, height 30.

System metrics at other DPI values (from `GetSystemMetricsForDpi`; per-window DPI could not be reached, so these are metrics only):

| DPI | `SM_CXSIZEFRAME` | `SM_CXPADDEDBORDER` | `SM_CYCAPTION` | `SM_CXSIZE` x `SM_CYSIZE` |
| --- | --- | --- | --- | --- |
| 96 | 4 | 4 | 23 | 36 x 22 |
| 120 | 4 | 5 | 29 | 46 x 28 |
| 144 | 5 | 6 | 34 | 54 x 33 |
| 192 | 5 | 8 | 45 | 72 x 44 |

## Corners and shadow (`dwm-corner-shadow-probe`)

Black 400x300 window over a white backdrop at 96 DPI; the visible frame is 307,300 - 693,593. The probe's definitions: corner
radius = distance along the diagonal to the first pixel below 50% coverage; shadow extent = distance to the first pixel within 2
levels of white (this includes the 7 px invisible border); offset estimate = (bottom - top) / 2.

| Corner preference | Radius by probe | First corner pixels' coverage (0-255) |
| --- | --- | --- |
| DEFAULT (0) and ROUND (2) | 3 | 92, 98, 213, 255 (active); 65, 71, 206, 255 (inactive) |
| DONOTROUND (1) | 0 | 255 |
| ROUNDSMALL (3) | 1 | 16, 248, 255 |

| State | Shadow left / right / top / bottom (px) | Darkest value (distance) |
| --- | --- | --- |
| Active, default corners | 38 / 38 / 28 / 75 | 104 (1 px) |
| Inactive, default corners | 34 / 35 / 14 / 71 | 144 (1 px) |
| Active or inactive, ROUNDSMALL | 7 / 7 / 1 / 16 | 229 (1 px) |

The probe's radius is a coverage threshold, not the nominal 8 px; a fit of the full arc is still to be done. The shadow differs
clearly between active and inactive (darker and taller when active) and is much smaller for small corners.

## Timing and thumbnails (`dwm-timing-iconic-probe`)

- `DwmGetCompositionTimingInfo`: `cbSize` 292, refresh and compose rate 10000000/166667 (60 Hz), counters advance by one per refresh.
- `DwmFlush`: 30 calls, min 6.1 ms, mean 16.3 ms, max 16.7 ms.
- `DwmQueryThumbnailSourceSize` returns the same size (306x233 for a 320x240 test window) for a normal, a minimized and a cloaked window.
- Iconic messages (`WM_DWMSENDICONICTHUMBNAIL`, `WM_DWMSENDICONICLIVEPREVIEWBITMAP`): none arrive without a real taskbar hover (0 in 3 s);
  `DwmSetIconicThumbnail` calls were accepted.

## Backdrop (`dwm-backdrop-color-probe`)

A test window with the frame extended into the whole client area (margins -1) and the client painted black, over a calibrated
window (left half RGB 255,0,0, right half RGB 0,0,255). Values are screen pixels at 9 points across the window (left, middle,
right shown); `ColorizationColor` on this machine is 0xE3BC000B with opaque blend on.

| Backdrop type | Light mode (left / middle / right) | Dark mode (left / middle / right) |
| --- | --- | --- |
| None (1) | 255,255,255 everywhere | 0,0,0 everywhere |
| Main window, Mica (2) | 249,241,236 / 249,241,236 / 249,241,237 | 35,31,28 / 36,31,27 / 37,31,27 |
| Tabbed, Mica Alt (4) | 227,216,208 / 229,215,206 / 229,214,206 | 19,8,0 / 19,7,0 / 20,7,0 |
| Transient, Acrylic (3) | 255,123,123 / 207,116,210 / 132,132,255 | 255,7,7 / 146,20,149 / 40,40,255 |

What this shows:
- Mica (2) and tabbed (4) do not depend on the windows behind (the red and blue halves give the same colour): they take the
  desktop wallpaper, strongly blurred, with a tint. Tabbed is darker than Mica in light mode and nearly black in dark mode.
  The colour is slightly warm, from the wallpaper and the accent colour.
- Acrylic (3) does follow what is behind: red behind gives 255,123,123 in light mode, which is red mixed with about 48% white,
  and blue gives 132,132,255. In dark mode the tint is much weaker (255,7,7 over red), i.e. a dark overlay of a few percent.
  The middle point (207,116,210) is the blurred red-to-blue transition, so the blur radius is at least several pixels.
- Without a backdrop the extended client area is the plain frame colour (white or black by the dark-mode setting).

Not measured: the blur radius in pixels (the checkerboard strip is in the data; a fit is still to be done), noise or luminosity
layers, and how the tint depends on `ColorizationColor` and the wallpaper.
