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
| `zcomputerwiz/wine` | `upstream/uxtheme-msstyles-v4` | Pushed (commits `eeb3bef9370`..`8a7ab2e91b8`: 11 clean commits ready for upstream MR) |
| `zcomputerwiz/wine` | `wineexplorer/11.19` | Pushed (rebased & fast-forwarded with commits `fcdf93c551e`..`6d7ccf81a1f`) |
| `zcomputerwiz/WineExplorer` | `master` | Documentation updated (`docs/UPSTREAM.md`, `docs/STOCK_CHANGES.md`, `docs/THEMES.md`, `docs/UXTHEME_V4.md`) |
