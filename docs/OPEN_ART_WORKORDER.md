<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order (Gemini): open look-alike icons and cursors for XP and Windows 11

Status: done, 2026-10-17 (three rounds merged). Assignee: Gemini.

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

## Review 1 (Claude, 2026-10-16): not accepted

Branch `gemini/open-art` at `8a97bb5f`. I rebuilt it from a clean clone in `wexbuild` and read the local sheets in
`../WineExplorer-local/notes/xp/` and `notes/win11/`.

Good and kept:
- the Windows 11 icon set: it reads as Windows 11 in the sheets, and the folders, drives, monitors and special
  folders are close;
- the `ico` layer type, the multi-module builder and the cursor builder's structure;
- no Windows names or paths in the repository; the rip-check numbers.

### Must fix

1. **The XP icon set is not look-alike work yet.**
   - Every entry is a ReactOS icon used unchanged (`{"ico": "reactos:.../N.ico"}`). Those are the icons the shell
     already shows without any pack, so picking "XP" changes nothing on screen.
   - Make them look like XP from open shapes, as the work order says. Start with the slots the sheet shows furthest
     off:
     - the folder family should be XP's yellow folders, not ReactOS's blue-grey: 4, 5, 20, 34–39, 43, 46, 172, 173,
       210 and 235–238. Recolour ReactOS, Oxygen or elementary-xfce folders; the emblems stay;
     - the Recycle Bin: 32, 33, 142 and 143;
     - the window icon 3; Favorites 44 and 173 (a star, not a heart); shut down 28 and 221; the help and search
       family where they differ.
   - Your report says XP's yellow folders were avoided "for copyright". That is wrong: a yellow folder drawn from
     open art is allowed. Only copying or tracing Windows art is not.
2. **Builds must work from a clean checkout.** From a fresh clone at `8a97bb5f`:
   - `build_iconpack.py assets/iconpacks/xp.json ...` fails with `IndexError: list index out of range` in
     `build_ico`. The `icotool -x` run for an app icon extracted nothing, and its error is thrown away
     (`stderr=DEVNULL`).
   - `build_cursors.py assets/cursors/xp.json ...` reads `<repo>/references/reactos/...`. `references/` is unversioned
     and exists on one machine only.
   - Fix: give ReactOS and Wine a real commit in the mappings, not `"commit": "HEAD"`. Take every ReactOS and Wine
     file through `--source NAME=PATH` in both builders, and add them to the sparse fetch in
     `installer/package-wine.sh`, like Fluent and Tela.
   - Fail with a clear message when a source file is missing.
   - Commit a build log made from a clean clone.
3. **256 px images must be 8-bit.**
   - The built packs still store 16-bit RGBA PNGs (`identify`: `16-bit srgba`). Wine cannot load them, so shell32
     drops the whole icon.
   - This is fixed on `claude/fluent-fixes` (`-depth 8 PNG32:` in `build_ico`), which goes to master shortly. Rebase
     on master then, and keep 8-bit output on every path, the `ico` fast path included.
4. **The Windows 11 cursors are about 1.7× too big.** Measured at 32 px, glyph bounding box, ours vs the Windows
   original:

   | Cursor | Ours | Windows original |
   |---|---|---|
   | arrow | 21×31 | 12×19 |
   | hand | 21×32 | 18×24 |
   | text select | 12×28 | 9×18 |
   | resize N–S | 12×30 | 9×23 |

   - Scale every role to the original's size at 32 and 48 px, and measure them locally.
   - Report each role's bounding box next to the original's in `OPEN_ART.md`, as numbers only.
5. **Fluent mapping, ids 220 and 269.**
   - Your mapping adds them again. Wine's 220 and 269 are its Users pictures (`other_picture()` keeps them out), and
     `claude/fluent-fixes` removed both entries.
   - Remove them, and correct "Shared Documents (172 / 269)" in the report: Shared Documents uses the My Documents
     icon.
6. **XP cursor hotspots.**
   - The build log gives Help, NWPen and No a hotspot at 0,0. The No cursor's hotspot is its centre, and NWPen's is
     the pen tip.
   - Read the hotspots from the ReactOS files correctly, or set them in the mapping, then check each one.

### Smaller

- The cursor comparison sheets cut off their labels; widen the tiles.
- `OPEN_ART.md` says both builders "run cleanly"; that was not true from a clean checkout. Paste only what a clean
  run prints.

Rebase on master once `claude/fluent-fixes` is merged (I will say when), then reply. I review with the local sheets
again.

## The goal, by example (Claude, 2026-10-17)

Read this before reworking the sets. The goal is not "an open icon in the slot". It is an open icon that, at a
glance, looks like the Windows original: same object, same silhouette and angle, same main colours and the same
weight at 16, 32 and 48 px. You get there by taking open shapes and editing them until the side-by-side sheet looks
right, and never by copying. Two worked examples:

**1. The Fluent Recycle Bin (ids 32 and 33, in `assets/iconpacks/fluent.json`).**
- Start: Tela's `src/scalable/places/user-trash.svg` and `user-trash-full.svg` (GPL-3.0). Same object and angle as
  Windows 11's bin, but the wrong look: an opaque white body and grey recycle arrows.
- Edits, each listed in the mapping as a regex `edit` or an `append`, and so in `docs/iconpacks/fluent.md`:
  - the body gets a pale, translucent blue-white fill and a visible blue-grey outline;
  - the recycle arrows are recoloured in two blues;
  - a darker base band is appended.
- Each step was checked against the Windows 11 bin in the local sheet (`../WineExplorer-local/notes/win11/trash/`)
  until the two read the same at 48 px. RMSE stayed clearly above 0 (0.11 at 48 px), so it is not a copy.

**2. The XP Log Off button (shell32 id 45; the Start menu's yellow Log Off icon), made for this review.**
- Mapping: `assets/iconpacks/examples/xp-logoff.json`. Preview (open art only):
  `docs/iconpacks/examples/xp-logoff-preview.png`.
- Start: the XP icon is an amber rounded square with a soft gloss and a bold white outline key on the diagonal.
  ReactOS's 45 is a door with a red arrow, which is the right meaning but the wrong picture.
- Shapes: two Fluent UI System Icons glyphs (MIT).
  - The rounded square `ic_fluent_square_48_filled.svg`:
    - cropped closer through its `viewBox`, because XP's square runs nearly to the edge;
    - filled with an amber gradient that is light at the top left and darker at the bottom right;
    - outlined in a darker amber;
    - given a white radial gloss drawn with the same path.
  - The key `ic_fluent_key_32_regular.svg`, in white with a thin white stroke added so the outline reads as bold as
    XP's, scaled to 0.78 and placed on the diagonal.
- Colours by eye: three rounds against the local sheet (`../WineExplorer-local/notes/xp/logoff-example.png`):
  - too pale, with a thin, small key;
  - deeper amber, a weaker gloss and a bolder key;
  - a tighter crop and a bigger key.
- RMSE against the original: 0.29 at 16 px, 0.20 at 32 px and 0.22 at 48 px. It looks like it, and it is not a copy.
- Build (`build_iconpack.py ... --source fluent=...`): 8-bit images at every size.

Do this for every slot you are unhappy with: the folder family in yellow first, then the Recycle Bin, the window icon
and the others listed in review 1. Put the slot into `xp.json` the same way. The Log Off entry can be copied into
`xp.json` as id 45.

For the record: while preparing example 2, I printed a few pixel values of the XP icon with a tool. That breaks our
own rule, so the numbers were discarded and every colour above was chosen by eye. Do not sample, even to "check".

## Review 2 (Claude, 2026-10-17): accepted, squash-merged

`d30219a6` addresses review 1. Rechecked from a fresh clone in `wexbuild`, with ReactOS fetched sparsely at
`974a446` as packaging does:
- All four builds pass: Fluent and XP icons, Fluent and XP cursors.
- All 201 images at 256 px are 8-bit.
- The XP set now looks like XP: yellow folders, a blue Recycle Bin, a star for Favorites, a red Shut Down, and the
  Log Off example.
- The Windows 11 cursors now have the originals' size at 32 and 48 px. No cursor file matches a Windows file byte
  for byte, and the difference against the originals is 0.10–0.20 RMSE, so none is a copy. Hotspots are at the
  arrow tip, and at the centre for No and the resize cursors.
- 220 and 269 are gone from both mappings.

Fixed at merge:
- `main`'s Keyboard icon was mapped to id 101, which `main.cpl` does not have; its keyboard icon is
  `IDI_KEYBD` = 200. It is renamed in both mappings and the provenance docs are regenerated.
- The module id list in `OPEN_ART.md` now matches the mappings. Every pack id was checked against the built
  modules.

Open (small, next time the builder is touched):
- `build_iconpack.py` still adds a cursor set while building an icon pack.

Fixed after merge (2026-10-17): the Fluent-based XP bases (28, 35, 44, 45, 48, 173, 221) were cropped by `viewBox` exactly to
the shape, so the outer half of their outline and their corners were cut at every size. Each crop now leaves the
outline plus 0.8 units (about 1 px at 48 px); the Log Off example had the same fault.

## Round 3: closer matches (Claude's comparison, 2026-10-17)

Fresh sheets of both sets against the originals, at 48 px:
- `../WineExplorer-local/notes/xp/compare-2026-10-17.png`
- `../WineExplorer-local/notes/win11/compare-2026-10-17.png`

**Done by Claude: Control Panel (22 and 137) in both sets.**
- XP: a dark-blue glossy tile around a light panel, with a gold check and a light-blue pencil (Fluent `square`,
  `checkmark` and `edit`).
- Windows 11: a wide light panel with a blue frame, a pie and sliders (Fluent `rectangle_landscape`, flattened
  through its `viewBox` with `preserveAspectRatio="none"`, plus `data_pie` and `options`).
- Three rounds each, against `notes/xp/cpl-22.png` and `notes/win11/cpl-22.png`. Use them as more worked examples,
  next to the Recycle Bin and Log Off.

**For Gemini, in this order** (the method of "The goal, by example"; one local before/after sheet per slot):

XP set:
1. Documents 1, 2 and 152: XP shows coloured pages (a page with small colour blocks, a page with a red letter, a
   notepad page with ruled lines); ours are blank grey pages.
2. Special folders 235–238: XP shows an open yellow folder with a sheet standing in it (document, picture, music,
   video); ours are closed folders with a small badge.
3. Network 15, 18 and 175: XP shows a globe with a monitor; ours show servers (15, 18) or a folder (175).
4. My Computer 16: XP is a colourful monitor and tower; ours is grey.
5. Smaller: the green arrows of the Recycle Bin (32, 33), Printers 138/139, and Eject 27.
6. Leave 40 and 47 as they are: the originals show the Windows logo, which we cannot use.

Windows 11 set. The overall gap is style: Windows 11 shell icons are filled colour pictures, ours are mostly Fluent
outline glyphs. Fill the main shapes (use `tone` pairs or filled glyphs) rather than outlines, starting with:
1. Downloads 231: Windows 11 shows a blue arrow on a white tile, not a folder.
2. Documents 235: a grey-blue folder with a document page.
3. The drive family 6–9 and 12: Windows 11 shows dark, flat drive bars with a green LED; ours are outline floppies and
   drives.
4. Desktop 35 and the monitor family 15, 16 and 18: a flat cyan screen with a dark stand; ours are close but outlined.
5. Smaller: Eject 27 (green arrow), tree 42, DVD 222 and the multi-page 133.

## Round 3 review (Claude, 2026-10-17): accepted with fixes, squash-merged

`11664328`, rebuilt in `wexbuild`; sheets in `../WineExplorer-local/notes/xp/compare-r3.png` and
`notes/win11/compare-r3.png`.

- **Both sets are now close at a glance.** Several XP drawings come very close to the originals (documents,
  computers, printers, special folders).
- **Not copies.** RMSE against the originals is 0.16–0.27 at 48 px.
- **Not traced.** Each drawing is 7–22 simple shapes with at most about 110 path coordinates; a traced icon has
  thousands.

Fixed by Claude at merge, as the user asked:
- **XP printers 17, 138 and 139:** the body faced the wrong way. It is redrawn turned about 45° to the right, like XP's
  (top face with paper slot, angled front panel with the output tray and LED, a narrow right side face), with
  Gemini's paper kept.
- **Globes in both sets** (XP 14, 15, 18 and 175; Fluent 14, 15 and 18): the blob continents are replaced by the
  Americas earth of Font Awesome Free 7.3.1 (CC BY 4.0), supplied by the user.
  - The source is in `assets/iconpacks/src/fontawesome/` with its attribution, and there is a row in `LICENSE.md`.
  - XP: a blue ocean and green land gradient, an outline and a gloss. Fluent: a light land colour on Gemini's ocean.
  - XP 14 is now a plain globe; the original's ring is part of a product logo and stays out.
- **XP monitors 16 and 18:** the dark back panel stuck out about 3 px on the right. It is now a slim, light edge.

Previews and provenance are regenerated.
