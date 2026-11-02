<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# dwmapi behaviour: Windows 11 against Wine 11.21 (measured 2026-11-02)

`scripts/dwmapi-probe.c` calls the public `dwmapi` functions on a test window and prints the HRESULTs and values.
It was run on Windows 11 build 26300 (composition on) and on this project's Wine 11.21 build (WoW64, headless). Only
return values and numbers were recorded; nothing from Windows files is kept here. This is the measured starting point
for D4 (`WINLOGON_AND_DWM.md`). Build: `x86_64-w64-mingw32-gcc -Wall -O2 scripts/dwmapi-probe.c -o dwmapi-probe.exe
-lgdi32 -luser32`.

| Function / attribute | Windows 11 | Wine 11.21 today | Needed for D4 |
| --- | --- | --- | --- |
| `DwmIsCompositionEnabled` | S_OK, TRUE | S_OK (follows our setting) | keep; false in XP mode |
| `DwmGetColorizationColor` | S_OK, ARGB colour, opaque flag | E_NOTIMPL | return the theme's accent colour |
| `DwmGetWindowAttribute` `NCRENDERING_ENABLED` | S_OK, 1 | E_NOTIMPL | follow composition |
| `...EXTENDED_FRAME_BOUNDS` | S_OK; frame without the invisible resize border (x +7 here) | S_OK, plain window rect | subtract the invisible border when the theme has one |
| `...CLOAKED` | S_OK, 0 | E_NOTIMPL | 0, or the real cloak state of the window |
| `...WINDOW_CORNER_PREFERENCE` | S_OK, 0 | E_NOTIMPL | stored per window |
| `...CAPTION_BUTTON_BOUNDS` | S_OK, real rectangle | E_NOTIMPL, zeros | from the theme's caption buttons |
| `DwmSetWindowAttribute` (policy, NCPAINT, dark mode, backdrop, corner) | S_OK; dark mode, backdrop and corner read back; policy and NCPAINT read back E_INVALIDARG | S_OK, but nothing is stored (read back E_NOTIMPL) | store the values; reading the two write-only ones is E_INVALIDARG |
| `DwmExtendFrameIntoClientArea` (0 and -1) | S_OK | S_OK | keep |
| `DwmEnableBlurBehindWindow` | S_OK | E_NOTIMPL | accept; apply only with composition |
| `DwmGetCompositionTimingInfo` | S_OK, 292-byte struct, refresh 10000000/166666 (60 Hz), counters | S_OK, rate as 60/1 | match the rate encoding (QPC units) |
| `DwmFlush` | S_OK, 9 ms | S_OK, 15 ms | keep |
| `DwmEnableComposition` | S_OK (disable and enable, composition stays on) | S_OK | keep |
| `DwmRegisterThumbnail` / `Update` / `Unregister` | S_OK; handle is a small integer | E_NOTIMPL | D3 thumbnails |
| `DwmQueryThumbnailSourceSize` | S_OK, size of the source window | **export missing**: the process aborts at the call | add the export |
| `DwmSetIconicThumbnail` / `LivePreviewBitmap` | E_INVALIDARG without iconic mode | S_OK | validate the mode first |
| `DwmInvalidateIconicBitmaps` | S_OK | E_NOTIMPL | accept |
| `DwmModifyPreviousDxFrameDuration` | E_NOTIMPL on this build | **export missing** | add the export, return E_NOTIMPL |
| `DwmGetGraphicsStreamTransformHint` | 0x80263001 (no stream) | E_NOTIMPL | return the same code |
| `DwmTransitionOwnedWindow` | E_INVALIDARG | ERROR_CALL_NOT_IMPLEMENTED (0x8007007f) | return E_INVALIDARG |
| `DwmShowContact` | S_OK | not run (the process had already aborted) | accept |

Findings:
- Wine's `dwmapi` lacks two exports (`DwmQueryThumbnailSourceSize`, `DwmModifyPreviousDxFrameDuration`), which kill a
  program that calls them. These are small and safe to add first.
- Several functions return `E_NOTIMPL` where Windows returns success and stores state (`Set/GetWindowAttribute`,
  blur-behind, thumbnails). Programs that check the HRESULT take their "no DWM" path, which is correct for the XP
  parity default, but Vista-era programs and Chromium's DirectComposition path want the success path when composition is
  on. That is what D4 and the application-facing work in `CHROMIUM_GRAPHICS_PLAN.md` add.
- The probe's raw outputs are in `ChromiumLab/dwmapi-host.txt` and `dwmapi-wine.txt` (local lab files, not in the repository).
