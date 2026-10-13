# Refactor map

P0 acceptance passed on 2026-10-12. All changed paths and text hunks are
classified against the pinned release/source snapshots. No source modules
had moved at that checkpoint. P1 acceptance passed;
validated branches and regression evidence are in [UPSTREAM.md](UPSTREAM.md).

P3b implementation progress is in refactor/P3B.md. Its browser boundary
moves explorer.c, browser_ui, browser_taskpane, browser.h and slideshow into
wexbrowse. The planned toolbars.c move is intentionally omitted: it implements
taskbar toolbars and depends on taskbar settings/layout. Browser bars already
live in browser_ui. Keeping taskbar code together avoids a wide interface;
one string/flag browser entry point leaves browser state private.
The historical P0 path/hunk hashes and source pin remain unchanged.

## Baseline and reproducible evidence

Use the real annotated `wine-11.19` tag, whose release commit is
`455e3509b98a6919fd4ad1def4803e08c41c03b2`. The fork currently has no tags;
the unchanged tag was fetched from `https://github.com/wine-mirror/wine`.
The fork's `master` was `081042ede6a165a36bf9988350dd584b440d7a3d`.
The audited project commit is `c9f9440dfc105d0c92fd3ecc7ffe53bfc4bd9a5b`.
This refresh includes the P1-tested battery initialization correction; all
other path and hunk decisions remain unchanged. P2 progress is in
[refactor/P2.md](refactor/P2.md).

`40c17e1db47ac0f8ae08506268bc2491f7bf1ef0` is useful only to identify
inherited import differences. Despite its VERSION file saying 11.19, its files
also contain later upstream changes. Treating all differences against the tag
as WineExplorer modifications would incorrectly claim those changes as ours.

The imported stock code matches upstream snapshot
`eba89375a0515957701928faac0f5007ef638b04` (October 5), **72 upstream commits
after the release tag**. Its entire diff against the import is 68 paths:
36 mode changes, the 20 omitted ntoskrnl files, eight open icon replacements,
and four CRLF/LF conversions. There are no additional stock-code changes in
that comparison. This provides a concrete original upstream series for P2,
rather than manufacturing commits from the imported snapshot. Inspect it with:

```text
git --git-dir=build/refactor/wine.git diff --raw --no-renames eba89375a0515957701928faac0f5007ef638b04 40c17e1:wine
git --git-dir=build/refactor/wine.git log --reverse --format="%H %s" wine-11.19..eba89375a0515957701928faac0f5007ef638b04
```

Run from the project root (Git and Python 3 required):

```text
git clone --bare --filter=blob:none --single-branch https://github.com/zcomputerwiz/wine.git build/refactor/wine.git
git --git-dir=build/refactor/wine.git fetch --no-tags https://github.com/wine-mirror/wine.git refs/tags/wine-11.19:refs/tags/wine-11.19
git --git-dir=build/refactor/wine.git fetch --no-tags . c9f9440dfc105d0c92fd3ecc7ffe53bfc4bd9a5b:refs/audit/wineexplorer
python scripts/refactor-audit.py --git-dir build/refactor/wine.git --output build/refactor/baseline
python scripts/refactor-audit.py --git-dir build/refactor/wine.git --output build/refactor/baseline --verify
```

The JSON inventory and full binary-capable patch are local, ignored evidence.
The script compares committed trees, not working files, so NTFS permissions,
line-ending conversion and the local asset overlay cannot distort the audit.
Every changed path records old/new blob ids and modes, import provenance,
patch hash and line count, and each text hunk's hash and added/removed counts.
Unmatched patch blocks are an error. `--verify` regenerates both artifacts
and checks exact equality against the recorded Git revisions.

The complete [path table](refactor/PATHS.md) and its
[coverage index](refactor/PATHS.json) are committed. Generate and check them with:

```text
python scripts/refactor-audit.py --git-dir build/refactor/wine.git --output build/refactor/baseline --verify --map docs/refactor/PATHS.md
python scripts/refactor-audit.py --git-dir build/refactor/wine.git --output build/refactor/baseline --verify --check-map docs/refactor/PATHS.json
```

Coverage checks reject missing/duplicate paths, changed patch or hunk hashes,
and mismatched snapshot revisions. Adding `--require-classified` is the P0
acceptance gate; it now passes with **zero unclassified paths**. Classification
memberships cover 436 M, 205 B, 12 N, 82 X, nine H and eleven U paths (mixed
files belong to multiple classes). B includes one upstream deletion, and N includes
four ws2_32 data files whose only difference is CRLF/LF conversion. The U paths
contain twelve independent fixes: alpha replacement, wizard focus, DST bias,
timezone cache, power capability battery presence, custom folder IconFile,
shortcut target icon, rename notifications and DrawText mnemonic suppression.
Also classified U: `MSSTYLES_GetNextToken` whitespace trimming and missing
empty Recycle Bin/Internet Explorer desktop icons, both using Wine's own art.
Separate changes in the same file must become separate P1/P2 commits.

[REVIEWS.json](refactor/REVIEWS.json) records the stock decisions against exact
per-file patch hashes. A changed patch invalidates its review when regenerating
the map. The U classifications establish scope only; P1 still requires
Wine-style code, regression tests, build verification and branch pushes.

Reviewed stock files now have explicit decisions for every text hunk in the
generated table and JSON. The acceptance gate also rejects a stock review
that has file-level classification but no hunk decisions. Graphics/theming,
shell lifetime, menu cues, X11 stacking/capture, and endpoint audio have been
reviewed, together with shell32 menus/views, resources and the Explorer boundary.
Single-receiver shell hooks and incomplete endpoint volume remain X:
upstream submission is deferred until their registration, channel, callback,
driver and WoW64 semantics can be tested and completed.

Initial inventory: **750 paths, 1,876 text hunks, 140,422 patch lines**;
six changes are mode-only. 217 paths are unchanged since the initial import,
and 190 match the fork's current master exactly. Counts include added assets
and treat moves as deletion/addition so no path is silently hidden by rename
detection.

## Extraction decisions

The shared resources and lookup module will be **wexres.dll**. It is extracted
first in P3b; resource ownership and its export boundary still require review.
P3 retains the shell in one module before any smaller-module extraction.

| File/change | Class | Target |
|---|---|---|
| Exact post-tag upstream blobs inherited by the import | B | Original upstream commits on `wineexplorer/11.19`; no new upstream submission |
| Executable-bit loss with identical content | N | Restore upstream modes during assembly |
| New project modules | M | `src/dlls` and `src/programs`; complete path list in PATHS.md |
| Existing explorer shell additions | H/M | Small stock loader hook and `src/dlls/wexshell` |
| Remaining stock differences | U/X/H/B/N | Explicit per-hunk targets in PATHS.md/REVIEWS.json |

`dlls/ntoskrnl.exe` has already been restored in the current project tree and
matches the release tag; it contributes no diff. Do not move it into `src/`.
Own public headers will live in `src/include/`; own theme definitions will live
in `src/resources/themes/`. Assembly must link those into Wine as well as
DLLs/programs. Replacement desk.cpl, wlanui and wmplayer modules retain all
Wine/ReactOS notices when moved; their existing stock directory names do not
make their custom implementations new upstream fixes.

## Stock items found beyond the initial candidate list

The initial extra candidates have now been evaluated for extraction scope:

| File/change | Proposed class | Target branch/commit |
|---|---|---|
| `dlls/kernelbase/locale.c`: preserve the locale chosen through the regional applet while the Unix locale is unchanged | U evaluation | `upstream/locale-preference`, if independently correct and tested; otherwise X |
| `dlls/user32/cursoricon.c`: registry cursor-scheme loading/cache | U evaluation | Independent cursor-scheme fix after cache/refresh semantics review; otherwise X |
| `dlls/shell32/changenotify.c`: notify watchers of either side of a rename/move | U evaluation | `upstream/rename-notifications` with a regression test |
| `dlls/shell32/openwith.c`, `dialogs.c`, `shlexec.c`: Open With implementation and removed stubs | X/U evaluation | Keep XP UI separate from independently upstreamable API behavior |
| `tools/make_makefiles`: monorepo `.git` fallback | H | Remove when wine becomes a submodule; no upstream MR |

These are proposals, not accepted P1 branches. No upstream branch has been
created or pushed yet.

The battery candidate is specifically `SystemPowerCapabilities`:
`SystemBatteryState` already uses sysfs in the release tag. Do not submit or
carry a redundant patch for that stock implementation.

Verification passed for exact evidence regeneration, all-path/all-hunk
coverage and `--require-classified`. Seven negative checks passed: missing path,
duplicate path, changed patch hash, missing hunk, wrong snapshot, unreviewed
classification and missing stock-hunk decisions all reject the map as expected.

## P0 acceptance and P1 extraction constraints

The complete diff is classified: 750 paths, 1,876 hunks and 140,422 patch lines.
N configure output must be regenerated from the classified inputs, not dropped
without replacement. The large-file MR 7746 backport is separate from XP
drag/drop changes in shfldr_fs.c. The empty Recycle Bin resource is separate
from XP wording/dialogs in shell32.rc. configure.ac's inherited upstream hunks
are B; project module entries are H.

FileIconInit remains X because the current cache/reload implementation includes
the project's nonstandard negative-id icon overrides. SHFindFiles is H: it
depends on our Search Results command and does not implement its folder/saved
search inputs. Neither is ready for an independent upstream submission as-is.

Explorer's browser state already moved into browser.h but is shared between
Explorer callbacks, browser UI, task pane and Search Companion. P3 moves them
together. P3b must first design an opaque browser/search boundary; do not export
that mutable state structure or force a wide callback interface. Existing
appwiz Create Shortcut and shell32 views/menus stay X until a real supported
extension or smaller export boundary is available. Optional P5 extraction
does not justify changing behavior during P3.
