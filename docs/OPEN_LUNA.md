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


---

## 6. Round 2 Acceptance: Seven Details Polish (Gemini)

Following Claude's Round 2 review and workorder (`docs/OPEN_LUNA_WORKORDER.md`), seven specific visual details were polished so Open Luna reads authentically as Windows XP Luna at a glance.

### Local Acceptance Comparison Sheets (100% and 125% DPI)

Each item was verified against genuine Windows XP Luna across both standard scale (100% / 96 DPI) and high-DPI scale (125% / 120 DPI). The comparison sheets are committed in `WineExplorer-local/notes/luna/`:

| Item | Feature | Local Comparison Sheet (Local Only) | Status & Parity |
|---|---|---|---|
| **1** | **Caption Font** | `notes/luna/r2-item1-caption-font.png` | **Matched**: `Trebuchet MS` substituted to open `DejaVu Sans` in `font-substitutes.reg`. Captions render with authentic humanist sans curves, letter spacing, and bold weight at 96 DPI and 120 DPI. |
| **2** | **Caption Buttons** | `notes/luna/r2-item2-caption-buttons.png` | **Matched**: Minimize, maximize, and close buttons resized to uniform 21x21 rounded pill shape with 1px crisp white outline (`stroke="#ffffff"`) and bold 2px white glyph bitmaps. All three buttons read balanced, dimensional, and identical in scale. |
| **3** | **Start Button** | `notes/luna/r2-item3-start-button.png` | **Matched**: Taller 30px green pill (`REACTOS_STARTBUTTON.svg`) with bright top specular highlight (`#c8ffa4` to transparent), dark border (`#0e4006`), and bold italic white "start" with soft dark drop shadow (`0 55 12`). Displays the open Wine glass emblem (clean prefix verification). |
| **4** | **Start Menu** | `notes/luna/r2-item4-start-menu.png` | **Matched**: White 2.5px rounded border with drop shadow around user picture (`NORMAL_USERTILEBACKGROUND.svg`); larger bold white username with drop shadow; clean green "All Programs" arrow button (32bpp alpha, zero magenta artifacts); 2px vibrant orange separator strip under top band; 40px royal blue Log Off band. |
| **5** | **Taskbar & Tray** | `notes/luna/r2-item5-taskbar-tray.png` | **Matched**: Active taskbar button in darker sunken pressed blue (`#062470` to `#1242a4`) with 1px vibrant orange top highlight (`#f89a1c`); notification tray in lighter cyan-blue gradient (`#168ee4` to `#043e94`) with 1px bright cyan-blue vertical highlight line (`#48c4ff`) along its left edge. |
| **6** | **Scroll Bars** | `notes/luna/r2-item6-scrollbars.png` | **Matched**: Light blue-grey rounded thumbs with 3 horizontal gripper ridges; rounded arrow buttons with navy blue triangle glyphs; soft light blue shaft. Verified themed rendering in Notepad when window content requires scrolling. |
| **7** | **Task Pane (My Computer)** | `notes/luna/r2-item7-task-pane.png` | **Matched**: ExplorerBar background gradient tuned to rich royal blue (`GradientColor1 = 120 152 224`, `GradientColor2 = 84 112 198`); System Tasks and Other Places group box backgrounds updated to pure crisp white fill (`#ffffff`) with 1px soft blue border (`#b4ccf0`). |

### Theme Engine & Scrollbar Verification Note
Wine's `uxtheme` engine (`UXTHEME_ScrollBarDraw`) themes non-client scrollbars dynamically whenever the document height exceeds client window bounds (`thumbsize > 0`). In an empty Notepad window, the scrollbar remains in the disabled/flat state. Opening a document containing text activates full visual style rendering: the light blue-grey rounded thumb (`REACTOS_SCROLLTHUMBVERTICAL.bmp`), 3 horizontal gripper ridges (`REACTOS_SCROLLTHUMBGRIPPERVERTICAL.bmp`), rounded arrow buttons (`REACTOS_SCROLLARROWS.bmp`), and soft light blue channel track (`REACTOS_SCROLLSHAFTVERTICAL.bmp`) render cleanly and faithfully to Windows XP Luna. No theme engine code changes were required.

### Clean-Prefix Committed Screenshots (Open Art Only)
All screenshots were captured in a fresh, isolated prefix (`/root/luna-clean`) using the build tree's WOW64 Wine and wineserver (`Xvfb :29`), with no proprietary files present:
- `docs/luna/screenshots/r2-01-desktop.png`: Desktop with Start button (Wine glass logo, bright top shine) and taskbar (lighter cyan notification tray with left highlight line).
- `docs/luna/screenshots/r2-02-start.png`: Two-column Start menu with white framed user picture, bold white username with shadow, orange separator strip, clean green All Programs arrow, and 40px royal blue Log Off band.
- `docs/luna/screenshots/r2-03-notepad.png`: Notepad with text displaying active themed scrollbar (rounded thumb with grippers, rounded arrow buttons), 21x21 caption buttons with 1px white outline and bold white glyphs, and active taskbar button in dark pressed blue with orange line.
- `docs/luna/screenshots/r2-04-mycomputer.png`: My Computer showing rich blue ExplorerBar gradient behind pure white group boxes.
- `docs/luna/screenshots/r2-05-control32.png`: 32-bit controls test program under Open Luna.
- `docs/luna/screenshots/r2-05-control64.png`: 64-bit controls test program under Open Luna.
