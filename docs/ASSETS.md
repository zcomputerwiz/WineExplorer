# Pictures, fonts and the genuine Windows XP files

The UI is designed against Windows XP, compared side by side with an XP VM. This repository contains **only open
material**: Wine's and ReactOS's art, and art drawn for the project. Microsoft material (genuine XP icons, bitmaps,
sounds, fonts, themes, and the reference sources used for comparison) never enters this repository. It lives on the
developers' machines only, in a separate local-only repository (`WineExplorer-local`, next to this checkout, never
pushed anywhere), together with the tools that work with it.

## Open pictures
`scripts/open_assets.py build` writes every picture that stands in for an XP one, and lists each with its source in
`assets/open_assets.json`: a ReactOS file (`references/reactos`, which anyone can clone), a Wine file, or one drawn by
the script.

```
python scripts/open_assets.py build   # (re)write the files under wine/ and assets/open_assets.json
python scripts/open_assets.py list    # the table
```

## Local builds with the genuine files
`scripts/vm.sh sync` looks for the local-only repository (`../WineExplorer-local`, or `$WEX_LOCAL`). When it exists, the
genuine files in its `ms-assets/wine/` replace their open counterparts in the copy sent to the build VM, so local builds
look like Windows XP while this repository holds open art only. Nothing is written into this checkout.

Optional runtime extras (resource packs `xpres_*.dll`, themes, cursors, sounds, the Search Companion's pictures) are
also built and installed by the local-only tools: `vm.sh resources xp` / `vm.sh resources open`. Explorer uses them
when present and falls back to the open pictures otherwise; resource packs also feed shell32 (Explorer points the
`Shell Icons` key at the theme's `xpres_shell32.dll`).

## Art still wanted
These have a neutral placeholder (or an interim Wine logo) until someone draws them. The licence must be compatible
with GPL-2.0-or-later (original work, public domain, CC0, MIT/BSD, or GPL). Tango (public domain) has a few close
matches, for example `network-wireless`.

| File | What it shows | Now |
|---|---|---|
| `wine/dlls/devmgr/icons/21.ico` | Device Manager class icon: IEEE 1394 Bus host controllers, 16/32 px | placeholder |
| `wine/dlls/devmgr/icons/22.ico` | Device Manager class icon: Infrared devices and Modems, 16/32 px | placeholder |
| `wine/dlls/devmgr/icons/25.ico` | Device Manager class icon: Smart card readers, 16/32 px | placeholder |
| `wine/dlls/sysdm.cpl/windows.bmp` | the product logo on System Properties > General, 118 x 113 | placeholder |
| `wine/programs/explorer/net_wireless.ico` | wireless network adapter (antenna), 16/32/48 px | placeholder |
| `wine/programs/explorer/rover.bmp` | Search Companion still character (shown when no animated character is installed), 56 x 68, magenta background | placeholder |
| `wine/programs/explorer/start.ico` | Start button logo, 16/24/32/48 px | Wine's logo |
| `wine/programs/explorer/startflag.bmp` | the same logo as a 25 x 20 bitmap on black (the Start button draws it) | Wine's logo |
| `wine/programs/explorer/res_power/flag.bmp` | the same logo, 48 x 40, on the title band blue (0,52,166) of Turn off computer / Log Off Windows | Wine's logo |
| `wine/programs/logonui/logo.bmp` | the Welcome screen logo, 137 x 86, on the panel blue (90,126,220), left of "To begin, click your user name" | Wine's logo |
| `wine/programs/explorer/wzcui.bmp` | wireless network list strip, 8 images of 16 x 16: checkbox off, checkbox on, access point, access point preferred (blue ring), access point unavailable (red X), ad hoc, ad hoc preferred, ad hoc unavailable | placeholder |
| `wine/dlls/main.cpl/mouse.bmp` | Mouse Properties > Buttons picture, 83 x 80 on cyan; the code frames the left and right button at fixed offsets (8 px in, 16 px tall) | ReactOS mouse icon, scaled |

When one is drawn, put it under `assets/art/`, point its row in `scripts/open_assets.py` at it
(`copy` with a path), and run the build.

Not pictures, but also modeled on XP and written by hand: the `.theme` files in `wine/resources/themes` (colours and
icon indexes), dialog layouts and UI wording in the `.rc` files, and colour values and metrics in the drawing code.

## Fonts
`assets/font_substitutes.json` is the list of Microsoft fonts the UI expects, with the open replacement, match quality
(`metric` / `approx` / `builtin` / `none`), license and Debian package for each. The genuine files are local only.

```
python scripts/fonts.py list          # table + whether each genuine file is in the local export
python scripts/fonts.py reg           # .reg mapping MS families to the open fonts (HKCU\Software\Wine\Fonts\Replacements)
scripts/vm.sh fonts xp                # copy the genuine fonts into the VM prefix (Wine picks up windows\Fonts)
scripts/vm.sh fonts open              # remove them again and import the replacement mapping
```
The open substitution table is fontconfig's `30-metric-aliases.conf` (Liberation = Arial/Times New Roman/Courier New,
Carlito = Calibri, Caladea = Cambria; all metric-compatible). It has no entry for Tahoma, Verdana, Trebuchet MS or
Segoe UI: Tahoma and Marlett come from `wine/fonts` (Wine's own LGPL fonts), Verdana/Trebuchet fall back to DejaVu
(approximate widths) and Segoe UI maps to Selawik (MIT, `github.com/microsoft/Selawik`, install by hand).
Not yet done: Luna's caption font (Trebuchet MS 10 bold) only applies once the theme engine sets the non-client metrics
from the msstyles `[SysMetrics]` when a theme is applied.
