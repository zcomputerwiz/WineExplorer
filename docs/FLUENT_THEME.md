<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Fluent (Windows 11 style) Theme for WineExplorer

This document describes the design, implementation, open art provenance, cursor generation, system packaging, and verification evidence for the **Fluent (Windows 11 style)** theme in WineExplorer (`src/dlls/themeui`, `src/dlls/wexres`, `src/dlls/wexshell`, `src/dlls/desk.cpl`).

---

## 1. Goal & Architecture

The objective is to make "Fluent (Windows 11 style)" a complete, self-contained theme that a user selects in Display Properties (`desk.cpl`) on an installed system:
- **Full Shell Icon Coverage:** Special shell folders (Documents, Pictures, Music, Videos, Desktop, Downloads, Shared Documents), Control Panel applet modules, standard program shortcuts (Internet Explorer, Notepad, Calculator, Paint, Command Prompt, Windows Media Player), and notification area / tray icons.
- **Custom Cursor Scheme:** Windows 11-style high-contrast vector cursors (`.cur` and `.ani`), wired into the `.theme` file under `[Control Panel\Cursors]`.
- **Live Theme & Cursor Switching:** Changing the theme in Display Properties applies immediately to the running shell and all open windows via `WM_THEMECHANGED` and `SPI_SETCURSORS` without requiring a logoff or session restart.
- **Clean Fallback & Baseline Isolation:** Switching back to Luna, Royale, or Classic restores original XP icons and cursors with pixel identity.
- **System Packaging:** Built dynamically during `installer/package-wine.sh` using sparse checkouts of pinned source commits, and installed under `drive_c/windows/Resources/Themes/Fluent` in the shared machine root beside Luna and Royale.

---

## 2. Technical Implementation

### 2.1 Theme Path Resolution & Live Switching (Phase T1)
- **Path Resolution (`src/dlls/themeui/theme_engine.c`):**
  Previously, `explorer /theme:<name>` stored only the base theme name in `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\CurrentTheme`. However, `shellres.c` (`theme_pack_dir()`) requires the full path to locate the `IconPack=` directory setting in the `.theme` file.
  - Implemented and exported `theme_resolve_path(const WCHAR *theme_name, WCHAR *out_path, DWORD size)` in `themeui.dll` (`src/include/wextheme.h`).
  - Resolves short names (e.g. `"Fluent"`) to `%WinDir%\Resources\Themes\<Name>\<Name>.theme` or `%ResourceDir%\Themes\...`.
  - Updated `theme_load_file()` and `theme_apply_file()` to record the resolved full path into `CurrentTheme`.
  - Updated `src/dlls/wexshell/command.c` to route `/theme:<name>` through `theme_apply_file()`.
- **Display Properties Integration (`src/dlls/desk.cpl/theme_page.c`):**
  - Enumerates both loose `.theme` files and subdirectories (`Resources\Themes\*\*.theme`).
  - Matched selection against canonical base names and stored full canonical paths upon user selection.
  - Broadcasts `WM_THEMECHANGED` across all top-level windows upon `Apply`.

### 2.2 Live Cursor Refresh & Scheme Reset (Phase T4)
- **Cursor Scheme Reset in Wine `user32`:**
  - Added `reset_scheme_cursors()` to `wine/dlls/user32/cursoricon.c` (`user_private.h`).
  - Flushes cached scheme cursor handles (`scheme_cursor_cache` and `scheme_cursor_tried`) when `SPI_SETCURSORS` is called via `SystemParametersInfoW()` / `SystemParametersInfoForDpi()` (`sysparams.c`), or when `WM_SETTINGCHANGE` with `SPI_SETCURSORS` or `0` arrives in `DefWindowProcW()` (`defwnd.c`).
- **Live Application in Theme Engine (`src/dlls/themeui/theme_engine.c`):**
  - In `apply_registry_sections()`, parses `[Control Panel\Cursors]` from the `.theme` file into `HKCU\Control Panel\Cursors`.
  - If a theme lacks a cursor section (e.g. Luna, Royale, Classic), resets the cursor entries to empty strings (Windows default).
  - Calls `SystemParametersInfoW(SPI_SETCURSORS, 0, NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)` followed by `SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW))` to immediately update the active cursor without logoff.

### 2.3 Comprehensive Icon Coverage & Multi-Module Pack (Phase T3)
- **Mapping Specification (`assets/iconpacks/fluent.json`):**
  - Special shell folders: Shell32 icon slots 235 (My Documents), 236 (My Pictures), 237 (My Music), 238 (My Videos), 231 (Downloads), 35 (Desktop). Shared Documents uses the My Documents icon.
  - Control Panel modules: `xpres_desk.dll`, `xpres_sysdm.dll`, `xpres_timedate.dll`, `xpres_main.dll`, `xpres_intl.dll`, `xpres_mmsys.dll`, `xpres_ncpa.dll`, `xpres_powercfg.dll`, `xpres_explorer.dll`.
  - Application icons: `Icons/Apps/*.ico` for `iexplore`, `notepad`, `calc`, `mspaint`, `cmd`, `wmplayer`.
  - Notification area / tray icons: `xpres_wexres.dll` for power/battery, network/wireless, and audio volume.
- **Icon Pack Builder (`scripts/build_iconpack.py`):**
  - Extended to support multi-module builds (`xpres_<module>.dll`) from a unified JSON mapping.
  - Automatically extracts stand-alone multi-resolution `.ico` files into `Icons/Apps/`.
  - Builds cursors via `scripts/build_fluent_cursors.py` and populates `[Control Panel\Cursors]` in the generated `Fluent.theme`.
- **Control Panel applet icons:** Explorer (`src/dlls/wexres/shellres.c`, `sync_module_icons`) writes `HKCU\Software\WineExplorer\Module Icons`, one value per module (`desk` = path of the pack's `xpres_desk.dll`). A small shell32 change takes the icons of system-directory modules from there. Modules outside the system directory are never replaced.
- **WexRes Icon Override (`src/dlls/wexres/shellres.c`):**
  - Exported `WexResIconModule()` in `wexres.dll` (`wexres.spec`, `src/include/wexres.h`).
  - Tray components (`powertray.c`, `volumetray.c`, `nettray.c`, `wlan_status.c`, `wlan_dialog.c`) load tray icons via `WexResIconModule()` to reflect the active icon pack.

---

## 3. Open Art Sources & Rip Check Verification

In accordance with [`docs/ASSETS.md`](ASSETS.md):
- **Proprietary Microsoft Art:** Strictly excluded from the repository. No Windows binaries or artwork are copied or committed.
- **Allowed Open Sources:**
  1. **Fluent UI System Icons (Microsoft):** MIT License. Commit `a347af95478f989038cccbf085978865264c048a`. Licenced in [`LICENSE.md`](../LICENSE.md) and shipping beside the pack as `Icons/LICENSE-fluent`.
  2. **Tela Icon Theme (Ubuntu Budgie fork):** GPL-3.0 License. Commit `50f51f45d95c629cbb21530bf71c82cdb96269da`. Used for the Recycle Bin glyphs. Licenced in [`LICENSE.md`](../LICENSE.md) and shipping beside the pack as `Icons/LICENSE-tela`.
  3. **Fluent Cursors (`scripts/build_fluent_cursors.py`):** GPL-2.0-or-later project code. Vector drawing routines generating 32x32 RGBA cursor frames with embedded hotspots and animated `.ani` sequences.

### 3.1 Cursor Rip Check Results
Every generated cursor was rendered and checked against genuine host Windows cursors using ImageMagick normalized RMSE. Any near-pixel match (RMSE < 0.05) would disqualify the source. All cursors passed:

```
arrow               vs the Windows original: RMSE = 0.2691 -> PASS (clean, independent art)
help                vs the Windows original: RMSE = 0.3191 -> PASS (clean, independent art)
hand                vs the Windows original: RMSE = 0.4466 -> PASS (clean, independent art)
text select         vs the Windows original: RMSE = 0.1440 -> PASS (clean, independent art)
resize N-S          vs the Windows original: RMSE = 0.2276 -> PASS (clean, independent art)
resize W-E          vs the Windows original: RMSE = 0.2275 -> PASS (clean, independent art)
resize NW-SE        vs the Windows original: RMSE = 0.2343 -> PASS (clean, independent art)
resize NE-SW        vs the Windows original: RMSE = 0.2608 -> PASS (clean, independent art)
move                vs the Windows original: RMSE = 0.3676 -> PASS (clean, independent art)
handwriting         vs the Windows original: RMSE = 0.3537 -> PASS (clean, independent art)
```
Scan for embedded rasters (`data:image`): None found. Clean vector geometry only.

Provenance for every icon is generated and recorded in [`fluent.md`](iconpacks/fluent.md) with visual preview in [`fluent-preview.png`](iconpacks/fluent-preview.png).

---

## 4. Packaging & Machine Root Layout (Phase T2)

- **Sparse Checkouts during Packaging (`installer/package-wine.sh`):**
  - Fetches the pinned commits of `fluentui-system-icons` and `Tela-icon-theme` into `/var/cache/wineexplorer/iconsrc` using sparse checkouts. Sources are not vendored into git.
  - Builds the complete theme package into `$STAGE_WINE/usr/local/share/wineexplorer/themes/Fluent`.
  - Enclosed in an error trap: if building the pack fails, it emits a clear warning and skips the pack without breaking the ISO build.
- **Machine Root Installation (`scripts/setup-machine-root.sh`):**
  - Recursively copies directory-based themes from `$THEME_DATA_DIR/` into `$MACHINE_ROOT/drive_c/windows/Resources/Themes/<Name>/`.
  - Installs `Fluent.theme`, `Cursors/` (`*.cur`, `*.ani`), and `Icons/` (`xpres_*.dll`, `Apps/*.ico`, `LICENSE*`).
  - Sets 755/644 POSIX permissions and inheritance ACLs so all users can read theme resources.

---

## 5. Real-Shell Live Acceptance Evidence

All tests executed on the real shell (`explorer.exe /desktop=Shell /shell`) under Xvfb using the Win32 CPL automation driver (`scripts/cpl_driver.exe`).

### 5.1 Test Run Log (`docs/fluent-theme/logs/acceptance_suite.log`)
```
=== Running 100% DPI (96 DPI) acceptance test ===
Launching real shell at 96 DPI (1024x768x24)...
Opening Display Properties (desk.cpl)...
Applying Fluent in Display Properties...
Display Properties found: Sheet=00000000000100d6, Page=00000000000100f0, Combo=00000000000100f6, Apply=00000000000100e2
Theme count: 4, currently selected index: 2
  [0] Windows Classic
  [1] Fluent (Windows 11 style)
  [2] Windows XP (Luna) (ACTIVE)
  [3] Windows Royale
Selecting [1] 'Fluent (Windows 11 style)'...
Clicking Apply button (00000000000100e2) via BM_CLICK...
Applied theme 'Fluent' successfully

CurrentTheme (Fluent):
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes
    CurrentTheme    REG_SZ    C:\windows\Resources\Themes\Fluent\Fluent.theme
Capturing 01_fluent_start_menu...
Opening Control Panel folder...
Capturing 02_fluent_control_panel...
Opening My Computer folder...
Capturing 03_fluent_my_computer...
Switching back to Luna...
Display Properties found: Sheet=00000000000100d6, Page=00000000000100f0, Combo=00000000000100f6, Apply=00000000000100e2
Theme count: 4, currently selected index: 1
  [0] Windows Classic
  [1] Fluent (Windows 11 style) (ACTIVE)
  [2] Windows XP (Luna)
  [3] Windows Royale
Selecting [2] 'Windows XP (Luna)'...
Clicking Apply button (00000000000100e2) via BM_CLICK...
Applied theme 'Luna' successfully

Capturing 04_luna_reverted_start_menu...
Re-applying Fluent live...
Display Properties found: Sheet=00000000000100d6, Page=00000000000100f0, Combo=00000000000100f6, Apply=00000000000100e2
Theme count: 4, currently selected index: 2
  [0] Windows Classic
  [1] Fluent (Windows 11 style)
  [2] Windows XP (Luna) (ACTIVE)
  [3] Windows Royale
Selecting [1] 'Fluent (Windows 11 style)'...
Clicking Apply button (00000000000100e2) via BM_CLICK...
Applied theme 'Fluent' successfully

Capturing 05_fluent_reapplied_start_menu...

=== Running 150% DPI (144 DPI) acceptance test ===
Launching real shell at 144 DPI (1536x1152x24)...
Opening Display Properties (desk.cpl)...
Applying Fluent at 144 DPI...
Display Properties found: Sheet=00000000000100d6, Page=00000000000100f0, Combo=00000000000100f6, Apply=00000000000100e2
Theme count: 4, currently selected index: 2
  [0] Windows Classic
  [1] Fluent (Windows 11 style)
  [2] Windows XP (Luna) (ACTIVE)
  [3] Windows Royale
Selecting [1] 'Fluent (Windows 11 style)'...
Clicking Apply button (00000000000100e2) via BM_CLICK...
Applied theme 'Fluent' successfully

Capturing 06_fluent_150dpi_start_menu...
Opening Control Panel at 150 DPI...
Capturing 07_fluent_150dpi_control_panel...

All acceptance tests and screenshots completed successfully!
```

### 5.2 Real Shell Screenshots
Screenshots: pending; the first set was withdrawn in review 1 (see FLUENT_THEME_WORKORDER.md).

---

## 6. Interactive Install Test & Fresh ISO Evidence

The fresh ISO (`wineexplorer-wow64.iso`) was built with `installer/build-iso.sh --clean` and tested through the end-to-end interactive install test (`installer/tests/install-interactive.sh`).

### 6.1 Install Test Log (`docs/fluent-theme/logs/install-interactive.log`)
```
=== Interactive Install Test starting at Fri Oct 16 09:26:34 PM CDT 2026 ===
started gemini-wow64-test
found
--- welcome
found
--- license
found
--- partitions
found
found
--- created
found
--- format
found
--- copied
found
--- regional
found
found
--- computer
found
found
found
--- workgroup
found
--- oobe
found
found
found
--- users
found
found
--- finish
found
--- logon
found
clicked 758,337
found
found
--- rejected-password
found
--- alice-desktop
found
--- switch-user
clicked 782,415
found
found
--- desktop
=== Building test32.exe (i686-w64-mingw32-gcc) and test64.exe (x86_64-w64-mingw32-gcc) ===
=== Staging test executables into VM ===
Desktop user: bobsmith on :1
=== 1. Running 32-bit test program on desktop ===
found
/tmp/vmtest/gemini-wow64-test/steps/18-test32.png
PASS: Win32 test program window drawn
=== 2. Running Wine 32-bit notepad from syswow64 ===
found
/tmp/vmtest/gemini-wow64-test/steps/19-notepad32.png
PASS: 32-bit syswow64 notepad window drawn
=== 3. Running 64-bit test program ===
found
/tmp/vmtest/gemini-wow64-test/steps/20-test64.png
PASS: Win64 test program window drawn
=== 4. Alice (admin) installs program for all users into Program Files ===
PASS: Program installed by Alice for all users
=== 5. Bob (standard user) executes application installed by Alice ===
found
/tmp/vmtest/gemini-wow64-test/steps/21-bob-common-app.png
PASS: Bob executed program installed for all users
=== 6. Bob (standard user) is denied write access to machine root ===

PASS: Bob write access denied to machine root as expected
=== 7. Display Properties lists Fluent theme ===
found
/tmp/vmtest/gemini-wow64-test/steps/22-desk-cpl-fluent.png
  [1] Fluent (Windows 11 style)
PASS: Fluent theme is listed in Display Properties on installed system
=== Cleaning up running test processes ===
PASS: All WoW64 32-bit and 64-bit program checks and prefix-split multi-user assertions succeeded.
shut down gemini-wow64-test
PASS: installed LightDM, custom/recovery greeters, session, automatic logon and prefix isolation
Alice (alice) prefix size: 1 MB
Bob Smith (bobsmith) prefix size: 1 MB
PASS: full names, root/Alice passwords, Bob passwordless state, administrator membership, prefix-split layout and per-user prefix size < 20 MB
PASS (Bob desktop 5 s after logon); screenshots in /tmp/vmtest/gemini-wow64-test/steps; VM stopped
=== Interactive Install Test completed in 440s (440 seconds) ===
```

### 6.2 Installed System Verification Summary
1. **Interactive Installation:** Text setup (partition creation, format, copy), GUI setup, OOBE account creation, LightDM Welcome screen all passed cleanly.
2. **Multi-User & Security:** Alice (administrator with password, rejected incorrect password), Bob Smith (passwordless user), user switching via Win+L, per-user prefix isolation (1 MB per prefix, well under 20 MB threshold).
3. **WoW64 Program Compatibility:** 32-bit custom Win32 test executable, 32-bit syswow64 notepad, and 64-bit custom Win64 test executable all launched, drew windows, and passed OCR verification.
4. **Display Properties Theme Enumeration:** `control.exe desk.cpl` launched on the installed desktop and enumerated `Fluent (Windows 11 style)` at index 1 alongside Windows Classic, Luna, and Royale.
5. **Machine Root Assets:** On the flushed disk, verified `Fluent.theme`, `Icons/xpres_shell32.dll`, and `Cursors/arrow.cur` under `/var/lib/wineexplorer/machine/drive_c/windows/Resources/Themes/Fluent/`.

---

## 7. Deliverables & Git Branch State

- **Branch:** `claude/fluent-fixes` (fixes after review 1 on top of `gemini/fluent-theme`)
- **Submodule `wine`:** commit `b1d3ab0` on branch `claude/fluent-fixes` of the Wine fork.
- **Raw Logs Committed:**
  - `docs/fluent-theme/logs/acceptance_suite.log`
  - `docs/fluent-theme/logs/install-interactive.log`
- **Acceptance Screenshots Committed:**
  Screenshots: pending; the first set was withdrawn in review 1 (see FLUENT_THEME_WORKORDER.md).
- **Documentation:**
  - `docs/FLUENT_THEME.md` (this report)
  - `docs/ASSETS.md` (updated rip-check verification and cursor sources)
  - `docs/STOCK_CHANGES.md` (documented `user32` cursor reset and `shell32` module icon override)
  - `docs/iconpacks/fluent.md` & `docs/iconpacks/fluent-preview.png` (regenerated icon provenance)
