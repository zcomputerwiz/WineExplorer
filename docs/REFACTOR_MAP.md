# Refactor map

P0 is in progress. No source modules have moved and no VM runtime has changed.
This document will become the reviewed file/hunk classification index; the
inventory alone does not satisfy P0 acceptance.

## Baseline and reproducible evidence

Use the real annotated `wine-11.19` tag, whose release commit is
`455e3509b98a6919fd4ad1def4803e08c41c03b2`. The fork currently has no tags;
the unchanged tag was fetched from `https://github.com/wine-mirror/wine`.
The fork's `master` was `081042ede6a165a36bf9988350dd584b440d7a3d`.
The audited project commit is `52842ca444a7c3b75a623b7c530b0204c5ea34c4`.

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
git --git-dir=build/refactor/wine.git fetch --no-tags . 52842ca444a7c3b75a623b7c530b0204c5ea34c4:refs/audit/wineexplorer
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
acceptance gate; it currently fails deliberately because **86 stock paths**
still need hunk decisions. Do not mistake complete inventory coverage for
completed classification. Whole-file classifications currently cover 434 M,
202 B, 11 N, ten X and seven U paths. B includes one upstream deletion, and N includes
four ws2_32 data files whose only difference is CRLF/LF conversion. The U paths
contain nine independent fixes: alpha replacement, wizard focus, DST bias,
timezone cache, power capability battery presence, custom folder IconFile,
shortcut target icon, rename notifications and DrawText mnemonic suppression.
Separate changes in the same file must become separate P1/P2 commits.

[REVIEWS.json](refactor/REVIEWS.json) records the stock decisions against exact
per-file patch hashes. A changed patch invalidates its review when regenerating
the map. The seven U classifications establish scope only; P1 still requires
Wine-style code, regression tests, build verification and branch pushes.

Initial inventory: **750 paths, 1,876 text hunks, 140,420 patch lines**;
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
| Exact post-tag upstream blobs inherited by the import | B, pending provenance review | Original upstream commits on `wineexplorer/11.19`; no new upstream submission |
| Executable-bit loss with identical content | N | Restore upstream modes during assembly |
| New project modules | M, pending complete path list | `src/dlls` and `src/programs` |
| Existing explorer shell additions | H/M, pending hunk review | Small stock loader hook and `src/dlls/wexshell` |
| Remaining stock differences | Pending | Review each hunk before assigning U/X/H/B/N |

`dlls/ntoskrnl.exe` has already been restored in the current project tree and
matches the release tag; it contributes no diff. Do not move it into `src/`.
Own public headers will live in `src/include/`; own theme definitions will live
in `src/resources/themes/`. Assembly must link those into Wine as well as
DLLs/programs. Replacement desk.cpl, wlanui and wmplayer modules retain all
Wine/ReactOS notices when moved; their existing stock directory names do not
make their custom implementations new upstream fixes.

## Stock items found beyond the initial candidate list

These need explicit decisions during the remaining hunk review:

| File/change | Proposed class | Target branch/commit |
|---|---|---|
| `dlls/kernelbase/locale.c`: preserve the locale chosen through the regional applet while the Unix locale is unchanged | U evaluation | `upstream/locale-preference`, if independently correct and tested; otherwise X |
| `dlls/user32/cursoricon.c`: registry cursor-scheme loading/cache | U evaluation | Independent cursor-scheme fix after cache/refresh semantics review; otherwise X |
| `dlls/shell32/changenotify.c`: notify watchers of either side of a rename/move | U evaluation | `upstream/rename-notifications` with a regression test |
| `dlls/shell32/openwith.c`, `dialogs.c`, `shlexec.c`: Open With implementation and removed stubs | X/U evaluation | Keep XP UI separate from independently upstreamable API behavior |
| `tools/make_makefiles`: monorepo `.git` fallback | N/build shim evaluation | Remove when wine becomes a submodule; no upstream MR |

These are proposals, not accepted P1 branches. No upstream branch has been
created or pushed yet.

The battery candidate is specifically `SystemPowerCapabilities`:
`SystemBatteryState` already uses sysfs in the release tag. Do not submit or
carry a redundant patch for that stock implementation.

Verification passed for exact evidence regeneration and all-path/all-hunk
coverage. Six negative checks also passed: missing path, duplicate path,
changed patch hash, missing hunk, wrong snapshot, and unreviewed P0 acceptance
all reject the map as expected.

## Remaining P0 acceptance work

Record classifications for all paths and all mixed stock-file hunks, including
generated configure/protocol files, import omissions, assets and upstream
snapshot changes. Then verify that the classification index accounts for the
complete saved diff before preparing P1 branches.
