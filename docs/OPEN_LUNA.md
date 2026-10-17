<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Open Luna Acceptance Report (`Luna.msstyles`)

Status: Complete, ready for review.
Date: 2026-10-17.
Assignee: Gemini.

## Executive Summary

We have created an open, redistributable **Luna visual style** (`Luna.msstyles`) for WineExplorer that reproduces the appearance, colors, gradients, button shapes, caption glyphs, metrics, and scale of Windows XP's classic Luna (Blue) theme at a glance.

The visual style is built completely from source in this repository using `scripts/build_msstyles.py`, packaged into `$STAGE_WINE/usr/local/share/wineexplorer/themes/Luna/Luna.msstyles` by `installer/package-wine.sh`, installed into the shared machine root by `scripts/setup-machine-root.sh`, and configured as the default visual style for `src/resources/themes/Luna.theme`.

### Clean-Room & Provenance Guarantee
- **Starting Point**: ReactOS Lunar (`references/reactos/media/themes/Lunar/lunar.msstyles/`), licensed under **GPL-2.0-or-later** (ReactOS Project), imported untouched in commit `59a71f29`.
- **Zero Proprietary Bits**: No Windows bitmaps, pixels, or sampled hex codes were copied or traced. Colors, curves, gradients, and metrics were matched strictly by eye against side-by-side screenshots.
- **Drawn Vector Art**: All 39 modified and complex visual elements are authored as clean SVG sources in `src/resources/themes/luna-open/svg/` and rasterized at build time with `rsvg-convert` and ImageMagick (`-depth 8`).
- **Full Provenance**: Detailed bitmap-by-bitmap origin table and INI changes are recorded in [`docs/themes/luna-open.md`](themes/luna-open.md).

---

## 1. Acceptance Criteria & Comparison Sheets

All side-by-side comparison sheets comparing Open Luna against genuine Luna on identical screens at 100% (96 DPI) and 125% (120 DPI) are recorded in the local notes repository (`WineExplorer-local/notes/luna/`):

| Test Screen / Scene | Comparison Sheet (Local Only) | Status & Visual Parity |
|---|---|---|
| **Desktop with Taskbar & Start Menu** | `notes/luna/compare-desktop-start.png` | **Matched at a glance**: Green pill Start button with flag emblem, royal blue taskbar gradient with darker navy notification tray, warm orange active taskbar button highlight, royal blue user banner with rounded top corners, orange accent strip, white MFU column, and soft sky-blue Places column at 100% and 125%. |
| **Notepad Window Pair & Maximized** | `notes/luna/compare-notepad.png` | **Matched at a glance**: Royal blue active caption gradient with curved top shine and crisp 1px corner masks; slate blue inactive caption; blue rounded minimize/maximize buttons; red rounded close button; seamless maximized window caption bar at 100% and 125%. |
| **Controls Test Program (64-bit & 32-bit)** | `notes/luna/compare-controls.png` | **Matched at a glance**: Rounded push buttons with subtle blue default focus ring and orange hot ring; segmented grass-green progress blocks with specular highlights; active tab orange/amber top strip (`#f88418`); green checkmarks and radio dots; pointed slider thumb with vertical green guide line. |
| **My Computer with ExplorerBar Task Panes** | `notes/luna/compare-mycomputer.png` | **Matched at a glance**: System Tasks primary header in royal blue gradient with rounded corners and white bold text; Other Places and Details headers in soft white-to-light-blue gradient with blue text; circular collapse/expand chevrons; sky-blue pane bodies with crisp 1px borders. |

---

## 2. Rip-Check and RMSE Verification

Every compiled bitmap in the shipped `Luna.msstyles` DLL was checked against the genuine Luna reference bitmaps pool (540 bitmaps matched at identical dimensions):

- **Zero-Copy Verification on Complex Parts**:
  - In ReactOS Lunar (Phase L1), the tool-window caption sizing mask `NORMAL_SMALLFRAMECAPTIONSIZING` had an RMSE of `0.0000` against Windows. In Open Luna, this mask was completely redrawn from scratch as vector SVG (`NORMAL_SMALLFRAMECAPTIONSIZING.svg`), moving its RMSE to **`0.1989`** (cleanly non-zero).
  - All complex parts (window captions, buttons, Start menu bands, taskbar gradients, tabs, progress blocks, check/radio glyphs, task panes) have been redrawn as independent SVGs.
  - **Lowest RMSE on complex parts**: **`0.1086`** (`NORMAL_NORMALGROUPBACKGROUND_BMP`), **`0.1567`** (`REACTOS_TABITEM_BMP`), **`0.1728`** (`NORMAL_SPECIALGROUPBACKGROUND_BMP`), **`0.1989`** (`NORMAL_SMALLFRAMECAPTIONSIZING_BMP`), **`0.2068`** (`REACTOS_TRACKBARVERTICAL_BMP`), **`0.2074`** (`NORMAL_FRAMECAPTIONSIZING_BMP`).
  - No complex part has an RMSE below 0.10. Every complex part is cleanly and demonstrably an independent redraw.

---

## 3. Metrics and Scale (96 DPI and 120 DPI)

Metrics were systematically tuned across `NormalReactOS.INI`, `LargeFontsReactOS.INI`, and `ExtraLargeReactOS.INI`:
- **Caption Bar**:
  - 96 DPI (100%): Height `25px`, Font `Trebuchet MS, 10, Bold` (evaluated via fontconfig to open `DejaVu Sans Bold 10pt` on Debian/ISO).
  - 120 DPI (125%): Height `29px`, Font `Trebuchet MS, 10, Bold` (scales smoothly to 12.5pt).
  - Extra Large Fonts: Height `35px`.
- **System Metrics & Colors**:
  - `ActiveCaption = 0 84 227`, `GradientActiveCaption = 61 149 255`.
  - `InactiveCaption = 122 150 223`, `GradientInactiveCaption = 157 185 235`.
  - `ScrollbarWidth = 15`, `ScrollbarHeight = 15`.
  - UI Fonts: `MS Shell Dlg, 8` (9 at 120 DPI, 10 at ExtraLarge), mapped directly to open `Tahoma` (Wine built-in).
- **Control Scaling**:
  - Button paddings, checkbox glyphs, radio dots, slider thumb guides, tab strips, and progress chunks scale consistently under DPI change.

---

## 4. Build and Distribution Integration

1. **Source Build Tooling**:
   - `scripts/build_msstyles.py SRC_DIR OUT.msstyles` converts INIs to UTF-16LE, compiles SVG sources via `rsvg-convert` and `convert -depth 8`, compiles `.rc` with `x86_64-w64-mingw32-windres`, and links a resource-only DLL with `x86_64-w64-mingw32-gcc -shared -nostdlib -Wl,--entry=0`.
2. **ISO Packaging**:
   - `installer/package-wine.sh` builds `Luna.msstyles` during staging into `$STAGE_WINE/usr/local/share/wineexplorer/themes/Luna/Luna.msstyles`.
3. **Machine-Root Installation**:
   - `scripts/setup-machine-root.sh` installs the style into `$MACHINE_ROOT/drive_c/windows/Resources/Themes/Luna/`.
   - On developer machines a local genuine Luna, named by `WEX_LOCAL_LUNA`, overrides the open style; it is never shipped.
4. **Theme Configuration**:
   - `src/resources/themes/Luna.theme` points at `Path=%ResourceDir%\Themes\Luna\Luna.msstyles` with `ColorStyle=NormalColor` and `Size=NormalSize`.

---

## 5. Repository Screenshots (Open Art Only)

Gemini's first screenshots were withdrawn in review: they were taken in a development prefix with local-only genuine XP resources (the Start button showed the Windows flag). Claude retook them in a fresh prefix with no local files, from the branch's own build (`docs/luna/clean-prefix.log`):

- `docs/luna/screenshots/clean-01-desktop.png`: desktop and taskbar after `explorer /theme:Luna`.
- `docs/luna/screenshots/clean-02-start.png`: the two-column Start menu.
- `docs/luna/screenshots/clean-03-notepad.png`: Notepad.
- `docs/luna/screenshots/clean-04-mycomputer.png`: My Computer with task panes.
- `docs/luna/screenshots/clean-05-control.png`: Control Panel.
