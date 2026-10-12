# Work order: separate WineExplorer from stock Wine

Status: planned 2026-10-12, not started. Read `CLAUDE.md` and `docs/STOCK_CHANGES.md` first.

## Goal

Rebasing on a new Wine release should be routine: our own code lives in our own modules, stock Wine carries a short
series of reviewed commits, and genuine bug fixes go upstream so the series shrinks over time.

## Decisions (made with the user)

- **Our own Wine build**, not the distribution's (Debian's Wine is too old for our internal changes): upstream tag +
  our commits + our modules, packaged for Debian.
- **Fork:** `https://github.com/zcomputerwiz/wine` (mirror of upstream, branch `master`). Pushing branches there is
  approved. Merge requests to WineHQ are opened **manually by the user**; prepare them, do not submit.
  - `upstream/<topic>` — one branch per upstreamable fix, based on the fork's `master`, ready to become an MR.
  - `wineexplorer/<version>` (first `wineexplorer/11.19`) — tag `wine-11.19` plus our stock-Wine commits, one commit
    per change, rebased onto each new release (Proton's model, instead of a quilt series).
- **Explorer becomes a plugin:** stock `explorer.exe` loads `wexshell.dll` when the shell is enabled; all shell code
  moves into that DLL.
- **Proton/Steam stays separate** (see last section): no shared build.

## Layout after the refactor

```
WineExplorer/
  wine/            git submodule -> zcomputerwiz/wine, branch wineexplorer/<version>
  src/dlls/...     our DLLs (wexhost, wexshell, devmgr, netshell, *.cpl, ...)
  src/programs/... our programs (logonui, setup, oobe, calc, mspaint, mmc, sndvol32, wmplayer, ...)
  scripts/assemble.sh   links src/ modules into the wine/ tree before configure (one stock commit adds their
                        WINE_CONFIG_MAKEFILE lines and the explorer plugin hook)
```

Building stays Wine's own build (makedep handles resources, IDL, unixlibs and WoW64), so nothing about the build
output changes. A fully out-of-tree build (like Proton's lsteamclient) is possible later; not part of this order.

## Phases (commit after each working step; each phase ends with its acceptance check)

### P0 — Baseline audit and map
1. Diff `wine/` against the real `wine-11.19` tag from the fork, not against commit `40c17e1`: the import was
   imperfect (e.g. `dlls/ntoskrnl.exe` is missing from `40c17e1` — probably `.gitignore` `*.exe` — and many tool
   scripts lost their exec bit). List every difference.
2. Classify every changed stock file/hunk into: **U** upstreamable fix, **X** XP behaviour we keep as a fork commit,
   **H** to be replaced by a hook into our code, **M** moves to `src/` (new modules), **B** backport (drop on rebase),
   **N** noise (mode bits, generated files).
3. Write `docs/REFACTOR_MAP.md` with the table (file, change, class, target branch/commit).

Acceptance: every line of `git diff wine-11.19 -- wine/` is accounted for in the map.

### P1 — Upstream fix branches on the fork
Candidates (from STOCK_CHANGES rows marked upstreamable, confirm in P0): `server/fd.c` DST bias; ntdll timezone cache
on `/etc/localtime` change; comctl32 image list alpha flag; comctl32 wizard focus (`PROPSHEET_SetWizButtons`);
shell32 shortcut shows its target's icon; shell32 empty Recycle Bin icon (32); ntdll `SystemBatteryState` /
`SystemPowerCapabilities` from `/sys/class/power_supply`; and to evaluate: `RegisterShellHookWindow`, `SHFindFiles`,
`FileIconInit`, `IAudioEndpointVolume` (larger; may need splitting).

For each: branch `upstream/<topic>` from the fork's current `master`; Wine commit style (`module: Summary.`), Wine
coding style, a test in the module's `tests/` where Wine has one; build the module and run its tests on the Proton
VM (Xvfb). Leave authorship/sign-off to the user (WineHQ wants a real name; do not add AI co-author trailers on these
branches unless the user says so). Push the branch; list it in `docs/UPSTREAM.md` with a ready-to-paste MR description.

Acceptance: each branch builds on current upstream and its tests pass; `docs/UPSTREAM.md` lists them.

### P2 — `wineexplorer/11.19` fork branch
From tag `wine-11.19`: one commit per X/H/U/B item (U items also stay here until merged upstream), messages naming
the `STOCK_CHANGES` row. Then a commit that adds the configure entries for `src/` modules.

Acceptance: `wineexplorer/11.19` + `src/` modules assembled == today's `wine/` tree (diff empty apart from the moves).

### P3 — Explorer plugin (`wexshell.dll`)
1. Define the interface in one header shared by explorer and the plugin, e.g. `WexShellStart(desktop hwnd,
   width, height)`, `WexShellCommand(argc, argv)` for explorer's switches (`/shell`, `/turnoff`, `/wlan`,
   `/netstatus`, `/netprops`, `::{CLSID}` paths, `/e`), `WexShellStop()`. Keep it minimal and versioned.
2. Stock patch in `programs/explorer` (`desktop.c`, `explorer.c`): when the desktop has `EnableShell`/`/shell` (the
   existing gate), `LoadLibraryW(L"wexshell.dll")` and call it; if the DLL is absent, stock behaviour. Target well
   under 100 changed lines in stock files.
3. Move the ~102 shell files (taskbar, Start menu, desktop view, tray, file browser windows, ...) into
   `src/dlls/wexshell`. The explorer process stays the host (one process, as on Windows).

Acceptance: without the plugin, explorer behaves exactly like stock Wine; with it, the shell is unchanged — compare
screenshots on Xvfb (desktop, Start menu both styles, taskbar, tray, Explorer window, Turn Off dialog) and run the
dev VM session (`vm.sh`), the D1/D2 probes and the installer test.

### P4 — Repository restructure
`wine/` becomes the submodule; our modules move to `src/`; add `scripts/assemble.sh`. Update everything that knows
paths: `vm.sh`/`fe.sh` sync, `scripts/open_assets.py`, `scripts/add-license.py` (BASE becomes the tag), 
`scripts/rc-makedep.py`, installer `package-wine.sh`/`build-iso.sh`, docs, and the overlay in
`../WineExplorer-local` (genuine assets must still land on top of the assembled tree; never commit them here).

Acceptance: fresh clone with `--recurse-submodules` → `assemble.sh` → configure → make → install works on the Proton
VM; the ISO builds from it; `installer/tests/install-interactive.sh` passes; the dev VM session runs.

### P5 — Shell extension points (optional, case by case)
Where Wine supports a registered extension (context-menu handlers via `shellex`, namespace extensions, property sheet
handlers), move XP features out of shell32 patches into our DLLs (e.g. Send To). Only where Wine's support is
already there; otherwise the change stays an X commit.

### P6 — Rebase drill
Rebase `wineexplorer/11.19` onto the newest upstream release (or `master`), record time and conflicts per commit in
`docs/REBASE.md`, drop B commits that upstream now has.

## Proton and Steam (later, separate work)

Proton keeps its own Wine, wineserver and per-game prefixes; Steam is a Linux application in our session (the shell
already frames Linux windows and lists Linux applications). Plan: preinstall the Steam client with Steam Play on (or
ship Proton-GE in `compatibilitytools.d`); test full-screen games with the compositor's fullscreen bypass and with
gamescope. Steam is proprietary: the ISO needs Debian non-free and its own notice. No build integration with our Wine.

## Rules that apply throughout

CLAUDE.md rules hold: no Microsoft material in this repo or the fork; GPL headers for new files; never pass C
escapes through heredocs/sed/`python -c`; commit after each working step; keep `docs/STOCK_CHANGES.md` true at
every commit (it becomes the index of fork commits).
