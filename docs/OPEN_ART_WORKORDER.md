<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order (Gemini): open look-alike icons and cursors for XP and Windows 11

Status: ready, 2026-10-16. Assignee: Gemini.

## Goal

Two art sets made only from open art, each looking as close as practical to its Windows original:

- **XP set**: shell icons and the cursor scheme, looking like Windows XP (Luna era).
- **Windows 11 set**: the Fluent pack's icons improved to look closer to Windows 11, plus a Windows 11-like cursor
  scheme.

This is art only. Do not change C code, the shell, packaging or Wine. The Fluent theme's code fixes (review 1 in
`FLUENT_THEME_WORKORDER.md`) are paused while you do this. Do one thing first: delete
`docs/fluent-theme/screenshots/04_luna_reverted_start_menu.png` from `gemini/fluent-theme` in a new commit.

## The rule that matters

**Look at the Windows original, but never copy, trace or sample it.**
- Every shape comes from an allowed open source (below), edited by hand: recoloured, combined, simplified.
- Colours are matched by eye. Do not pick colours from Windows images with a tool.
- No Windows pixels, paths or outlines go into any file in this repository, not even as an intermediate step.
- Side-by-side comparisons with Windows art are made and kept in the local-only repository
  `../WineExplorer-local` (no remote; never push it). Paths to the Windows references are in
  `../WineExplorer-local/notes/open-art-references.md`. Read it before you start.
- Never name a Windows file or Windows path in this repository. Say "the Windows original" instead.

## Open sources

All except Font Awesome, WinSymbols3 and ReactOS are already checked out in `wexbuild` under `/root/iconsrc`:

| Source | Licence (verify at the commit) | Commit / location | Good for |
|---|---|---|---|
| Fluent UI System Icons (Microsoft) | MIT | `/root/iconsrc/fluentui-system-icons`, `a347af9` | Windows 11 glyphs (already used) |
| Tela (Ubuntu Budgie fork) | GPL-3.0 | `/root/iconsrc/tela`, `50f51f4` | Windows 11 folders, Recycle Bin (already used) |
| Yaru | CC BY-SA 4.0 (`LICENSE_CCBYSA`) | `/root/iconsrc/yaru`, `7f18973` | flat Windows 11 pieces |
| Oxygen (KDE) | LGPL-3.0 (check its `COPYING*`) | `/root/iconsrc/oxygen`, `e8f611e` | glossy XP-like devices, drives, folders |
| elementary-xfce | GPL-2.0 (check) | `/root/iconsrc/elementary-xfce`, `ce92f51` | XP-like folders, documents |
| ReactOS | GPL / per file | `references/reactos/dll/win32/shell32/res/icons` (236 icons), cursors in `references/reactos/win32ss/user/user32/resources/ocr_*.cur` | the closest XP-like base; may be ported (`CLAUDE.md`) |
| Font Awesome Free | icons CC BY 4.0 | github.com/FortAwesome/Font-Awesome (pin a tag) | small overlay glyphs |
| WinSymbols3.ttf (SymbolIconManager) | MIT | pin the commit | Windows 11 symbol glyphs |
| Cursors: capitaine-cursors (LGPL-3.0), Bibata (GPL-3.0), Breeze (KDE, GPL-2.0-or-later) | as stated; verify | pin each commit | cursor bases, or draw your own |

**Not allowed:**
- Anything by yeyushengfan258.
- Sets based on Windows art: YlmfOS, Win2-7 and similar.
- NC or ND licences.
- Brand logos, the Windows logo included.

**Rip check.** Run it on every new source before you use it, and record the result in `docs/ASSETS.md`. The
procedure is in `FLUENT_THEME_WORKORDER.md`:
1. Scan for `data:image` in the source's SVGs.
2. Compare renders against the Windows originals with ImageMagick `compare -metric RMSE`. A result near 0 excludes
   the source.

ReactOS art is checked too.

## What to make

**Icons.** Use `scripts/build_iconpack.py` and its JSON mapping. The layer types are `glyph`, `tone` and `svg` with
`edits`/`append`.
- `assets/iconpacks/xp.json` (new). Pack name `XP`. Its `.theme` is based on `src/resources/themes/Luna.theme`.
- `assets/iconpacks/fluent.json`: improve the existing icons so they look closer to Windows 11.
- Cover the same slots in both packs:
  - the shell32 ids already in `fluent.json`;
  - special folders: My Documents, My Pictures, My Music, My Videos, Desktop, Shared Documents and Downloads, in the
    slots Wine really uses for them (see `other_picture()` in `src/dlls/wexres/shellres.c`; do not change that list);
  - the Control Panel applets `desk`, `sysdm`, `timedate`, `main`, `intl`, `mmsys`, `ncpa` and `powercfg`;
  - the tray icons: power, network and volume;
  - the program icons: Internet Explorer, Notepad, Calculator, Paint, Command Prompt and Windows Media Player.
- Use the multi-module support from your fluent-theme branch if you need it: bring over only `build_iconpack.py`.
- Sizes 16, 24, 32, 48 and 256. The 16 and 32 px images must be hand-tuned where a plain downscale goes blurry.

**Cursors.** Write one generic builder, `scripts/build_cursors.py`, from `build_fluent_cursors.py` on your branch. It
builds a set from a JSON mapping (`assets/cursors/xp.json`, `assets/cursors/fluent.json`).
- The 15 scheme roles: Arrow, Help, AppStarting (animated), Wait (animated), Crosshair, IBeam, NWPen, No, SizeNS,
  SizeWE, SizeNWSE, SizeNESW, SizeAll, UpArrow and Hand.
- Sizes 32 and 48, correct hotspots, `.cur` and `.ani`.
- Write the theme's `[Control Panel\Cursors]` section.

**Provenance.** For every icon and cursor: the source, its commit and file, and each edit.
- Generate it with `build_iconpack.py --provenance`, and the equivalent for cursors.
- Commit it as `docs/iconpacks/xp.md`, `docs/iconpacks/fluent.md` and `docs/cursors/*.md`, with open-art previews
  (`*-preview.png`).
- Add a row to `LICENSE.md` for each new source. The XP pack is GPL-3.0 as a whole if it contains GPL-3.0 or
  LGPL-3.0 art (`LICENSE.md`).

## Acceptance

1. **Local comparison sheets.** Each slot as a pair, Windows original left and ours right, at 16, 32 and 48 px.
   - Icons: XP sheets in `../WineExplorer-local/notes/xp/`, Windows 11 sheets in `../WineExplorer-local/notes/win11/`.
   - Cursors: the same pairs, in `../WineExplorer-local/notes/xp/` and `notes/win11/`.
   - Use or extend `../WineExplorer-local/tools/compare_icons.sh`; an XP variant goes in that repository's `tools/`.
   - Commit them in that repository only.
2. **Look-alike, not copy.**
   - At 48 px, a person should name the same object and see the same silhouette, angle and main colours.
   - No pair may be a near-pixel match: RMSE against the original must stay clearly above 0. Report the lowest value
     per set.
3. **Builds.** `build_iconpack.py` and `build_cursors.py` build both sets in `wexbuild` without errors. Commit the
   raw build output as a log.
4. **Report** in `docs/OPEN_ART.md`:
   - each set's coverage table;
   - the rip-check results;
   - the lowest RMSE per set;
   - the slots you are unhappy with.

   Real output only. Look at every image before you describe it.

I review with the local sheets. Expect a second round for individual icons.

## Boundaries

- Work in `wexbuild` in `/root/WineExplorer`, on branch `gemini/open-art` from `origin/master`.
- Touch only `assets/`, `scripts/build_iconpack.py`, `scripts/build_cursors.py`, `docs/iconpacks/`, `docs/cursors/`,
  `docs/OPEN_ART.md`, `docs/ASSETS.md` and `LICENSE.md`.
- Git:
  - run it with `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never` and a `timeout`;
  - never push master, never force-push;
  - write commit messages to a file.
- `../WineExplorer-local` must never get a remote and must never be pushed.
- `CLAUDE.md` rules apply: GPL headers on new scripts; no C escapes through heredocs, `sed` or `python -c`.
