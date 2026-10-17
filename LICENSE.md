# License

WineExplorer as a whole is distributed under the **GNU General Public License, version 2 or (at your option) any
later version** (GPL-2.0-or-later). The full text is in [`COPYING`](COPYING).

The tree combines code under several compatible licenses. Every file keeps the notice it came with:

| Code | License | Notes |
|---|---|---|
| Wine (`wine/`, everything not listed below) | LGPL-2.1-or-later (`wine/LICENSE`, `wine/COPYING.LIB`) | Unmodified and modified upstream files keep Wine's header. The LGPL (section 3) allows these copies to be distributed under the GPL, which is what happens where they are combined with GPL code. |
| Code ported from ReactOS (for example `wine/programs/wmplayer`, `wine/programs/explorer/alttab.c`) | GPL-2.0-or-later, as stated in each file | Kept with its original header and copyright lines. |
| ReactOS code under the LGPL (for example `wine/programs/calc`) | LGPL-2.1-or-later, as stated in each file | |
| ReactOS art (icons and bitmaps, listed in `assets/`, including `assets/iconpacks/xp.json` and `assets/cursors/xp.json`) | as in the ReactOS tree (GPL-2.0-or-later, or the license noted next to the art, such as Tango's) | Used for shell32, CPL modules, and cursor scheme in the XP pack. |
| Fluent UI System Icons (Microsoft), the glyphs of the Fluent icon and cursor packs (`assets/iconpacks/fluent.json`, `assets/cursors/fluent.json`, built by `scripts/build_iconpack.py` and `scripts/build_cursors.py`) | MIT, Copyright (c) 2020 Microsoft Corporation | The licence text ships beside the pack (`Icons/LICENSE-fluent`). Tinted and rendered here. |
| Tela icon theme (Ubuntu Budgie fork of vinceliuice/Tela-icon-theme), the Recycle Bin of the Fluent icon pack | GPL-3.0 | Edited as listed in `docs/iconpacks/fluent.md`; the licence text ships beside the pack (`Icons/LICENSE-tela`). Because of it the built Fluent pack is GPL-3.0 as a whole. |
| Font Awesome Free 7.3.1 (Fonticons, Inc.), the Americas earth used in the globes of the XP and Fluent icon packs (`assets/iconpacks/src/fontawesome/`) | CC BY 4.0 | Recoloured and scaled; attribution in `assets/iconpacks/src/fontawesome/LICENSE.txt` and in the packs' provenance (`docs/iconpacks/`). |
| Project code written for WineExplorer | GPL-2.0-or-later for new files (`scripts/add-license.py` adds the header). Older project files carry an LGPL-2.1-or-later header, which is compatible and may stay. | |
| Code and skins ported from Open-Shell (`src/dlls/wexshell/startskin.*`, `src/resources/startskins/`) | MIT (`src/resources/startskins/LICENSE`) | Copyright (c) 2009-2017 Ivo Beltchev, Copyright (c) 2017-2018 Open-Shell Team. Compatible with GPL-2.0-or-later. |
| Selawik font (Microsoft), Segoe UI open metric-compatible substitute (`/usr/local/share/fonts/selawik/`) | SIL OFL 1.1 (`/usr/local/share/fonts/selawik/LICENSE.txt`) | Copyright (c) 2015 Microsoft Corporation. Open font replacement for Segoe UI. |
| Wine Gecko 2.47.4 (`/usr/local/share/wine/gecko/`) | MPL-2.0 (`/usr/local/share/wine/gecko/LICENSE`) | Mozilla Corporation and Wine project. Offline MSHTML engine bundled in ISO. |
| Wine Mono 11.3.0 (`/usr/local/share/wine/mono/`) | MIT, LGPL-2.0, GPL-2.0 (`/usr/local/share/wine/mono/LICENSE`) | Novell, Inc., Vincent Povirk, et al. Offline .NET runtime engine bundled in ISO. |
| DXVK 2.4.1 (`/usr/local/share/wineexplorer/dxvk/`) | zlib (`/usr/local/share/wineexplorer/dxvk/LICENSE.dxvk`) | Copyright (c) 2017-2024 Philip Rebohle, et al. Opt-in DirectX 8-11 to Vulkan layer. |
| vkd3d-proton 2.13 (`/usr/local/share/wineexplorer/dxvk/`) | LGPL-2.1 (`/usr/local/share/wineexplorer/dxvk/LICENSE.vkd3d`) | Copyright (c) 2018-2024 Hans-Kristian Arntzen and contributors. Opt-in DirectX 12 to Vulkan layer. |

A module that contains GPL code (for example `explorer.exe`, because of `alttab.c`) is distributed under the GPL as a
whole. GPL-2.0-or-later material may be combined with GPL-3.0, LGPL-3.0 or Apache-2.0 material; the combined module is
then distributed under GPL-3.0. Program modules avoid that so their code can still be shared with Wine and ReactOS.
Icon packs (`xpres_*.dll`, art only, loaded as data) are separate works and may contain GPL-3.0 art; such a pack is
GPL-3.0 as a whole, and a `LICENSE` file inside the pack says so.

The project does not aim to contribute to Wine or ReactOS. Anyone may port parts of it back under their own terms,
provided they respect the licenses above. Note that Wine only accepts LGPL code, so files containing GPL code cannot
go to Wine.

## Not part of this repository

Microsoft material (genuine Windows XP resources, fonts and the reference sources used for comparison) is not
distributed with WineExplorer and is never committed to this repository. It is kept on the developers' machines only.
WineExplorer builds and runs with the open replacements; the genuine files are optional, local-only extras.
