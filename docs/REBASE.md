<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Rebase drill

October 13, 2026: the retained wineexplorer/11.19 series was rebased onto
the current upstream master, verified directly with git ls-remote against
wine-mirror/wine. Upstream tip: 081042ede6a165a36bf9988350dd584b440d7a3d.
The base remains the same as the P1 upstream-test snapshot.

The isolated candidate is [codex/rebase-drill](https://github.com/zcomputerwiz/wine/tree/codex/rebase-drill),
head 95fa31a9a8c31725d291cd482b7e5b93822fa064. The published
wineexplorer/11.19 branch and production submodule pin remain d0a0af0681;
the candidate is available for review without rewriting that branch.

Git replay completed in 52.798 seconds with zero conflicts. Per-commit
start timestamps, replay duration, original/new hashes, subject and conflict
lists are recorded in [refactor/P6_REBASE.json](refactor/P6_REBASE.json).
These durations include the instrumentation callback; they are not compiler
timings. git range-diff reports all 93 retained commits unchanged.

Backports removed from the replay:

- The original 72 import commits through eba89375a0 are already ancestors
  of upstream master. The merge base is eba89375a0515957701928faac0f5007ef638b04;
  they are no longer carried as a post-release backport series.
- 0b34bd48e3f87e2d7305596af4a9157386fc7294 (CharToOemBuffW byte-count fix,
  preserving the original upstream author) was skipped as an already-applied
  patch. Range-diff shows this one removal and no rewritten feature patches.

The other retained file-size backport remains in the series; it was not
silently dropped. Upstream bug-fix topics remain prepared for manual WineHQ
submission by the user.

The source replay, range checks and full 64-bit candidate build pass.
Evidence: /srv/protonexplorer/wex-refactor-p6/build.status is
REBASE_BUILD_PASSED, with complete compiler log in p6-build.log.
An isolated candidate runtime smoke check also passes (checks/status:
CHECK_PASSED): tray bounds and all eight native dialog placement cases.
Log: /srv/protonexplorer/p6-smoke.log. The candidate is not installed on the
dev VM and does not replace the production submodule pin.
The build uses the public P4 project sources assembled with the candidate;
unchanged input timestamps were preserved only after byte equality checks.
P4's full build/install, ISO, interactive installation and live dev acceptance
pass on the production pin, recorded in refactor/P4.md.
Use a clean fork checkout for future rebases; assembly links deliberately
modify build checkouts and preserve stock stubs under ignored build output.
