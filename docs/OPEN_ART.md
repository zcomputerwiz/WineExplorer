# Open Look-Alike Art Sets (XP & Windows 11)

This document records the construction, coverage, provenance, rip-check verification, and visual assessment for the four open-source look-alike art sets in WineExplorer:

1. **Windows XP Icon Set** (`assets/iconpacks/xp.json` → `docs/iconpacks/xp.md`, `docs/iconpacks/xp-preview.png`)
2. **Windows 11 Fluent Icon Set** (`assets/iconpacks/fluent.json` → `docs/iconpacks/fluent.md`, `docs/iconpacks/fluent-preview.png`)
3. **Windows XP Cursor Set** (`assets/cursors/xp.json` → `docs/cursors/xp.md`, `docs/cursors/xp-preview.png`)
4. **Windows 11 Fluent Cursor Set** (`assets/cursors/fluent.json` → `docs/cursors/fluent.md`, `docs/cursors/fluent-preview.png`)

All sets are built from open-source upstream repositories pinned to exact commit SHAs and passed via `--source NAME=PATH` to `scripts/build_iconpack.py` and `scripts/build_cursors.py`. Every rendered icon size (16, 24, 32, 48, and 256 px) and cursor frame (32 and 48 px) is written as 8-bit RGBA (`PNG32`).

---

## 1. Coverage

### Icon Sets (`assets/iconpacks/xp.json` and `assets/iconpacks/fluent.json`)

Both icon packs build multi-resolution `.ico` resources at **16, 24, 32, 48, and 256 px** (8-bit RGBA at all sizes) and compile them into resource-only PE DLLs (`xpres_<module>.dll`) plus standalone application `.ico` files (`Icons/Apps/*.ico`):

- **Shell icons (`xpres_shell32.dll`, 74 resource IDs):**
  - `1–21`: Documents (`1`, `2`), executable window (`3`), closed and open folders (`4`, `5`), floppy/removable/fixed/network/optical/RAM drives (`6–13`), network globe and computers (`14–19`), Programs folder (`20`), Recent Documents (`21`).
  - `22–31`: Control Panel (`22`), Search (`23`), Help (`24`), Run (`25`), Sleep/Standby (`26`), Eject (`27`), Shut Down / Turn Off (`28`), Sharing and Shortcut overlays (`29`, `30`), Scheduled Tasks (`31`).
  - `32–49`: Recycle Bin empty and full (`32`, `33`), Dial-Up Networking folder (`34`), Desktop / Taskbar (`35`), Program group and Control Panel folders (`36`, `37`), Printers folder (`38`), Fonts folder (`39`), Taskbar (`40`), Audio CD (`41`), Folder tree (`42`), Network computer folder (`43`), Favorites star (`44`), Log Off (`45`), Search / Folder up (`46`), Update globe (`47`), Security lock (`48`), Disconnect (`49`).
  - `133–175`: Multiple documents (`133`), Find document/computer (`134`, `135`), Control Panel (`137`), Printers folder and Add Printer (`138`, `139`), file and folder in Recycle Bin (`142`, `143`), text/script/INI/font files (`152–157`), Shared / Network folder (`172`), Favorites (`173`), Network connections (`175`). (Shared Documents uses the My Documents icon in the shell; Wine's user picture slots `220` and `269` are excluded by `other_picture()`.)
  - `210–238`: Folder options (`210`), Power / Shut Down badge (`221`), DVD drive (`222`), Downloads (`231`), My Documents (`235`), My Pictures (`236`), My Music (`237`), My Videos (`238`).
- **Control Panel, Shell & System Tray Modules (`xpres_<module>.dll`):**
  - `desk` (ID `100` Display), `sysdm` (ID `100` System), `timedate` (ID `100` Date and Time), `main` (ID `100` Mouse, `200` Keyboard), `intl` (ID `200` Regional Options), `mmsys` (ID `100` Sounds), `ncpa` (ID `100` Network Connections), `powercfg` (ID `100` Power Options), `explorer` (ID `103` Show Desktop), `wexres` (ID `1001`, `1002`, `1003`, `1004`, `1005`, `1006`, `1007`, `1008`, `1009`, `1010`, `1011`, `1012`, `1013`, `1015`, `1016`, `1017`, `1018`, `1019`, `1020`, `1021` tray: power, network and volume). The ids are the modules' own icon ids (checked against the built modules).
- **Application Icons (`Icons/Apps/*.ico`):**
  - `iexplore.ico`, `notepad.ico`, `calc.ico`, `mspaint.ico`, `cmd.ico`, `wmplayer.ico`.

### Cursor Sets (`assets/cursors/xp.json` and `assets/cursors/fluent.json`)

Both cursor sets cover all **15 standard desktop cursor roles** (`Arrow`, `Help`, `AppStarting`, `Wait`, `Crosshair`, `IBeam`, `NWPen`, `No`, `SizeNS`, `SizeWE`, `SizeNWSE`, `SizeNESW`, `SizeAll`, `UpArrow`, `Hand`) at both **32×32 px (96 DPI)** and **48×48 px (144 DPI)** with scaled hotspots.

---

## 2. Sources and Licences

| Art Set | Upstream Sources | Commit SHA | Licence | Provenance Doc & Preview |
|---|---|---|---|---|
| **XP Icons** (`assets/iconpacks/xp.json`) | ReactOS Project Icons (`reactos`)<br>Fluent UI System Icons (`fluent`) | `974a4464497c68c1ebcf41ab372bec2c010e6f8b`<br>`a347af95478f989038cccbf085978865264c048a` | GPL-2.0-or-later and MIT | `docs/iconpacks/xp.md`<br>`docs/iconpacks/xp-preview.png` |
| **Fluent Icons** (`assets/iconpacks/fluent.json`) | Fluent UI System Icons (`fluent`)<br>Tela Icon Theme (`tela`) | `a347af95478f989038cccbf085978865264c048a`<br>`50f51f45d95c629cbb21530bf71c82cdb96269da` | GPL-3.0 (MIT glyphs + GPL-3.0 trash SVGs) | `docs/iconpacks/fluent.md`<br>`docs/iconpacks/fluent-preview.png` |
| **XP Cursors** (`assets/cursors/xp.json`) | ReactOS Project Cursors (`reactos`) | `974a4464497c68c1ebcf41ab372bec2c010e6f8b` | GPL-2.0-or-later | `docs/cursors/xp.md`<br>`docs/cursors/xp-preview.png` |
| **Fluent Cursors** (`assets/cursors/fluent.json`) | Vector SVG geometry & Fluent UI System Icons (`fluent`) | `a347af95478f989038cccbf085978865264c048a` | MIT | `docs/cursors/fluent.md`<br>`docs/cursors/fluent-preview.png` |

---

## 3. Rip-Check Results and Lowest RMSE per Set

Every icon and cursor was checked against the genuine reference assets using the three-step rip-check procedure in `docs/ASSETS.md` (provenance check, embedded raster check on all SVG inputs, and normalized RMSE pixel comparison on white background at 48, 32, and 16 px; threshold `RMSE > 0.05`):

| Art Set | Embedded Raster Check | Lowest RMSE (48 px) | Lowest RMSE (32 px) | Lowest RMSE (16 px) | Highest RMSE (48 px) | Result |
|---|---|---|---|---|---|---|
| **XP Icons** (`xp.json`) | 0 embedded rasters | **0.1072** (ID 30 Shortcut overlay) | **0.1236** (ID 30) | **0.1537** (ID 17 Printer) | **0.3821** (ID 221 Power) | **PASS** (all 74 > 0.05) |
| **Fluent Icons** (`fluent.json`) | 0 embedded rasters | **0.0820** (ID 30 Shortcut overlay) | **0.0831** (ID 30) | **0.0758** (ID 30) | **0.5023** (ID 42 Folder tree) | **PASS** (all 74 > 0.05) |
| **XP Cursors** (`xp.json`) | N/A (open `.cur` bitmaps) | **0.0860** (`UpArrow`) | **0.1099** (`UpArrow`) | N/A | **0.3407** (`Help`) | **PASS** (all 15 > 0.05) |
| **Fluent Cursors** (`fluent.json`) | 0 embedded rasters | **0.0939** (`UpArrow`) | **0.0922** (`UpArrow`) | N/A | **0.2343** (`AppStarting`) | **PASS** (all 15 > 0.05) |

---

## 4. Windows 11 Cursor Bounding Boxes (32 px and 48 px)

Each Fluent cursor role is scaled so its rendered non-transparent glyph bounding box (`width×height`) matches the reference cursor at both 32 px and 48 px:

| Role | Ours (32 px) | Reference (32 px) | Ours (48 px) | Reference (48 px) |
|---|---|---|---|---|
| Arrow | 12x19 | 12x19 | 18x27 | 18x27 |
| Help | 21x23 | 21x23 | 30x33 | 30x33 |
| AppStarting | 23x27 | 23x27 | 34x39 | 34x39 |
| Wait | 22x22 | 22x22 | 30x30 | 30x30 |
| Crosshair | 19x19 | 19x19 | 25x25 | 25x25 |
| IBeam | 9x18 | 9x18 | 10x26 | 10x26 |
| NWPen | 24x24 | 24x24 | 30x30 | 30x30 |
| No | 16x16 | 16x16 | 24x24 | 24x24 |
| SizeNS | 9x23 | 9x23 | 15x35 | 15x35 |
| SizeWE | 23x9 | 23x9 | 35x15 | 35x15 |
| SizeNWSE | 17x17 | 17x17 | 25x25 | 25x25 |
| SizeNESW | 17x17 | 17x17 | 25x25 | 25x25 |
| SizeAll | 23x23 | 23x23 | 35x35 | 35x35 |
| UpArrow | 9x19 | 9x19 | 15x28 | 15x28 |
| Hand | 18x24 | 18x24 | 25x32 | 25x32 |

---

## 5. Visual Review & Round 3 Refinements

After inspecting every slot-by-slot before/after sheet (`16`, `32`, and `48` px) and the full comparison grids (`icons-48.png`, `icons-32.png`, `icons-16.png`, `cursors-48.png`, and `cursors-32.png`):

### Windows XP Icon Set (`assets/iconpacks/xp.json`)
- **Documents (`1`, `2`, `152`):**
  - `1` (file): Dog-eared white sheet containing a miniature blue-header window with six colourful icon blocks (red, green, blue, yellow, purple, cyan).
  - `2` (document): Dog-eared white sheet with a small landscape picture box at top-left, blue ruled text lines, and a bold red serif `A` at bottom-right.
  - `152` (text file): 3D tilted spiral-bound notepad with golden top binding rings, cyan cover trim, and charcoal horizontal text lines.
- **Special folders (`235?238`, `172`):**
  - Open yellow folders (warm amber back leaf + translucent golden-yellow front flap) with standing sheets/emblems rising out of the folder: a tilted blue-lined document page (`235` My Documents and `172` Shared Documents, with the golden network T-pipe below `172`), a tilted white-framed landscape photo with a red sphere (`236` My Pictures), a glossy silver-blue CD disc paired with a tall indigo eighth note (`237` My Music), and a tilted 35 mm blue filmstrip with sprocket holes (`238` My Videos).
- **Network & My Computer (`15`, `18`, `175`, `16`):**
  - `15` (My Network Places): Glossy 3D blue-and-green Earth globe with a white two-button corded mouse in front.
  - `18` (Network): Glossy 3D Earth globe behind an angled cyan-screen LCD monitor on a round silver-lavender pedestal.
  - `175` (Network Connections): Glossy 3D Earth globe with an angled translucent RJ-45 crystal plug and blue cable at bottom-right.
  - `16` (My Computer): Angled cyan-screen LCD monitor in front of a 3D lavender-blue mini-tower case with optical drive bays and a green LED.
- **Smaller XP refinements (`32`, `33`, `142`, `143`, `17`, `138`, `139`, `27`, `22`, `137`):**
  - `32`, `33`, `142`, `143` (Recycle Bin family): All three arrows of the recycle emblem on the translucent silver-blue bin are rich emerald green.
  - `17`, `138`, `139` (Printers family): 3D isometric grey desktop printer with a curved blue-white paper sheet rising from the top feeder and a glowing green status LED (`17`), combined with an open yellow folder behind at top-left (`138`) or a vertical yellow binder pad behind (`139`).
  - `27` (Eject): 3D isometric silver-grey drive unit with a bold green left-pointing eject arrow on the front face.
  - `22`, `137` (Control Panel): Dark-blue glossy tile with a tilted light checklist card, golden check mark, and light-blue pencil.
  - `40` and `47` are left as they are (classic non-Windows motifs).

### Windows 11 Fluent Icon Set (`assets/iconpacks/fluent.json`)
- **Downloads (`231`) & Documents (`235`) & Special Folders (`236?238`):**
  - `231` (Downloads): Rounded white square tile (`#FFFFFF` fill, `#BCCAE0` border) with a bold blue downward gradient arrow.
  - `235` (Documents): Slate grey-blue folder (`#546E82` back tab, `#9EB8D2`?`#6B88A4` body) with white document lines and a white square emblem.
  - `236` (Pictures), `237` (Music), `238` (Videos): Flat blue folder with white mountain peaks and sun circle (`236`), coral-to-purple gradient folder with white beamed eighth notes (`237`), and purple folder with filmstrip sprocket holes and white play triangle (`238`).
- **Drive family (`6?12`, `222`):**
  - Dark charcoal-slate flat drive bars (`#1E2228`?`#68707A` front face, `#C4CCD8`?`#DCE2EA` top bevel) with a glowing green LED on the left, paired with a dark 5.25" floppy (`6`), blue-grey 3.5" floppy (`7`), green-and-silver horizontal network pipe (`10`), grey pipe with red-orange `X` (`11`), iridescent CD disc (`12`), and large iridescent DVD disc with a dark `DVD` badge (`222`).
- **Desktop (`35`) & Monitor family (`15`, `16`, `18`, `175`, `26`, `47`, `135`):**
  - `35` (Desktop): Flat borderless cyan-blue screen with a dark navy bottom taskbar bar and two small system-tray squares.
  - `16` (This PC): Flat cyan-blue widescreen monitor with a thin dark bezel, light silver top edge, silver neck, and dark flat stand.
  - `15`, `18` (Network): Flat cyan-blue monitor with a blue-and-ice-white Earth globe at top-left.
  - `175` (Network Connections): Flat cyan-blue monitor above a horizontal green-and-silver network pipe.
- **Smaller & filled-shape refinements (`27`, `42`, `133`, `1?5`, `13`, `14`, `17`, `19?21`, `23`, `25`, `28?31`, `34`, `36?41`, `43?46`, `48`, `49`, `134`, `138`, `139`, `142`, `143`, `152?157`, `172`, `173`, `210`, `221`):**
  - `27` (Eject): 3D isometric silver-grey USB drive with a bold green left-pointing arrow above it.
  - `42` (Folder tree): Bushy green deciduous tree with a brown trunk and side branches.
  - `133` (Multi-page): Two overlapping crisp white dog-eared document sheets.
  - All remaining outline-only glyphs (documents `1, 2, 21, 134, 152?157`, window `3`, yellow folder family `4, 5, 20, 34, 37, 39, 43, 46, 172, 173, 210`, RAM chip `13`, globe `14`, printers `17, 138, 139`, network bus `19`, search lens `23`, slanted run window `25`, power/security/key badges `28, 45, 48, 49, 221`, overlays `29?31`, Start menu `40`, Music CD `41`, and Favorites star `44`) are now filled colour shapes rather than bare outlines.

### Builder Fix (`scripts/build_iconpack.py` and `scripts/build_cursors.py`)
- Removed `[Control Panel\Cursors]` `.theme` injection from `scripts/build_iconpack.py` and moved it into `scripts/build_cursors.py` so `build_iconpack.py` only writes icon-pack entries while `build_cursors.py` adds the cursor section.

### Cursor Sets (`assets/cursors/xp.json` and `assets/cursors/fluent.json`)
- **Fluent Cursors:** All 15 roles match the Windows 11 glyph bounding boxes at both 32 and 48 px, including the circular blue/cyan ring spinner for `Wait` and `AppStarting` and the top-left-pointing pen for `NWPen`.
- **XP Cursors:** All 15 roles have verified hotspots (`Help` at `(2, 2)`, `NWPen` at `(1, 1)`, `No` at `(13, 13)` on the 32 px grid, scaled to `(3, 3)`, `(2, 2)`, and `(20, 20)` at 48 px).

---

## 6. Clean Clone Build Output

From a fresh git clone (`/var/tmp/clean_clone_test`, with no `references/` directory) using pinned `--source` checkouts (`docs/open-art-build.log`):

```text
$ python3 scripts/build_iconpack.py assets/iconpacks/fluent.json /var/tmp/clean_out --source fluent=/root/iconsrc/fluentui-system-icons --source tela=/root/iconsrc/tela
/var/tmp/clean_out/Fluent: complete icon pack built from fluent, tela
$ python3 scripts/build_cursors.py assets/cursors/fluent.json /var/tmp/clean_out --source fluent=/root/iconsrc/fluentui-system-icons
Built /var/tmp/clean_out/Fluent/Cursors/arrow.cur (hotspot 32px=(0,0), 48px=(0,0))
Built /var/tmp/clean_out/Fluent/Cursors/help.cur (hotspot 32px=(0,0), 48px=(0,0))
Built /var/tmp/clean_out/Fluent/Cursors/appstarting.ani (8 frames, hotspot 32px=(0,8), 48px=(0,12))
Built /var/tmp/clean_out/Fluent/Cursors/wait.ani (8 frames, hotspot 32px=(16,16), 48px=(24,24))
Built /var/tmp/clean_out/Fluent/Cursors/crosshair.cur (hotspot 32px=(15,16), 48px=(23,23))
Built /var/tmp/clean_out/Fluent/Cursors/ibeam.cur (hotspot 32px=(15,16), 48px=(23,23))
Built /var/tmp/clean_out/Fluent/Cursors/handwriting.cur (hotspot 32px=(0,0), 48px=(0,0))
Built /var/tmp/clean_out/Fluent/Cursors/no.cur (hotspot 32px=(8,8), 48px=(12,12))
Built /var/tmp/clean_out/Fluent/Cursors/sizens.cur (hotspot 32px=(4,11), 48px=(7,17))
Built /var/tmp/clean_out/Fluent/Cursors/sizewe.cur (hotspot 32px=(11,4), 48px=(18,7))
Built /var/tmp/clean_out/Fluent/Cursors/sizenwse.cur (hotspot 32px=(8,8), 48px=(12,12))
Built /var/tmp/clean_out/Fluent/Cursors/sizenesw.cur (hotspot 32px=(8,8), 48px=(12,12))
Built /var/tmp/clean_out/Fluent/Cursors/sizeall.cur (hotspot 32px=(11,11), 48px=(17,17))
Built /var/tmp/clean_out/Fluent/Cursors/uparrow.cur (hotspot 32px=(4,0), 48px=(7,0))
Built /var/tmp/clean_out/Fluent/Cursors/hand.cur (hotspot 32px=(6,0), 48px=(10,0))
$ python3 scripts/build_iconpack.py assets/iconpacks/xp.json /var/tmp/clean_out --source reactos=/root/iconsrc/reactos --source fluent=/root/iconsrc/fluentui-system-icons
/var/tmp/clean_out/XP: complete icon pack built from fluent, reactos
$ python3 scripts/build_cursors.py assets/cursors/xp.json /var/tmp/clean_out --source reactos=/root/iconsrc/reactos
Built /var/tmp/clean_out/XP/Cursors/arrow.cur from reactos:win32ss/user/user32/resources/ocr_normal.cur (hotspot 32px=(2, 2), 48px=(3, 3))
Built /var/tmp/clean_out/XP/Cursors/help.cur from reactos:win32ss/user/user32/resources/ocr_help.cur (hotspot 32px=(2, 2), 48px=(3, 3))
Built /var/tmp/clean_out/XP/Cursors/appstarting.ani from reactos:win32ss/user/user32/resources/ocr_appstarting.cur (hotspot 32px=(2, 2), 48px=(3, 3))
Built /var/tmp/clean_out/XP/Cursors/wait.ani from reactos:win32ss/user/user32/resources/ocr_wait.cur (hotspot 32px=(16, 16), 48px=(24, 24))
Built /var/tmp/clean_out/XP/Cursors/crosshair.cur from reactos:win32ss/user/user32/resources/ocr_cross.cur (hotspot 32px=(11, 11), 48px=(16, 16))
Built /var/tmp/clean_out/XP/Cursors/ibeam.cur from reactos:win32ss/user/user32/resources/ocr_ibeam.cur (hotspot 32px=(6, 9), 48px=(9, 14))
Built /var/tmp/clean_out/XP/Cursors/handwriting.cur from reactos:win32ss/user/user32/resources/ocr_pen.cur (hotspot 32px=(1, 1), 48px=(2, 2))
Built /var/tmp/clean_out/XP/Cursors/no.cur from reactos:win32ss/user/user32/resources/ocr_no.cur (hotspot 32px=(13, 13), 48px=(20, 20))
Built /var/tmp/clean_out/XP/Cursors/sizens.cur from reactos:win32ss/user/user32/resources/ocr_sizens.cur (hotspot 32px=(15, 15), 48px=(22, 22))
Built /var/tmp/clean_out/XP/Cursors/sizewe.cur from reactos:win32ss/user/user32/resources/ocr_sizewe.cur (hotspot 32px=(15, 15), 48px=(22, 22))
Built /var/tmp/clean_out/XP/Cursors/sizenwse.cur from reactos:win32ss/user/user32/resources/ocr_sizenwse.cur (hotspot 32px=(15, 15), 48px=(22, 22))
Built /var/tmp/clean_out/XP/Cursors/sizenesw.cur from reactos:win32ss/user/user32/resources/ocr_sizenesw.cur (hotspot 32px=(15, 15), 48px=(22, 22))
Built /var/tmp/clean_out/XP/Cursors/sizeall.cur from reactos:win32ss/user/user32/resources/ocr_sizeall.cur (hotspot 32px=(15, 15), 48px=(22, 22))
Built /var/tmp/clean_out/XP/Cursors/uparrow.cur from reactos:win32ss/user/user32/resources/ocr_up.cur (hotspot 32px=(15, 6), 48px=(22, 9))
Built /var/tmp/clean_out/XP/Cursors/hand.cur from reactos:win32ss/user/user32/resources/ocr_hand.cur (hotspot 32px=(9, 2), 48px=(14, 3))
```
