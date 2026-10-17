<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order (Gemini): open fonts and runtimes in the base prefix and the ISO

Status: ready, 2026-10-17. Assignee: Gemini. Based on Gemini's "Custom Debian + Wine 11.x" plan, corrected by Claude.

## Goal

Every installed system has, without internet and without proprietary files:
- open, metric-compatible substitutes for the common Windows fonts;
- wine-mono (.NET) and wine-gecko (MSHTML);
- a supported way to add the DirectX-to-Vulkan layers;
- an admin step that downloads Microsoft's own redistributables only after the user accepts their licence.

Everything that is legally redistributable goes into the ISO and the shared machine root. Proprietary components are
downloaded on the user's machine, never shipped.

Today:
- the ISO ships only `fonts-dejavu-core`, `fonts-liberation` and `fonts-noto-mono`;
- no font substitutes are set;
- wine-mono and wine-gecko are switched off: `WINEDLLOVERRIDES="mscoree,mshtml="` in
  `installer/scripts/wex-gui-setup.sh` and `wex-oobe.sh`.

## Corrections to the original plan

1. **No stock Wine change.** Do not patch `loader/wine.inf.in`. The substitutes go into the shared machine root's
   HKLM from our own setup, which has the same effect for every prefix and keeps `STOCK_CHANGES.md` short
   (`CLAUDE.md` rule).
2. **Our prefix layout.** Work where the setup already does:
   - `installer/scripts/wex-gui-setup.sh` creates `/etc/skel/.wine`;
   - `scripts/setup-machine-root.sh` builds the shared machine root `/var/lib/wineexplorer/machine`;
   - each user has a per-user prefix linked into it (`docs/PREFIX_SPLIT_WORKORDER.md`).

   Do not add a separate pre-baked `/etc/skel/.wine` path.
3. **Font sources.**
   - fontforge is not needed: Wine's source ships its own fonts (`tahoma.ttf`, `marlett.ttf`, `symbol.ttf`,
     `wingding.ttf`, ...) prebuilt, and our build installs them under `/usr/local/share/wine/fonts`.
   - So `fonts-wine` is a duplicate, and `fonts-croscore` repeats `fonts-liberation2`'s metrics. Leave both out.
   - Tahoma stays Wine's own: the XP shell is drawn in it. No substitute for Tahoma.
4. **Mappings.**
   - IPAMona is not in Debian; use IPAGothic, IPAPGothic, IPAMincho and IPAPMincho.
   - SimSun is a serif face; use Noto Serif CJK SC.
   - Arial, Times New Roman and Courier New need entries too, because Wine does not match names through fontconfig
     aliases.
   - Batang, Lucida Console and Arial Narrow are added.
   - Consolas → Liberation Mono and Georgia → DejaVu Serif are fallbacks, not metric-compatible. If a metric-compatible
     Georgia (Gelasio) is packaged in Debian trixie, use it.
5. **DXVK and vkd3d-proton stay opt-in.**
   - Turning them on by default breaks systems without a Vulkan device, VMs included, where wined3d works.
   - Wine already uses vkd3d internally for Direct3D 12.
   - Ship them, but switched off.
6. **Winetricks verbs dropped:**
   - `wmp11`: it conflicts with our own Windows Media Player (ported from ReactOS);
   - `sapi`: Wine has SAPI built in;
   - `d3dcompiler_47`: Wine's built-in one (vkd3d-shader) comes first; native stays optional.
   - The "never override" list of the plan is right: `dwrite`, `gdiplus`, `usp10`, `riched20`, `mf`, `msxml*`,
     `ucrtbase`, `wininet`, `crypt32` and core DLLs.
7. **EULA downloads.**
   - Make them an administrator-run "Windows Components" step that installs into the machine root, so they are
     shared.
   - Not a per-user autostart.
   - Downloading after the user accepts Microsoft's licence is lawful; redistributing is not, so nothing proprietary
     goes on the ISO or into the repository.

## Phases (one review at the end)

**F1. Fonts (data files by Jules).**

Jules is adding these (PRs against master; pull them first):
- the font packages in `installer/live-build/config/package-lists/wineexplorer.list.chroot`;
- `src/resources/fonts/font-substitutes.reg`;
- `src/resources/fonts/font-links.txt`.

You do:
- **Selawik.** Fetch Microsoft's Selawik release (SIL OFL 1.1, github.com/microsoft/Selawik). Pin it to a release tag
  and SHA256 in `installer/package-wine.sh`, like the icon sources, and install it to
  `/usr/local/share/fonts/selawik/`. Add its row to `LICENSE.md`.
- **Check every target family exists.** In the built image, every target family in `font-substitutes.reg` must be
  listed by `fc-list : family` with exactly that name: Selawik's weight families, `Noto Sans Symbols 2`,
  `Noto Serif CJK SC` and `Liberation Sans Narrow` are the ones to check.
  - Fix names that differ.
  - If a family is not installed by the listed packages, add the smallest package that has it, or drop the line,
    and say which.
- **Import the substitutes** into the machine root's HKLM from `scripts/setup-machine-root.sh` (`wine regedit` of
  the `.reg`). Do it on every machine-root update, so changes reach existing installs.
- **Create the font links** in the machine root's `C:\windows\Fonts` from `font-links.txt` as symbolic links.
  Missing targets are skipped with a message.
- **Measure the ISO size** before and after, and report it. `fonts-noto-core` and `fonts-noto-cjk` are large: if the
  image grows by more than about 150 MB, propose which to trim (for example only the CJK Regular/Bold weights).

**F2. wine-mono and wine-gecko offline.**
- Bundle the exact versions Wine 11.19 expects, x86 and x86_64 for gecko, in `/usr/local/share/wine/mono` and
  `/usr/local/share/wine/gecko`.
  - Find the versions in Wine's `dlls/mscoree` and `dlls/appwiz.cpl` sources at the pinned commit.
  - Pin the downloads with SHA256 in `installer/package-wine.sh`.
- Licences: wine-mono is MIT, LGPL and GPL parts; wine-gecko is MPL 2.0. Add `LICENSE.md` rows; their licence files
  ship next to them.
- Remove `mscoree,mshtml=` from the setup scripts. Check that a fresh install sets up mono and gecko in the machine
  root without a prompt or network, and that every per-user prefix uses them.

**F3. DirectX to Vulkan, opt-in.**
- Ship DXVK and vkd3d-proton pinned (zlib and LGPL-2.1; `LICENSE.md` rows) under
  `/usr/local/share/wineexplorer/dxvk/`.
- They are off by default.
- Add `wex-graphics vulkan on|off` (a small script) that sets or clears the per-user DLL overrides and links. It
  refuses `on` when `vulkaninfo` finds no device.
- Document it in `docs/HANDOFF.md`.

**F4. Windows Components (EULA downloads).**
- An administrator-only step offers optional Microsoft redistributables after showing and accepting their licence:
  - Visual C++ 2015–2022 (`vcrun2022`);
  - core fonts (`corefonts`, `tahoma`), off by default;
  - DirectX extras (`d3dcompiler_47`, `xact`, `physx`), each optional.
- It installs into the machine root through winetricks, using a shared cache `/var/cache/winetricks`.
- Add `winetricks` to the package list if it is not already pulled in, and check that the version in trixie supports
  these verbs.
- XP-style entry: a "Windows Components" item, the same place XP's "Add/Remove Windows Components" is. If that applet
  does not exist yet, a first-logon prompt for administrators is fine.
- Nothing is downloaded without the click. A limited user cannot start it.

## Acceptance (report in `docs/FONTS_RUNTIMES.md`)

- In a fresh ISO install, without network:
  - `fc-match` and the Wine font dialog show each substitute;
  - a test document in Calibri, Cambria, Segoe UI and Arial renders in Carlito, Caladea, Selawik and Liberation
    with the same line breaks (WordPad, 32- and 64-bit).
- A CJK test string in MS Gothic, SimSun and Malgun Gothic renders without empty boxes.
- A small .NET Framework program and an MSHTML-based program (for example a help file viewer or our own browser
  control) run, without network, in both sessions.
- With `wex-graphics vulkan on` on a machine with Vulkan, a Direct3D 11 sample runs through DXVK. With it off, or
  with no device, wined3d is used and nothing breaks.
- Windows Components downloads nothing before acceptance, and installs vcrun2022 into the machine root for all users.
- Report the ISO size before and after.
- Real output and screenshots only, from the installed ISO, open art in repository screenshots. Run the X11 and
  Wayland install tests and a 32-bit program regression.

## Boundaries

- Work in `wexbuild` on branch `gemini/fonts-runtimes` from `origin/master`, after Jules' two PRs are merged.
- Do not touch theme-engine code, Codex's Wayland files, or the open Luna branch.
- Git: use `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never` with a `timeout`; never push master; never force-push. If
  pushing from `wexbuild` fails on credentials (`git-credential-manager.exe: Invalid argument`), say so instead of
  working around it.
- `CLAUDE.md` rules apply: GPL headers on new scripts; no C escapes through heredocs, `sed` or `python -c`;
  proprietary Microsoft files never enter the repository or the ISO.
