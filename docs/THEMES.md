# Themes: how they are built, packaged and installed

A visual style is a resource-only DLL. Wine's uxtheme natively supports both Windows XP and Windows Vista through 11 visual styles:

### Windows XP layout (msstyles v3)
Read by `dlls/uxtheme/msstyles.c`:

| Resource | Holds |
|---|---|
| `THEMES_INI`, `COLORNAMES`, `SIZENAMES`, `FILERESNAMES`, `ORIGFILENAMES`, `MINDEPTH`, `COMBODATA` | the style's colour schemes, sizes, and which class-data file serves each combination |
| one `TEXTFILE` per colour/size combination (`<X>_INI`, UTF-16) | the class data: parts, states and their properties |
| `BITMAP` resources named after the image path (`Blue\StartButton.bmp` -> `BLUE_STARTBUTTON_BMP`) | the images the class data names |

`COMBODATA` is `cColors, cSizes` followed by, per size, the index of the file serving each colour.

### Windows Vista through 11 layout (msstyles v4)
Read by `dlls/uxtheme/msstyles_v4.c`:

| Resource | Holds |
|---|---|
| `CMAP` (`PATH` 1) | String table mapping class names to class IDs |
| `VARIANT` (`NORMAL` 1) | Binary property stream containing typed properties (`INT`, `BOOL`, `COLOR`, `MARGINS`, `RECT`, `POSITION`, `STRING`, `FONT`, `INTLIST`, `DISKSTREAM`, `FILENAME`) |
| `IMAGE` (PNG resources) | PNG images decoded via WIC into 32-bit premultiplied ARGB DIBs; DPI scaling selects `IMAGEFILE1`..`IMAGEFILE5` per screen DPI (`MINDPI1`..`MINDPI5`) |
| `STREAM` (Atlas PNGs) | Shared image atlas bitmap streams sliced via `ATLASRECT` (Windows 8+) |
| `AMAP` (Animations) | Animation duration maps and transitions |

## Packages
A theme package is a zip: `theme.json`, `<Name>.theme`, `<Name>/<name>.msstyles`, `<Name>/Icons/xpres_*.dll`, plus
cursors, sounds and wallpapers. It installs into `C:\windows\Resources\Themes\<Name>` and is applied with
`explorer /theme:<name>`.

The packages with Windows XP's own styles, icons, cursors and sounds are built on the developers' machines by the
local-only tooling (see `docs/ASSETS.md`) and are never committed. This repository ships the open parts: the
`.theme` files in `wine/resources/themes`, Wine's own styles, and the open icons and bitmaps.

## Icons and bitmaps belong to the theme
`<Name>.theme` gets a section
```
[WineExplorer]
IconPack=%ResourceDir%\Themes\<Name>\Icons
```
The shell (`shellres.c`) loads `xpres_<module>.dll` from the active theme's folder first, then from
`%WinDir%\Resources\Icons`, then falls back to Wine's shell32 and the bitmaps built into explorer.exe (ReactOS and our
own). Switching themes reloads them (`WM_THEMECHANGED`).

* Cursors: `[Control Panel\Cursors]` names the files; user32 reads them when a system cursor is loaded
  (`dlls/user32/cursoricon.c`, `SPI_SETCURSORS` is accepted).
* Sounds: the scheme is written as `[AppEvents\...]` sections of the `.theme`.
* Icons and bitmaps: the theme's `IconPack=` folder (see above).

## Not covered yet
- The Start panel pictures (`Luna\Shell\<scheme>\shellstyle.dll`) come only with a locally built package.
- Accelerated compositing and native Wayland frame integration are not implemented.
- Start menu skins in Open-Shell's format and Windows 7/11 shell layouts are separate plans (Vista through 11 `.msstyles` visual styles are now natively supported in `uxtheme`; see `docs/UXTHEME_V4.md`).

## Rounded frames and Start menu icons

The non-client renderer derives the caption shape from `GetThemeBackgroundRegion`, combines it with the rectangular body, and applies it as a window region. This exposes the desktop at Luna's transparent corners rather than filling those pixels with the button-face color. Regions update on resize/maximize/restore and clear when visual styles are disabled. Explicit application regions take precedence; clearing an application region allows the theme shape again. A recursion guard protects non-client repainting during region updates.

The XP Start panel uses the theme's user-pane region instead of a fixed-radius approximation. Start menu flyout and Classic icons use `HBMMENU_CALLBACK` to draw the original icon with its transparency over the normal/highlighted background, avoiding Wine's legacy `NOTSRCCOPY` treatment of ordinary menu bitmaps. Icon and bitmap resources are released when their menus close.

Behavior reference: `references/reactos/dll/win32/uxtheme/themehooks.c`, inspected for the theme-region approach. The implementation uses the existing Wine APIs. `scripts/themed-frame-probe.c` checks Luna corners, maximize/restore, application-defined regions, and switching to Classic on an isolated desktop. Theme-only Explorer invocations also skip desktop refresh until the shell view's grid exists, avoiding a divide-by-zero during initialization.

## Open icon packs

`scripts/build_iconpack.py MAPPING FLUENT_REPO OUT_DIR` builds a theme folder (`<Name>/<Name>.theme` with
`IconPack=`, `<Name>/Icons/xpres_shell32.dll`, licence) from a mapping in `assets/iconpacks/`. A mapping lists shell32
icon ids (Windows numbering, the same as Wine's `IDI_SHELL_*` except the ids `shellres.c` skips) with an icon and a
palette colour. Ids left out fall back to the default icons. Run it in `wexbuild` (`rsvg-convert`, `icotool`,
x86_64 MinGW) against a checkout of `microsoft/fluentui-system-icons` (`git clone --depth 1 --filter=blob:none
--sparse`, then `git sparse-checkout set assets`).

- **Fluent (Windows 11 style)** (`assets/iconpacks/fluent.json`): 69 shell32 icons built only from Fluent UI System
  Icons (MIT) glyphs. Each is layered: a filled glyph in a light colour under its outline in a darker colour, plus
  small overlay glyphs (a clock, an arrow, a globe…). The colours were matched by eye against Windows 11's icons of
  the same slots on a developer's machine. Per-icon provenance: `docs/iconpacks/fluent.md` (generated by
  `build_iconpack.py --provenance`); preview: `docs/iconpacks/fluent-preview.png`. It works on the desktop, Start menu,
  Explorer and taskbar. Special folders (My Documents and friends) and application icons are not covered yet.
  There is one set only: Windows 11 draws these shell icons the same in light and dark mode (its theme-dependent
  glyphs are elsewhere, such as the taskbar and Settings).
- Apply a pack by making its `.theme` current. `CurrentTheme` must be the full path of the `.theme` file;
  `shellres.c` reads `IconPack=` from it.
- Windows XP, Vista/7 and Classic style packs need art under a GPL-compatible licence that is not derived from Windows
  art. None was found: the packs checked so far were NonCommercial, GPL-3.0 plus Segoe glyphs, or remakes of Windows
  icons. See `docs/ASSETS.md` for the rules.
