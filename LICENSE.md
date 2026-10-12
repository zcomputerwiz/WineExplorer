# License

WineExplorer as a whole is distributed under the **GNU General Public License, version 2 or (at your option) any
later version** (GPL-2.0-or-later). The full text is in [`COPYING`](COPYING).

The tree combines code under several compatible licenses. Every file keeps the notice it came with:

| Code | License | Notes |
|---|---|---|
| Wine (`wine/`, everything not listed below) | LGPL-2.1-or-later (`wine/LICENSE`, `wine/COPYING.LIB`) | Unmodified and modified upstream files keep Wine's header. The LGPL (section 3) allows these copies to be distributed under the GPL, which is what happens where they are combined with GPL code. |
| Code ported from ReactOS (for example `wine/programs/wmplayer`, `wine/programs/explorer/alttab.c`) | GPL-2.0-or-later, as stated in each file | Kept with its original header and copyright lines. |
| ReactOS code under the LGPL (for example `wine/programs/calc`) | LGPL-2.1-or-later, as stated in each file | |
| ReactOS art (icons and bitmaps, listed in `assets/`) | as in the ReactOS tree (GPL, or the license noted next to the art, such as Tango's) | |
| Project code written for WineExplorer | GPL-2.0-or-later for new files (`scripts/add-license.py` adds the header). Older project files carry an LGPL-2.1-or-later header, which is compatible and may stay. | |

A module that contains GPL code (for example `explorer.exe`, because of `alttab.c`) is distributed under the GPL as a
whole. Code under licenses that are not compatible with GPLv2 (Apache-2.0, GPL-3.0-only) must not be added to the
same module.

The project does not aim to contribute to Wine or ReactOS. Anyone may port parts of it back under their own terms,
provided they respect the licenses above. Note that Wine only accepts LGPL code, so files containing GPL code cannot
go to Wine.

## Not part of this repository

Microsoft material (genuine Windows XP resources, fonts and the reference sources used for comparison) is not
distributed with WineExplorer and is never committed to this repository. It is kept on the developers' machines only.
WineExplorer builds and runs with the open replacements; the genuine files are optional, local-only extras.
