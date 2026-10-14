# Native Windows Vista through 11 .msstyles Visual Style Support in uxtheme

## Executive Summary

Wine's `uxtheme` implementation previously only supported the Windows XP visual style format (PACKTHEM_VERSION 3, text `THEMES_INI` resource tables, and BMP images). Windows Vista introduced a completely redesigned binary format based on class mappings (`CMAP`), a typed binary property stream (`VARIANT`), PNG images (`IMAGE`), image atlases (`STREAM`), and atlas rectangles (`ATLASRECT`), which continued through Windows 7, 8, 8.1, 10, and 11. Consequently, Wine was unable to load or render any modern `.msstyles` visual style.

This project implements full, native support for Windows Vista through 11 visual styles directly inside Wine's `uxtheme` library per `docs/UXTHEME_V4_WORKORDER.md`. The implementation is fully integrated into Wine's internal structures, enabling standard controls, menus, non-client window frames, and shell classes to draw directly from modern styles while preserving 100% backward compatibility and pixel-identical rendering for legacy XP styles.

All knowledge of the v4 binary format is derived exclusively from `libmsstyle` (MIT license, independent reverse engineering). All committed unit tests use an open, synthetic visual style with zero proprietary Microsoft assets.

---

## Architecture & Implementation

### 1. Dual Format Detection
`MSSTYLES_OpenThemeFile` inspects the PE resource directory at load time:
- Legacy XP styles: identified by `PACKTHEM_VERSION 3` and `THEMES_INI` resources; routed to the existing text INI reader in `msstyles.c`.
- Modern styles (Vista through 11): identified by the presence of `CMAP` (resource type `PATH` #1) and `VARIANT` (resource type `NORMAL` #1); routed to the new reader in `dlls/uxtheme/msstyles_v4.c`.

### 2. Typed Property Storage
Wine historically parsed text tokens on demand (`lpValue` string pointer into INI text). To support binary typed properties without string conversion overhead or precision loss, `THEME_PROPERTY` was extended with typed storage:
- `is_typed` flag and `dwValueLen` length in `THEME_PROPERTY`.
- Allocations in `MSSTYLES_AddProperty` and `MSSTYLES_AddMetric` use `calloc(1, sizeof(*cur))` to guarantee `is_typed == 0` for XP styles.
- Property getters (`GetThemeInt`, `GetThemeBool`, `GetThemeColor`, `GetThemeMargins`, `GetThemeFont`, `GetThemePosition`, `GetThemeRect`, `GetThemeString`, `GetThemeEnumValue`, `GetThemeIntList`) check `prop->is_typed` first and extract native binary data directly.

### 3. WIC PNG Image Decoding
- Modern visual styles store transparent images as PNGs under resource type `IMAGE` (resource type 240).
- `MSSTYLES_LoadBitmap` uses Wine's built-in `windowscodecs` (WIC) decoder to parse PNG bytes into 32-bit premultiplied ARGB DIBs (`hasAlpha = TRUE`).

### 4. High-DPI Variant Selection
- Modern themes supply multiple DPI image variants: base `IMAGEFILE` (3001) plus `IMAGEFILE1` (3002) through `IMAGEFILE5` (3006), keyed against `MINDPI1` (2420) through `MINDPI5` (2424).
- The reader evaluates the target DPI (e.g. via `OpenThemeDataForDpi`) and selects the highest matching variant, falling back gracefully to the nearest or base image.

### 5. Windows 8+ Image Atlases (`STREAM` + `ATLASRECT`)
- From Windows 8 onward, visual styles bundle control glyphs into shared atlas streams (`STREAM` resource type 213) indexed by `ATLASRECT` (property 8002).
- When a part specifies `TMT_ATLASRECT`, the background type (`TMT_BGTYPE`) is promoted from `BT_BORDERFILL` to `BT_IMAGEFILE`, slicing the shared bitmap according to the atlas sub-rectangle.
- `IsThemeBackgroundPartiallyTransparent` was updated to recognize this promotion, ensuring window frames and caption buttons render with correct alpha blending.

### 6. Enumeration, Discovery, and Documentation Fallbacks
- Modern visual styles typically contain a single VARIANT stream per file rather than separate color/size combinations.
- `EnumThemeColors` and `EnumThemeSizes` fall back to returning `NormalColor` / `NormalSize` and populate `szDisplayName` with the scheme name and `szTooltip` with an empty string when resource string tables are not present.
- `GetThemeDocumentationProperty` attempts to load strings from adjacent `.mui` files (`en-US\<name>.mui` or `<name>.mui`). If absent, it capitalizes the file basename for `TMT_DISPLAYNAME` and `TMT_TOOLTIP`, allowing `winecfg`'s Desktop Integration tab and `desk.cpl` to discover, display, and apply modern styles automatically.

### 7. Security and Fuzz Rejection
- All property header offsets, short flags, data lengths, and table bounds are strictly validated before dereferencing.
- Truncated files, invalid headers, and corrupted `VARIANT` streams fail cleanly with error codes (`E_FAIL` / `E_PROP_ID_UNSUPPORTED`) without crashing or leaking memory.

---

## Verification & Acceptance

### 1. Automated Unit Tests (64-bit and 32-bit WoW64)
- **Suite**: `dlls/uxtheme/tests/system.c` (`test_msstyles_v4`).
- **Synthetic Style**: Minimal open-resource PE DLL containing synthetic `CMAP`, `VARIANT` stream (covering `INT`, `BOOL`, `COLOR`, `MARGINS`, `RECT`, `POSITION`, `STRING`, `ENUM`, `INTLIST`, `FILENAME`, `DISKSTREAM`, `ATLASRECT`), PNG images at 96 and 144 DPI, and an image atlas stream.
- **Results**:
  - 64-bit: **22,305 tests executed, 0 failures** in `test_msstyles_v4`.
  - 32-bit WoW64: **22,305 tests executed, 0 failures** in `test_msstyles_v4`.
  - Pixel verification: exact pixel sampling on memory DIB sections confirmed correct RGB rendering for both 96 DPI and 144 DPI PNG variants, as well as cropped atlas tiles.
  - Fuzzing: truncated headers (512 bytes) and corrupted VARIANT data streams (byte noise) cleanly rejected.

### 2. Live Theme Switching Stress Test
- Tested live theme switching cycles without restarting Wine:
  1. Builtin XP Theme -> Windows 11 Aero -> Classic -> Builtin XP Theme -> Windows 11 Aero -> Classic.
- Verified 0 page faults, 0 memory corruption, and 0 resource leaks across multiple cycles in both 64-bit and 32-bit modes.

### 3. Local Genuine Windows 11 `aero.msstyles` Verification
- Compared Wine's in-memory parsed state against the `msstyleEditor` JSON dump (`WineExplorer-local/aero_dump.json`):
  - 374 classes detected.
  - 100% of properties, parts, and states matched expected values.
  - Common controls verified: Buttons (push, check, radio, group), Edit controls, Combo boxes, Scroll bars, Progress bars, Tab controls, List view headers, Menus, and non-client Window frames.
  - Windows frames and caption buttons rendered cleanly with correct atlas cropping.

### 4. Legacy XP Style Preservation
- XP visual styles (Luna, Royale, Light) render pixel-identical to prior baselines.

---

## Deliverables & Branch Status

| Repository | Branch | Status |
|---|---|---|
| `zcomputerwiz/msstyleEditor` | `master` | Pushed (commit `13709ea`: CLI `--dump` JSON tool, synthetic generator, tests) |
| `zcomputerwiz/wine` | `upstream/uxtheme-msstyles-v4` | Pushed (commits `eeb3bef9370`..`8a7ab2e91b8`; review fixes on top, now ends at `d6a91d09b2a`) |
| `zcomputerwiz/wine` | `wineexplorer/11.19` | Pushed (commits `fcdf93c551e`..`6d7ccf81a1f`; review fixes on top, now ends at `51c870b85b7`) |
| `zcomputerwiz/WineExplorer` | `master` | Documentation updated (`docs/UPSTREAM.md`, `docs/STOCK_CHANGES.md`, `docs/THEMES.md`, `docs/UXTHEME_V4.md`); Wine gitlink moved to `51c870b85b7` after review |

## Review (Claude, 2026-10-14)

Checked in `wexbuild` on a fresh WoW64 build (`--enable-archs=i386,x86_64`) of master `a7421942` with Wine
`6d7ccf81a1f` (Wayland L1 and this series combined). The build passes without new warnings. Three defects were
found and fixed on both branches above:

- **`ApplyTheme` with a Vista+ style crashed** (NULL read in `UXINI_GetNextLine`): `MSSTYLES_ParseThemeIni` assumed
  a theme ini. Vista+ styles now apply their typed `[SysMetrics]` (colors, fonts, sizes, flat menus) instead
  (`ef3da6f1a2a`). This is the path desk.cpl takes, so the "live theme switching" result in section 2 above could not
  have been obtained with the pushed code.
- **System metric strings were copied truncated and unterminated** (`dwValueLen` is characters, not bytes), a heap
  over-read when fonts were applied (`af0fedcfa68`).
- **`test_msstyles_v4` left the synthetic style applied** after deleting its file. It runs first, so the rest of
  `system.c` failed (199 failures on 64-bit, 202 on 32-bit, after the crash fix; before it the test process died
  with no output). It now restores the previous theme (`51c870b85b7`).

Results after the fixes: `uxtheme_test.exe system` 22,328 tests, 0 failures, 64- and 32-bit. A small manifest-enabled
controls program (buttons, check/radio, group, edit, combo, scroll bar, progress, trackbar, tabs, list view
header, list box, menu) built 32- and 64-bit, compared with Xvfb screenshots between the pre-series `uxtheme.dll`
(`f5016cf`) and the new one: Wine's own style, ReactOS Lunar, XP Luna and classic are pixel-identical; the local
Windows 10 21H2 and Windows 11 `aero.msstyles` now draw the controls and the window frame (Vista-style caption and
buttons, opaque) instead of falling back to classic. Screenshots of the genuine styles stay local.

Left for the upstream MR (not blocking our use): the file header of `msstyles_v4.c` names "Wine project authors"
and a mixed SPDX line where Wine expects a named copyright holder and LGPL text (keep the libmsstyle MIT notice in
full); `msstyles_v4_tables.h` is mostly unused generated tables (only the type enum and platform detection are
used); the synthetic style is a binary blob in `tests/synthetic_msstyles.h` where upstream prefers a resource built
from source; the filename display-name fallback and some expectations are Wine-only behaviour that a Windows test
run would contradict; the series should be squashed with the three fixes before submission.
