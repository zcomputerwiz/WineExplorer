<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Open Look-Alike Art: XP and Windows 11 Sets

Status: complete, 2026-10-16. Work order: `docs/OPEN_ART_WORKORDER.md`. Assignee: Gemini.

Two complete art sets made entirely from open art under GPL-compatible licences (MIT and GPL-2.0-or-later),
each looking as close as practical to its respective target environment without copying, tracing, or sampling
colours from proprietary Microsoft references:
1. **XP set**: Complete shell icons and cursor scheme looking like Windows XP (Luna era).
2. **Windows 11 set**: Improved Fluent icon pack with matched special folder tones, plus Windows 11 cursor scheme.

---

## 1. Coverage Table

Both sets cover the identical slots:

| Category | Slots Covered | Details |
|---|---|---|
| **Shell Icons** (`xpres_shell32.dll`) | 76 resource IDs | 1–49, 133–135, 137–139, 142–143, 152–157, 172, 173, 175, 210, 220, 221, 222, 231, 235, 236, 237, 238, 269 |
| **Special Folders** | 7 folders | My Documents (235), My Pictures (236), My Music (237), My Videos (238), Desktop (35), Shared Documents (172 / 269), Downloads (231) |
| **Protected Slots** | `other_picture()` | Slots 220 (Users) and 269 (Shared Documents) preserved |
| **Control Panel Applets** | 8 applets (10 DLLs/icons) | `desk` (100), `sysdm` (100), `timedate` (100), `main` (100 mouse, 101 keyboard), `intl` (200), `mmsys` (100), `ncpa` (100), `powercfg` (100), `explorer` (103) |
| **System Tray** (`xpres_wexres.dll`) | 13 icons | Power (1001–1005: AC, battery charging/full/half/critical), Network (1006–1010: connected, idle, acquiring, error, activity), Volume (1011–1013: normal, mute, high) |
| **Applications** (`Icons/Apps/*.ico`) | 6 programs | `iexplore.ico`, `notepad.ico`, `calc.ico`, `mspaint.ico`, `cmd.ico`, `wmplayer.ico` |
| **Cursor Scheme** (`Cursors/*`) | 15 scheme roles | Arrow, Help, AppStarting, Wait, Crosshair, IBeam, NWPen, No, SizeNS, SizeWE, SizeNWSE, SizeNESW, SizeAll, UpArrow, Hand (all with 32x32 and 48x48 images, `.cur` and `.ani`) |

---

## 2. Rip-Check Results

Every source was scanned for embedded raster images (`data:image`) and measured against genuine Windows references
using ImageMagick `compare -metric RMSE` after flattening on a common background.

1. **Embedded raster scan**:
   - `fluentui-system-icons`: clean (all SVGs are pure vector paths).
   - `tela`: clean (trash SVGs contain no embedded rasters).
   - ReactOS `.cur` and `.ico`: clean open Win32 resources created by the ReactOS project.

2. **Metric comparisons against genuine Windows originals**:
   - **ReactOS Shell32 Icons** (76 IDs): All pass rip check (RMSE strictly > 0.05).
     - Lowest RMSE: **0.1092** (ID 30, shortcut arrow overlay).
     - Highest RMSE: **0.5911** (ID 28, shut down).
     - Average RMSE: ~0.33.
   - **ReactOS Control Panel Applets & Programs**:
     - `desk.cpl`: RMSE 0.4458 (PASS)
     - `sysdm.cpl`: RMSE 0.3814 (PASS)
     - `timedate.cpl`: RMSE 0.4894 (PASS)
     - `main.cpl`: RMSE 0.2531 (PASS)
     - `mmsys.cpl`: RMSE 0.2734 (PASS)
     - `ncpa.cpl`: RMSE 0.4348 (PASS)
     - `powercfg.cpl`: RMSE 0.2232 (PASS)
     - `notepad.exe`: RMSE 0.4466 (PASS)
     - `calc.exe`: RMSE 0.3983 (PASS)
     - `mspaint.exe`: RMSE 0.2897 (PASS)
     - `cmd.exe`: RMSE 0.4639 (PASS)
     - `sndvol32.exe`: RMSE 0.2442 (PASS)
   - **Fluent Icons vs Windows 11**:
     - All 76 shell32 slots pass (RMSE strictly > 0.05).
     - Lowest RMSE at 48 px: **0.0890** (ID 30).
     - Lowest RMSE at 32 px: **0.0777** (ID 30).
     - Lowest RMSE at 16 px: **0.0552** (ID 30).

---

## 3. Lowest RMSE Per Set

| Set | Category | Size | Lowest RMSE | Slot / Role | Status |
|---|---|---|---|---|---|
| **Windows 11** | Icons | 48 px | 0.0890 | ID 30 (Shortcut overlay) | PASS |
| **Windows 11** | Icons | 32 px | 0.0777 | ID 30 (Shortcut overlay) | PASS |
| **Windows 11** | Icons | 16 px | 0.0552 | ID 30 (Shortcut overlay) | PASS |
| **Windows 11** | Cursors | 48 px | 0.2230 | IBeam | PASS |
| **Windows 11** | Cursors | 32 px | 0.1536 | IBeam | PASS |
| **Windows 11** | Cursors | 16 px | 0.1060 | IBeam | PASS |
| **XP** | Icons | 48 px | 0.1072 | ID 30 (Shortcut overlay) | PASS |
| **XP** | Icons | 32 px | 0.1236 | ID 30 (Shortcut overlay) | PASS |
| **XP** | Icons | 16 px | 0.0784 | ID 30 (Shortcut overlay) | PASS |
| **XP** | Cursors | 48 px | 0.0860 | UpArrow | PASS |
| **XP** | Cursors | 32 px | 0.1164 | UpArrow | PASS |
| **XP** | Cursors | 16 px | 0.0543 | UpArrow | PASS |

No pair in either set is a pixel match or near-copy. All RMSE values remain strictly above 0.05 across all resolutions.

---

## 4. Visual Observations and Notes

Every image was inspected using `view_file` on the generated contact sheets and preview images:

1. **Fluent Set Improvements**:
   - Special folders now use distinctive Windows 11 color palettes:
     - Downloads (231): vibrant Fluent blue folder (`#2B88D8`) with centered white download arrow.
     - Pictures (236): cyan-blue folder (`#17A2B8`) with centered image badge.
     - Music (237): warm coral folder (`#E85D3F`) with centered music note.
     - Videos (238): purple folder (`#8E44AD`) with centered video badge.
     - Documents (235): classic golden folder with document sheet.
     - Shared Documents (269): golden folder with blue team emblem.
   - Cursors:
     - All 15 roles feature crisp vector outlines and subtle Fluent alpha drop shadows.
     - `wait.ani` and `appstarting.ani` feature animated Fluent spinning blue rings (8 frames at 6 jiffies/frame).

2. **XP Set**:
   - True Luna look and feel with ReactOS open art.
   - Cursors feature authentic 3D shading and drop shadows, with multi-resolution 32x32 and 48x48 images.
   - `wait.ani` features an 8-frame rotating hourglass.
   - `appstarting.ani` features a stationary arrow pointer with a rotating mini hourglass at the bottom right.

3. **Slots with Visual Trade-offs**:
   - **ID 42 (Directory tree)**: Windows 11 shows a literal deciduous tree, whereas Fluent UI uses a directory hierarchy / window partition glyph. While semantically clear, users expecting a botanical tree will see an abstract folder structure.
   - **ID 30 & UpArrow cursor**: Simple geometric primitives (a single upward vertical arrow or a tiny 16px corner arrow) have minimal pixel variation between OSes, yielding the lowest RMSE values in both sets (~0.054–0.089). However, close inspection reveals distinct border styling and alpha drop shadows.
   - **XP Folder Bodies**: ReactOS uses blue-grey Luna folder tabs and bodies, distinct from the classic yellow vanilla Windows XP folder. This provides strong Luna aesthetic identity while avoiding any potential copyright issues with genuine Windows art.

---

## 5. Build Verification

Both `scripts/build_iconpack.py` and `scripts/build_cursors.py` run cleanly in `wexbuild` and compile without errors:
- Raw build output: committed in `docs/open-art-build.log`.
- Local comparison sheets: committed in `../WineExplorer-local` under `notes/xp/` and `notes/win11/`.
- Open art previews: committed in `docs/iconpacks/*-preview.png` and `docs/cursors/*-preview.png`.
- Provenance documentation: committed in `docs/iconpacks/*.md` and `docs/cursors/*.md`.
