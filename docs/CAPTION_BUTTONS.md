<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# Caption button reference measurements (2026-11-03)

Measurements only; no art or glyph data is copied. Sources: the Windows 11 development host at 96 dpi (a WinForms window,
screen capture of its title bar, `DwmGetWindowAttribute`), genuine Luna resources kept in the local-only repository, and
a third-party Windows 7 style used as test data. Claims about Windows 10 and the Windows 7 glyphs could not be measured
here and are marked so.

## Verdicts on the hints from Gemini

| Claim | Verdict | Measured |
|---|---|---|
| XP restore glyph: two overlapping 7x7 boxes, thick 2 px top bars, solid white | **Correct** | Luna glyph cell 13x13: front box x1-7, y4-10; back box x4-10, y1-7, each 7x7 with a 2 px top bar; overall 10x10; pure #FFFFFF. No dark shadow in the glyph (the buttons are blue gradients). |
| Win10/11 restore glyph 10x10 footprint | **Correct for Windows 11** | 10x10 (host). Min glyph 10x1, max glyph 10x10. Windows 10 not measurable here. |
| Win11 caption buttons 46 px wide | **Correct** | Glyph centres 391, 436, 482 px: a 45-46 px pitch. |
| Title bar height 32 px | **Not for the system frame** | 30 px (hover plate rows 0-29) at 100 %. Microsoft's design page gives 32 px for an app's own title bar. |
| Win11 inactive light bar flat #f9f9f9 | **Wrong** | Inactive #F3F3F3. The active bar is Mica-tinted (#F9F1EC over this wallpaper), not flat. |
| Win11 inactive dark bar #202020 | **Correct** | #202020. The active dark bar is Mica-tinted (#241F1B). A later "dark inactive #1c1c1c" in the CSS is wrong. |
| Win11 inactive glyph alpha 0.45 | **Roughly** | Darkest glyph pixel #9B9B9B (close), #929292 (min) on #F3F3F3: alpha about 0.36-0.40. |
| Win11 close hover #C42B1C | **Correct** | #C42B1C, light and dark. |
| Win11 dark hover rgba(255,255,255,0.06) | **Roughly** | #2F2D2A over #241F1B is about 0.05. Light hover is about 0.045 black (#EEE9E3 over #F9F1EC). |
| Win10 close hover #E81123, dark bar #1f1f1f, inactive alpha 0.4 | **Unverified** | No Windows 10 host. #E81123 is the commonly cited value. |
| Vista/7: 7x6 boxes with outline; inactive close loses its red | **Unverified** | A third-party Windows 7 style (test data) has min/max buttons 28x20 and close 46x20 per state, glyph cells 13x14 (close) and 16x14 (max) including outline and shadow. Not genuine Windows art. |

The CSS in the hints is for a web mock-up; WineExplorer draws these in C from the style's parts (`uxtheme/window.c`).
Use the measured numbers above (inactive #F3F3F3 / #202020, hover #C42B1C) for the Windows 11 frame; the rest stays
as estimated until a Windows 10 or 7 reference is available.
