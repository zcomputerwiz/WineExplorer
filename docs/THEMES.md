# Themes: how they are built, packaged and installed

## Visual styles (.msstyles)
A visual style is a resource-only DLL. Wine's uxtheme reads the layout Windows XP uses (`dlls/uxtheme/msstyles.c`):

| Resource | Holds |
|---|---|
| `THEMES_INI`, `COLORNAMES`, `SIZENAMES`, `FILERESNAMES`, `ORIGFILENAMES`, `MINDEPTH`, `COMBODATA` | the style's colour schemes, sizes, and which class-data file serves each combination |
| one `TEXTFILE` per colour/size combination (`<X>_INI`, UTF-16) | the class data: parts, states and their properties |
| `BITMAP` resources named after the image path (`Blue\StartButton.bmp` -> `BLUE_STARTBUTTON_BMP`) | the images the class data names |

`COMBODATA` is `cColors, cSizes` followed by, per size, the index of the file serving each colour.

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

## Rounded frames and Start menu icons

The non-client renderer derives the caption shape from `GetThemeBackgroundRegion`, combines it with the rectangular body, and applies it as a window region. This exposes the desktop at Luna's transparent corners rather than filling those pixels with the button-face color. Regions update on resize/maximize/restore and clear when visual styles are disabled. Explicit application regions take precedence; clearing an application region allows the theme shape again. A recursion guard protects non-client repainting during region updates.

The XP Start panel uses the theme's user-pane region instead of a fixed-radius approximation. Start menu flyout and Classic icons use `HBMMENU_CALLBACK` to draw the original icon with its transparency over the normal/highlighted background, avoiding Wine's legacy `NOTSRCCOPY` treatment of ordinary menu bitmaps. Icon and bitmap resources are released when their menus close.

Behavior reference: `references/reactos/dll/win32/uxtheme/themehooks.c`, inspected for the theme-region approach. The implementation uses the existing Wine APIs. `scripts/themed-frame-probe.c` checks Luna corners, maximize/restore, application-defined regions, and switching to Classic on an isolated desktop. Theme-only Explorer invocations also skip desktop refresh until the shell view's grid exists, avoiding a divide-by-zero during initialization.
