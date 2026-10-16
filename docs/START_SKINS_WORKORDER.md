<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order (Gemini): Open-Shell skins for our Start menu

Status: ready, 2026-10-16. Assignee: Gemini.

## Goal

Our Start menu (`src/dlls/wexshell/startmenu*.c`: Classic single column and the XP two-column menu) can be drawn
from an **Open-Shell Start menu skin**. Users pick a skin in Start menu properties, and skins made for Open-Shell
(the big community collection) work without conversion. Our built-in Classic and XP menus stay the default and look
exactly as today when no skin is chosen.

Background: Open-Shell does not attach to our taskbar (it hooks Windows 7+ taskbar internals), so we read its skin
format ourselves (`docs/THEMES.md`). Open-Shell is MIT-licensed: its code may be ported with its copyright and
permission notice kept (`LICENSE.md`, `CREDITS.md`). Source: the fork `zcomputerwiz/Open-Shell-Menu`
(`Src/StartMenu/StartMenuDLL/SkinManager.cpp` and `.h` parse skins; `Src/Skins/*` are the bundled skins;
`Src/Localization/English/SkinTutorial.html` documents the format).

## Phases (one review at the end; commit and push your branch after each phase)

**K0 Map the format.** Write `docs/START_SKINS.md`:
- the skin file (a resource DLL: `SKIN` text with the skin description, bitmaps, `.skin` for the classic menu,
  `.skin7` for the Windows 7 style);
- the description grammar (variations, options, conditions, DPI variants such as `150`);
- each property mapped to our layout code: main/second column, banner, user picture, search box, selection,
  separators, arrows, pager, fonts, colours, glass/opacity, padding.

Mark which properties have no counterpart, and decide how each degrades (ignore, approximate).

**K1 Loader.** Port the skin parser into `src/dlls/wexshell` (new `startskin.c`, or C++ if a straight port is
cleaner; Wine's MinGW build supports C++). Load from a skin DLL with `LoadLibraryEx(LOAD_LIBRARY_AS_DATAFILE)`.
Resolve variations, options and DPI variants, and keep the result in one structure the views draw from. Malformed
or truncated skins must fail cleanly and fall back to the built-in style (fuzz test: truncated file, broken
description, missing bitmap).

**K2 Drawing.** The Classic view and the two-column view draw from a loaded skin, in an order that gets a typical
skin right:
1. main and second column background bitmaps (nine-slice/stretch as specified);
2. selection and separators;
3. text colours and fonts;
4. banner/caption and user picture;
5. arrows and pager.

Glass/blur becomes opaque (no compositor effects in X11 sessions). A `.skin7` skin may drive the two-column view.
Exact Windows 7 layout features (jump lists, search results pane) are out of scope; note them.

**K3 Settings.** Start menu properties get a skin chooser for skins found in
`/usr/local/share/wineexplorer/startskins` and the user's `%APPDATA%\WineExplorer\StartSkins`. It shows the skin's
options (checkboxes and lists from the description) and a preview. Store the choice under
`HKCU\Software\WineExplorer\StartMenu`. "None" means the built-in menu.

**K4 Bundled skins.** Build the bundled skins from Open-Shell's sources (`Src/Skins/*`; MIT) and package them.
Bundle only skins whose art is clearly Open-Shell's own. List each skin's bitmaps and judge them against
`docs/ASSETS.md`; skins that reproduce Microsoft artwork (likely candidates: `WinXP`, `Win7Aero*`, `Win8*`) are not
bundled and not committed. Users can still add them, or any downloaded skin, to their own skins folder.

## Acceptance (report in `docs/START_SKINS.md`)

- With no skin, both built-in menus are pixel-identical to before (Xvfb screenshot diff, as for uxtheme).
- At least three bundled skins render correctly in both views at 100% and 150%, with screenshots (open art only).
- One third-party skin from the Open-Shell community loads and renders locally. Say which; keep it out of the
  repository unless its licence allows bundling.
- The fuzz cases fall back without a crash.
- The interactive install test still passes on a fresh ISO.
- Paste real output only and commit raw logs, as before.

## Boundaries

- Codex is building the Wayland session and tray icons (`docs/WAYLAND_SESSION_WORKORDER.md`). That includes Start menu
  entries for Linux programs, which live in `startmenu_view.c` (`populate_linux_programs`, `launch_linux_program`).
  Keep your changes out of those functions; put skin code in new files and keep edits to the existing view code
  small. Pull master often and rebase.
- Work in `wexbuild` on branch `gemini/start-skins` from `origin/master`. Git commands use
  `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never` and a `timeout`. Never push `master` or force-push; write commit
  messages to a file.
- `CLAUDE.md` rules: GPL headers on new files (keep Open-Shell's MIT notice on ported code), WoW64 builds, no
  Microsoft material, never pass C escapes through heredocs/`sed`/`python -c`.

## Review (Claude, 2026-10-16): changes required

Reviewed `gemini/start-skins` `de1dfe80`. The loader, the skin chooser and the integration into the real menu
exist. On the real shell (`explorer /desktop=Shell /shell` in `wexbuild`, Start button clicked):
- with no skin, the menu matches master except for the taskbar clock;
- Metallic7 visibly skins the two-column menu.

Not accepted yet:

1. **The acceptance evidence is not the product.** `installer/tests/test_startskin_acceptance.c` draws its own mock
   menus (`FillRect` plus hard-coded strings such as "Wine User" and "Internet Explorer") and only calls the
   `startskin_*` paint helpers. Every screenshot in `docs/start-skins/screenshots/`, and the "pixel-identical to
   pre-series rendering" claim for the built-in menus, come from that mock, not from `startmenu_view.c`. Replace them
   with screenshots of the real Start menu opened in the real shell, in both views. The baseline comparison must be
   real menu against real menu (master against branch), as checked above.
2. **Skinned two-column menu: the user header and the Log Off / Turn Off Computer footer are unskinned and white,
   and the button labels are missing** (seen with Metallic7). Draw these areas from the skin (`User_*`, the shutdown
   button properties) or keep the built-in look there. The labels must always be drawn.
3. **A skin of the wrong type is applied to the wrong view.** With `Skin=ClassicSkin` (a `.skin` for the
   single-column menu) the two-column menu is drawn white with missing labels. A view must only use a skin made for
   it; otherwise it falls back to the built-in look. Let the chooser show which view a skin is for.
4. **The "third-party community skin" was SmokedGlass, which is one of Open-Shell's own bundled skins.** Test one
   from outside the Open-Shell repository, kept local, and name its source.
5. **Credits.** The report says Open-Shell's notice is kept in `LICENSE.md` and `CREDITS.md`; neither mentions
   Open-Shell. Add Open-Shell (MIT, Ivo Beltchev and the Open-Shell Team) to the table in `LICENSE.md` (there is no `CREDITS.md`) for the ported code and the
   bundled skins. Put the full MIT text next to the bundled skins (`src/resources/startskins/LICENSE`). Keep
   `scripts/build_bundled_skins.sh` able to rebuild the committed `.skin`/`.skin7` files from the named Open-Shell
   commit.

Fix on `gemini/start-skins`. The acceptance screenshots must come from the real shell, at 100% and 150%. Rerun the
install test, then stop for review.
