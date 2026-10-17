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
  - `desk` (ID `100` Display), `sysdm` (ID `100` System), `timedate` (ID `100` Date/Time), `main` (ID `100` Mouse, `200` Keyboard), `intl` (ID `100` Regional), `mmsys` (ID `100` Sound), `ncpa` (ID `100` Network Connections, `101–104` tray network status states), `powercfg` (ID `100` Power Options, `200–204` battery/AC states), `explorer` (ID `100` Start Computer, `101` Start Programs), `wexres` (ID `701–702` speaker/muted tray icons).
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

## 5. Visual Review & Slots With Remaining Differences

After inspecting every comparison sheet (`icons-48.png`, `icons-32.png`, `icons-16.png`, `cursors-48.png`, and `cursors-32.png`) and the open-art previews:

### Windows XP Icon Set (`assets/iconpacks/xp.json`)
- **What reads well:**
  - **Yellow folder family (`4`, `5`, `20`, `22`, `34`, `36–39`, `43`, `46`, `137`, `138`, `172`, `175`, `210`, `231`, `235–238`):** Recolouring the ReactOS folder base from grey-blue into warm XP golden-amber (back leaf) and sunny yellow-gold (front flap) while keeping the foreground emblems intact makes the entire folder family read immediately as XP at 48, 32, and 16 px. `172` (open yellow folder over a golden network T-pipe), `210` (open yellow folder with red-amber checkmark), and `231` (open yellow folder with blue download arrow) match their XP counterparts closely.
  - **Recycle Bin family (`32`, `33`, `142`, `143`):** Recolouring the bin body to translucent icy silver-blue with cyan-blue and emerald-green recycle arrows, overflowing paper (`33`), a white document sheet (`142`), and a yellow folder (`143`) eliminates the olive-green ReactOS bin and reads as XP's Recycle Bin.
  - **Start menu & shell badges (`3`, `23`, `24`, `25`, `28`, `35`, `44`, `45`, `48`, `173`, `221`):** Window (`3`), Search (`23`, flipped with cyan lens and golden handle), Help (`24`, saturated royal blue), Run (`25`, window with motion lines), Turn Off Computer (`28`, orange-red rounded square with standby symbol), Desktop pad (`35`), Favorites (`44` and `173`, 5-pointed yellow star), Log Off (`45`, amber rounded square with white key), Security (`48`, golden brass padlock), and Power (`221`, royal-blue circle) all match the XP silhouettes and palettes.
- **Slots still approximate:**
  - **My Documents (`235`) and My Pictures / Music / Videos (`236–238`):** The folders are now warm XP yellow, and the emblems are ReactOS's house (`235`), film roll (`236`), green sixteenth-note (`237`), and clapperboard (`238`) sitting in front of the closed folder rather than sticking out of an open folder.
  - **Control Panel (`22`, `137`):** Uses the yellow folder with wrench and screwdriver rather than XP's blue checklist pad with pencil.
  - **Batch / Script file (`153`):** Uses ReactOS's document sheet with a grey gear-diamond rather than XP's small window with a golden gear.

### Windows 11 Fluent Icon Set (`assets/iconpacks/fluent.json`)
- **What reads well:**
  - **Recycle Bin (`32`, `33`)** from Tela's cylindrical trash SVGs edited with translucent pale blue-white walls, blue-grey rim/base, and two-tone blue recycle arrows (RMSE `0.11` at 48 px).
  - **Special folders (`231` Downloads in blue, `235` Documents in gold, `236` Pictures in cyan-teal, `237` Music in coral-red, `238` Videos in purple)** with centered emblems.
  - **Core shell folders, drives, monitors, and file types (`1–20`, `23–31`, `38–39`, `44–48`, `133–157`)**.
- **Slots still approximate:**
  - **Folder tree (`42`)** uses a split-panel layout glyph rather than a literal tree.
  - **Disconnect (`49`)** uses a red power tile rather than a network disconnect monitor.

### Cursor Sets (`assets/cursors/xp.json` and `assets/cursors/fluent.json`)
- **Fluent Cursors:** All 15 roles now match the exact Windows 11 glyph bounding boxes at both 32 and 48 px, including the circular blue/cyan ring spinner for `Wait` and `AppStarting` (at top-right of the pointer) and the top-left-pointing pen for `NWPen`.
- **XP Cursors:** All 15 roles have verified hotspots (`Help` at `(2, 2)`, `NWPen` at `(1, 1)`, `No` at `(13, 13)` on the 32 px grid, scaled to `(3, 3)`, `(2, 2)`, and `(20, 20)` at 48 px). `Wait` and `AppStarting` wrap ReactOS's static hourglass cursors in `.ani` containers.

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
