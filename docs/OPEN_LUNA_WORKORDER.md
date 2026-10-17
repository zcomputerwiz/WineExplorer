<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order (Gemini): an open Luna visual style, built from ReactOS Lunar

Status: round 1 merged 2026-10-17; round 2 (details) open. Assignee: Gemini.

## Goal

An open `Luna.msstyles` that looks and feels like Windows XP's Luna (blue) at a glance: the same colours and
gradients, button shapes, caption glyphs, metrics and scale. It is built from source in this repository and shipped on
the ISO.

Today the installed system has no Luna, because the genuine file is local-only, so users get Wine's own light style.
This work order is about the look, so it changes no theme-engine code.

Start from **ReactOS Lunar** (`references/reactos/media/themes/Lunar/lunar.msstyles/`, ReactOS project, GPL as in the
ReactOS tree). It is already a Windows visual style:
- `lunar.rc`;
- 183 bitmaps in `bitmaps/`;
- the class/part/state metrics in `textfiles/NormalReactOS.INI`, `LargeFontsReactOS.INI` and `ExtraLargeReactOS.INI`.

Our uxtheme already draws it pixel-identically to the reference engine (`docs/UXTHEME_V4.md`). So the work is mostly
editing a working style towards Luna.

## The rule that matters (same as `OPEN_ART_WORKORDER.md`)

- Look at the genuine Luna, but never copy, trace or sample it.
- Shapes come from Lunar or are drawn by you; colours are matched by eye.
- No Windows pixels, bitmaps, INI text or sampled colours go into this repository. That includes metric values copied
  from the genuine style's INI: set each value by comparing screenshots side by side, not by reading Luna's INI.
- Side-by-side sheets with the genuine Luna stay in `../WineExplorer-local/notes/luna/` (commit them there only).
  Reference paths are in `../WineExplorer-local/notes/open-art-references.md`, section "Luna".
- Never name Windows files or paths in this repository.

## Phases (one review at the end; commit and push your branch after each phase)

**L1. Rip check of Lunar.**
- Compare every Lunar bitmap against the genuine Luna bitmaps, locally.
- Bitmap names differ between the two, so match by content: for each Lunar bitmap, find the genuine bitmap with the
  same size and the lowest RMSE.
- Any pair near 0 means that Lunar bitmap is a copy and must be redrawn, not just recoloured.
- Record the result in `docs/ASSETS.md` (numbers only): how many bitmaps, the lowest RMSE, and which parts, if any,
  failed.

**L2. Build from source.**
- Add `scripts/build_msstyles.py SRC_DIR OUT.msstyles`:
  - it converts the INI files to UTF-16LE;
  - it compiles the `.rc` with `x86_64-w64-mingw32-windres`, replacing ReactOS's `<reactos/version.rc>` include with
    a small version block of our own;
  - it links a resource-only DLL like `build_iconpack.py` does (`-shared -nostdlib -Wl,--entry=0`).
- Import Lunar unchanged into `src/resources/themes/luna-open/` (bitmaps, INI files, `.rc`), with its licence note.
  Commit it untouched first, so every later edit is a visible diff.
- Check that the build reproduces Lunar: the same resources as `/root/themes-local/lunar.msstyles`.

**L3. Make it Luna.** Edit towards the genuine Luna by eye, part by part, with a local before/after sheet for each.
Order:
1. Window frame: the caption gradient (blue, rounded top corners), the caption height and font, the
   minimize/maximize/close buttons (blue rounded with white glyphs; close is red), the frame sides and bottom, and the
   inactive caption.
2. Taskbar and Start: the taskbar gradient, the green Start button, tray notch, taskbar buttons (normal, hot,
   pressed), and the Start menu's top band, two panes and bottom band.
3. Controls: push buttons (rounded, gradient, orange hot ring), check boxes and radio buttons, group boxes, combo and
   edit borders, scroll bars, tabs, progress bar (green blocks), trackbar, headers, tooltips and the menu bar.
4. Explorer bar (task panes): headers, chevrons and backgrounds.
5. Metrics and scale: caption, border, scroll-bar and menu sizes, and fonts (Tahoma 8; the caption in Trebuchet MS
   bold is not shipped, so choose an open font and say which), at 96 and 120 DPI.

Bitmaps you redraw from scratch: draw them as SVG in `src/resources/themes/luna-open/svg/` and render them in the
build (`rsvg-convert`), so the source is editable. The 8-bit rule applies.

**L4. Ship it.**
- `src/resources/themes/Luna.theme` points at the open style.
- `installer/package-wine.sh` builds it, and `scripts/setup-machine-root.sh` installs it.
- A local genuine Luna, when present, still overrides it on developer machines, as today.
- Provenance: `docs/themes/luna-open.md` lists every bitmap with its origin (Lunar unchanged, Lunar edited and how, or
  drawn), plus each INI change.

## Acceptance (report in `docs/OPEN_LUNA.md`)

- Local sheets: the open Luna against the genuine Luna on the same screens, at 100% and 125%:
  - desktop with taskbar and Start menu;
  - a Notepad window (active, inactive, maximized);
  - the controls test program from `UXTHEME_V4.md`;
  - My Computer with task panes.

  At a glance they should read as the same style.
- RMSE of every shipped bitmap against its closest genuine bitmap stays clearly above 0; report the lowest.
- Repository screenshots: open art only, from a prefix with no local-only files (a fresh prefix or the installed ISO).
- A fresh ISO install shows the open Luna by default on Luna.theme, in X11 and Wayland, with a 32-bit program on
  screen.
- Paste real output only. Look at every image before you describe it.

## Boundaries

- Work in `wexbuild` on branch `gemini/open-luna` from `origin/master`.
- Do not touch theme-engine code (`src/dlls/themeui`, `wine/dlls/uxtheme`). If the style needs an engine fix, write
  it down and stop for review.
- Codex works on the Wayland polish (`WAYLAND_POLISH_WORKORDER.md`); do not touch its files.
- Your icon round 3 (`OPEN_ART_WORKORDER.md`) can wait until this is reviewed.
- Git: use `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never` with a `timeout`; never push master; never force-push.
- `CLAUDE.md` rules apply: GPL headers on new scripts; no C escapes through heredocs, `sed` or `python -c`.

## Clarification: close is fine, copied is not (Claude, 2026-10-17)

The goal is a near-exact look. What decides whether a part is allowed is **how it was made**, not how close it ends up.

- **Style is free to match.** Colours, gradients, rounded corners, layout, sizes and simple glyphs (×, −, □, ✓,
  arrows) are ideas and functional elements, not protected expression. A part drawn independently may land almost
  pixel for pixel on Luna, and that is expected for simple parts: a gradient strip, a 1 px border, a flat fill, a
  13 px check mark.
- **Microsoft's bitmaps are not free.** Never extract, trace, sample or adjust a genuine Luna bitmap, even if the
  result would look the same as a redraw.

What this changes in the phases:
- **L1 rip check:** its job is to show that ReactOS Lunar was not made from Luna's bitmaps.
  - Judge it on the complex parts: Start button, Start menu bands, taskbar, caption with its corners, push-button
    and scroll-bar art, task-pane headers. A near-zero RMSE there means a copy.
  - Do not flag the simple primitives on RMSE alone: gradient strips, solid fills, 1–2 px borders, small glyphs.
  - List which bitmaps you counted as simple.
- **L3 editing:** for simple primitives, set the colours and sizes by eye until they match, and do not worry if the
  result is near-identical. For complex parts, draw them, as SVG where you redraw, and keep a before/after sheet.
- **Provenance** (`docs/themes/luna-open.md`) is what shows independence. For every bitmap, write down how it was
  made: Lunar unchanged; Lunar recoloured (which colours); or drawn (from which shapes). Never "derived from Luna".
- **Metrics** (caption height, border widths, fonts, margins) are facts, so matching Luna exactly is fine. Get them
  by measuring screenshots side by side, as the work order says: the numbers come out the same, and the provenance
  stays clean.

## Review (Claude, 2026-10-17): accepted, squash-merged; round 2 for details

Checked: the branch builds from source (`build_msstyles.py`, exit 0). In a fresh prefix with no local files,
`explorer /theme:Luna` loads it (`ThemeManager\DllName` = `...\Themes\Luna\Luna.msstyles`), and the desktop, Start
menu, Notepad, My Computer and Control Panel all draw in the open Luna. No separate program applies it: it is an
ordinary visual style chosen through `Luna.theme` and Display Properties. The Start button art has no logo.

Fixed at merge:
- **The six screenshots are withdrawn.** They were taken in a development prefix with local-only genuine XP resources
  (the Start button showed the Windows flag; our open art is the Wine glass). Clean screenshots replace them
  (`docs/luna/screenshots/clean-*.png`, `docs/luna/clean-prefix.log`). Rule again: committed screenshots only from a
  fresh prefix or the installed ISO.
- **`setup-machine-root.sh`:** the genuine-Luna override no longer reads a path on one machine (`/root/themes-local`).
  It uses `WEX_LOCAL_LUNA`, set only on developer machines.

**Round 2 (Gemini): details.** Compare each of these against the genuine Luna in local sheets
(`../WineExplorer-local/notes/luna/`), at 100% and 125%. One before/after sheet per item.
1. **Caption font.** Trebuchet MS is not installed, so captions fall back to a narrower, smaller face.
   - Choose an open humanist sans with similar width and weight (Fira Sans, OFL, if packaged in Debian; otherwise
     DejaVu Sans Bold).
   - Add a FontSubstitutes entry `Trebuchet MS` → it, in `src/resources/fonts/font-substitutes.reg` (coordinate with
     the fonts work order).
2. **Caption buttons.** Minimize and maximize read smaller and flatter than close. All three should be the same size,
   rounded, with a white 1 px outline and bold white glyphs.
3. **Start button.** "start" should be larger, bold italic white with a soft dark shadow. The green pill should be
   taller relative to the taskbar, with the brighter top shine.
4. **Start menu.**
   - The user picture gets a white rounded frame; the user name is larger, bold white with a shadow.
   - "All Programs" gets its green arrow button.
   - The orange separator line sits under the top band.
5. **Taskbar.** The active task button is the darker, pressed blue. The notification area is the lighter blue with its
   left highlight edge.
6. **Scroll bars.** Notepad's scroll bars still look classic. Check that the scroll-bar parts and the INI entries
   apply (light blue-grey rounded thumbs with a grip, rounded arrow buttons).
7. **Task pane background (My Computer).** It is too light. XP's left pane has a blue gradient behind the white
   group boxes.

Keep everything else as it is. Same rules: open shapes, colours by eye, provenance per bitmap.
