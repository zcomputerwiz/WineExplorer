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

## Remaining P0 acceptance work

Record classifications for all paths and all mixed stock-file hunks, including
generated configure/protocol files, import omissions, assets and upstream
snapshot changes. Then verify that the classification index accounts for the
complete saved diff before preparing P1 branches.
