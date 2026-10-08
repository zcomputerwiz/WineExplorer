# Themes: how they are built, packaged and installed

## How Windows XP visual styles are made

| Input | Becomes |
|---|---|
| `themes.ini`: `[documentation]`, `[ColorScheme.*]`, `[Size.*]`, `[Subst.*]`, `[File.*]` | `COLORNAMES`, `SIZENAMES`, `FILERESNAMES`, `ORIGFILENAMES`, `MINDEPTH`, `COMBODATA`, display-name string tables, `THEMES_INI` |
| one class-data ini per colour scheme (`Blue\Blue.ini`) with `##Symbol` placeholders | one `TEXTFILE` per `[File.x]`: `<X>_INI`, UTF-16, placeholders replaced from the `[Subst.*]` tables of that file's colour and size |
| bitmaps named by `ImageFile =` and friends | `BITMAP` resources named after the path: `Blue\StartButton.bmp` -> `BLUE_STARTBUTTON_BMP` |

`COMBODATA` is `cColors, cSizes` followed by, per size, the index of the file serving each colour.
Wine's uxtheme reads exactly this layout (`dlls/uxtheme/msstyles.c`), and an `.msstyles` built from the source loads in it.

## Tools
```
```
A package is a zip: `theme.json`, `<Name>.theme`, `<Name>/<name>.msstyles`, `<Name>/Icons/xpres_*.dll`.
Packages and builds live in `build/` (gitignored): nothing Microsoft-derived is committed.

## Icons and bitmaps belong to the theme
`<Name>.theme` gets a section
```
[WineExplorer]
IconPack=%ResourceDir%\Themes\<Name>\Icons
```
The shell (`shellres.c`) loads `xpres_<module>.dll` from the active theme's folder first, then from
`%WinDir%\Resources\Icons`, then falls back to Wine's shell32 and the bitmaps built into explorer.exe (ReactOS). Switching
(see `docs/ASSETS.md`).

## Not covered yet
- `Luna\Shell\<scheme>\shellstyle.dll` (Start panel pictures) still comes from the XP export.
- Cursors, sounds and wallpaper are applied from the `.theme` file but not yet packaged.
