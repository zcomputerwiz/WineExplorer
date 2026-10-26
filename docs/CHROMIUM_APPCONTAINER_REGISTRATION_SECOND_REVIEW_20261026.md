<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Registration re-review: retain improvements, fix remaining publication defects

Reviewed root7043dee76f0df163a6e2347b5b20a4ad48694750 and Wine
44c8989453c2b9ff20773c5669cf220fdc6bc2b7. Actual export/production/test diffs,
committed report and private ac_run.sh/ac_evidence.log/ac_clean_configure.log
examined; both submitted-range whitespace checks pass. Worker reports49 tests
per architecture/0 failures/0 skips; parent has not rebuilt or rerun the DLLs.
Not accepted yet for import/browser trial.

Improvements retained: absolute mutant path, OBJ_OPENIF, acquisition/wait errors
propagated, lookup uses KEY_READ, type/even-length/terminator checks, distinct
lookup allocations, derived package SIDs, owned child handles, no broad pkill.
These address substantial first-review defects. Do not rewrite the whole design.

## 1. Publication rollback does not have deletion rights (P1)

Register opens/creates the leaf with KEY_READ|KEY_WRITE, then calls NtDeleteKey
on that handle when ac_publish_values fails. DELETE is not included in those
masks. This Wine revision server/registry.c delete_key explicitly requests DELETE
from get_handle_obj. Parent original Windows own-key probe independently returned
STATUS_ACCESS_DENIED (0xc0000022) for exactly KEY_READ|KEY_WRITE followed by
NtDeleteKey; probe key cleaned through its owning API. Private result:
D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/appcontainer-parent-lock-review-20261026/native-rollback-access.json.

Acquire rights needed to remove ONLY the newly owned leaf, evaluate rollback
status and document failure policy rather than claim successful rollback blindly.
Test a real second-value publication failure, confirm absent/incomplete state is
handled as designed, retry safely, and assert another existing mapping is intact.
A passing normal registration/deletion does not cover this path.

## 2. Existing mapping can be overwritten after a read error (P1)

The REG_OPENED_EXISTING_KEY branch treats every result except successful moniker
read plus display-value presence as an abandoned leaf. E_OUTOFMEMORY,
ACCESS_DENIED or another query failure therefore reaches ac_publish_values with
the new caller's data and can replace a previously valid mapping. The Boolean
ac_leaf_has_value also conflates absence with unrelated errors and does not
validate the claimed complete DisplayName. This violates duplicate preservation
and error propagation. Recovery must be based on POSITIVELY established partial
state/owned publication state, not an arbitrary failure. Propagate transient,
allocation and access failures unchanged and leave existing bytes untouched.

Add deterministic failure-path tests using real production code with narrow
internal test hooks or controlled own-key permissions/faults; no Python mirror.
Prove complete original moniker/display survive read/allocation failure and
failed duplicate. Prove the explicitly supported incomplete-leaf recovery
without taking ownership of unrelated/corrupt data by guesswork. A one-off
precreated empty key is not a test of process death/abandoned lock; either add
an actual owned-writer death test or narrow that claim honestly.

## 3. Evidence gaps (P2, bounded corrections)

- BlockedReader starts a child then waits2s for non-exit, with no ready/entered
  handshake. A child delayed in startup also passes. Add explicit independent
  readiness and a discriminating negative/control so removing reader locking
  reliably fails the test. Confirm complete publication, not only reading an
  already-complete value after an arbitrary startup delay.
- ac_reap still ignores WAIT_FAILED, TerminateProcess result and final reap
  result; LockFailure closes evh once then again at done. Remove double-close,
  check cleanup outcomes, and make required setup/timeout failures failures,
  not the remaining win_skip paths on the actual implementation. Do not claim
  impossible/no-leak guarantees without those outcomes.
- ac_run.sh prints a failing process exit but does not return failure from
  run_arch, has no pipefail/aggregate result, ignores scoped-cleanup errors,
  sleeps before copying DLLs without waiting for owned wineserver exit, and
  overwrites old logs. Commit a bounded original runner that returns nonzero
  for build/test/cleanup failures, waits for owned shutdown before module copy,
  uses fresh output paths and preserves old evidence. No broad kill patterns.
- Actual sentinel evidence ends '[warn] foreign wineserver still up after
  teardown' then RESULT PASS. Isolation of the main cleanup may pass, but full
  sentinel teardown is NOT proven. Preserve this failure and clean only the
  exact owned sentinel with checked outcome, do not claim it was removed.

## 4. Reproducibility finding is misdiagnosed (P2)

Report says wine/dcomp_underlay.h was never committed. It IS tracked at44c8989:
include/wine/dcomp_underlay.h blobf1b0bafaeb1cabe7d51be6127fa26cdefdd4d961.
The same revision include/Makefile.in lists wine/dcomp_layer.h but does not list
wine/dcomp_underlay.h. Investigate the makedep/install-list prerequisite rather
than blaming a nonexistent source file. wex*.h are project overlay headers:
provide exact preparation from committed WineExplorer src/include sources where
needed, not unknown local headers. A minimal separate prerequisite fix/known
project preparation is permitted in your OWN checkout; keep it separate from
API semantics and describe its source. No other-worker files or old build edits.

Deliver clean-room reproducibility from exact published Wine/root inputs and
explicit preparation steps, without undocumented configure edits/deleted dirs.
Verify actual loaded module evidence as well as intended path echoes, and run
x64/i686 targeted tests plus accepted SID regression checks. Report exact
failure/skip counts honestly. No need to retest unrelated graphics matrices.

## Next handoff

Antigravity supervises same implementer/branches, no competing patch. Commit AND
push corrected Wine, original runner/tests/report/root gitlink; reply directly
codex with commits/evidence and stop. No R6 prep/VM/SSH/browser/master merge,
unrelated token work, bypass or vendor publication. Parent independently reviews.
