<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# AppContainer registration APIs — implementation & rework report

Status: **second parent-review rework complete, pushed for independent parent review.** This is the
implementation companion to `CHROMIUM_APPCONTAINER_REGISTRATION_IMPLEMENTATION_WORK_ORDER_20261025.md`
and addresses two successive rounds of parent-review rework:

- First round (R1–R5), recorded in `docs/CHROMIUM_APPCONTAINER_REGISTRATION_REVIEW_REWORK_20261026.md`,
  delivered as wine commit `44c8989` — **retained unchanged** as the foundation.
- Second round (the four defects below), delivered as wine commit `cb1d117` on top of `44c8989`.

Scope reminder: these kernelbase exports register **metadata only** (a current-user, SID-keyed UTF-16
moniker/display mapping). They create **no** lowbox token, directory profile, or security boundary. No
test in this change makes a token/sandbox success claim from the mapping APIs.

## Branches and commits

| Repo | Branch | Base | Commit pushed |
|------|--------|------|---------------|
| wine (owned worktree) | `codex/g4-appcontainer-registration` | `e096ae4` (accepted SID derivation) | `cb1d117d02fef13af18772104b942c4aded50d00` (parent `44c8989`) |
| root (owned worktree) | `codex/chromium-appcontainer-registration-implementation` | `e405e33` | this report + wine gitlink bump `44c8989 → cb1d117` |

Files changed in the second-rework wine commit (`cb1d117`), exactly three:
- `dlls/kernelbase/security.c` (defects 1 & 2 — implementation)
- `dlls/kernelbase/tests/security.c` (defect 3 — conformance tests / harness)
- `include/Makefile.in` (defect 4 — one line: add `wine/dcomp_underlay.h`)

No other production, test, spec, or build file was modified. The accepted `userenv` SID derivation and
the normal userenv route are untouched. The untracked `wex*.h` graphics headers were **not** committed
(see defect 4).

## Second parent-review rework — the four defects

### Defect 1 (P1) — rollback needs the DELETE right; evaluate NtDeleteKey's status

In `AppContainerRegisterSid`, the new leaf was opened with `KEY_READ | KEY_WRITE`; when
`ac_publish_values` failed, the rollback `NtDeleteKey(key)` returned `STATUS_ACCESS_DENIED`
(`0xC0000022`) because `DELETE` (`0x00010000`) was omitted from the desired-access mask — leaving an
unowned, incomplete leaf behind.

Fix: the leaf is now opened with `KEY_READ | KEY_WRITE | DELETE`. The rollback evaluates
`NtDeleteKey`'s return: `STATUS_PENDING` is accepted (deletion queued) and any other failure is logged
via `WARN("rollback NtDeleteKey failed: %08x")` **without masking** the real publish error returned to
the caller.

Coverage / honesty note: a deterministic in-test trigger of a *mid-publish* failure (first value written,
second `NtSetValueKey` failing) requires fault injection between two writes on an owned key, which the
conformance harness cannot deterministically induce; that branch is therefore exercised defensively and is
verified by code inspection plus the fact that `DELETE` is granted for our own HKCU keys. The surrounding
key-management paths (fresh create, abandoned-leaf repair, duplicate) are directly tested (see defect 2).

### Defect 2 (P1) — strictly check read errors on REG_OPENED_EXISTING_KEY; never clobber on a transient failure

Previously, when `disposition == REG_OPENED_EXISTING_KEY`, any failure in `ac_read_moniker_value`
(`E_OUTOFMEMORY` / access error) fell through to `ac_publish_values`, overwriting a valid existing mapping.

Fix: the existing-key branch now distinguishes three cases and never clobbers on a transient failure:
1. **Complete, valid mapping** (Moniker present *and* DisplayName present) → `ERROR_ALREADY_EXISTS`,
   original left intact.
2. **Positively incomplete leaf** → repair by publishing both values (we hold the lock): Moniker absent
   (`STATUS_OBJECT_NAME_NOT_FOUND`), or Moniker present but DisplayName absent.
3. **A read/allocation/access error while inspecting it** → propagate the real failure and touch nothing.

DisplayName presence is checked with a tri-state value-status probe (`ac_value_status`) that returns
`STATUS_SUCCESS` / `STATUS_OBJECT_NAME_NOT_FOUND` / (real error), so a genuine read error is never
conflated with mere absence.

Directly tested:
- **Duplicate registration** of the same SID returns `ERROR_ALREADY_EXISTS` and leaves the original
  mapping intact (`test_AppContainerRegistration`) — proves an existing complete mapping survives a second
  writer.
- **Abandoned-writer repair**: a pre-created empty leaf (Moniker positively absent) is repaired to a
  complete mapping on the next register (`test_AppContainerAbandonedWriter`).

Honesty note: the transient-read-failure branch (case 3) is defensive; forcing an `E_OUTOFMEMORY`/access
failure inside `ac_read_moniker_value` deterministically requires fault injection and is not exercised by a
dedicated test. Its correctness rests on the code path (propagate, do not write) plus cases 1–2 being
directly tested.

### Defect 3 (P2) — evidence & test-harness corrections

- **Blocked-reader handshake.** `test_AppContainerBlockedReader` no longer relies on an arbitrary sleep to
  assume the child reached the blocking read. The child now signals a named event
  (`WineACBlockReadEntered_<ppid>`) the instant before it calls the lookup; the parent waits on that event,
  then asserts the child is still blocked (bounded wait → `WAIT_TIMEOUT`), releases the lock, and verifies
  the child completes with the correct moniker. Reader locking is thus *demonstrably* required.
- **`ac_reap` return-value evaluation.** The bounded/idempotent child-reaper now evaluates the returns of
  `WaitForSingleObject` (initial and post-terminate) and `TerminateProcess`; a child that will not die is
  surfaced on stderr rather than silently ignored, and handles are closed exactly once.
- **LockFailure double-close removed.** The duplicate `NtClose` of the event handle in
  `test_AppContainerLockFailure` is gone (single close).
- **Required failures are failures, not skips.** Required setup cases that cannot run now fail (`ok(FALSE)`)
  instead of `win_skip`. Only genuine platform-conditional guards ("AppContainer registration APIs are not
  available", i.e. `GetProcAddress` returned NULL) remain `win_skip`.

### Defect 4 (P2) — build reproducibility & headers

- `include/wine/dcomp_underlay.h` is **tracked** in the wine tree but was missing from
  `include/Makefile.in`; it is now listed in `SOURCES`, so `makedep` and the install list are consistent.
  This is a one-line, in-scope change (the header file itself was already committed).
- The untracked public `wex*.h` graphics headers (`wextimezone.h`, `wexbrowse.h`, `wexhost.h`, etc.) are
  **deliberately not committed** to the wine tree. They remain build-environment prerequisites sourced from
  the fork's committed `WineExplorer/src/include` baseline; an out-of-scope `wextimezone.h` line that was
  present as a local dirty edit in `include/Makefile.in` was removed so this commit adds only the
  `dcomp_underlay.h` entry.

## First parent-review rework (R1–R5) — retained foundation

These were delivered as wine commit `44c8989` and are unchanged by the second round:

- **R1 — NT mutant object path & locking (no fail-open).** The session lock is a named mutant in
  `\BaseNamedObjects\WineAppContainerRegistrationLock`, created with `OBJ_CASE_INSENSITIVE | OBJ_OPENIF`;
  a create error other than `STATUS_OBJECT_NAME_EXISTS` returns an error and does **not** proceed unlocked.
  Both writers and readers take the lock, so a mapping is never observed half-published. A complete, valid
  duplicate register returns `ERROR_ALREADY_EXISTS`; an incomplete leaf is deterministically repaired under
  the lock. Proven by `test_AppContainerBlockedReader` and `test_AppContainerLockFailure` (a pre-occupied
  lock name of a different object type leaves the registry untouched).
- **R2 — test-runner isolation & reproducibility.** The owned runner (`ac_run.sh`) shuts down Wine only for
  its own prefix via `WINEPREFIX=$PREFIX wineserver -k`/`-w`, verified by a read-only `/proc/<pid>/environ`
  scan keyed on the exact prefix; it never issues a name/pattern `pkill`. It exits nonzero on build failure,
  an unconfirmed prefix teardown before a DLL copy, or any architecture's test failures (aggregated status).
- **R3 — legitimate derived SIDs, robust child lifecycles & race tests.** Test identities are package SIDs
  derived at runtime via `DeriveAppContainerSidFromAppContainerName` from unique per-run names. Children are
  launched with `CreateProcessW`, awaited with bounded timeouts, and reaped idempotently; the concurrent test
  uses a manual-reset named event so two real children release together deterministically.
- **R4 — storage error propagation & value validation.** `AppContainerRegisterSid` returns
  `ERROR_ALREADY_EXISTS` only for a complete valid existing mapping; `ac_read_moniker_value` validates type
  (REG_SZ), even byte length, bounds, and a final NUL WCHAR before allocating; `AppContainerLookupMoniker`
  opens HKCU with `KEY_READ` only. HKCU access uses `RtlOpenCurrentUser`; all registry I/O uses the Nt* set
  (kernelbase is below advapi32).

## Results — both architectures, fresh build from committed source `cb1d117`

| arch | tests executed | todo | flaky | failures | skipped | exit |
|------|----------------|------|-------|----------|---------|------|
| x86_64-windows | 49 | 0 | 0 | **0** | **0** | 0 |
| i386-windows | 49 | 0 | 0 | **0** | **0** | 0 |

Observed child traces (both arches): persistence lookup `hr=0 moniker="PersistMoniker"`; concurrent race —
exactly one winner `hr=0`, the other `hr=0x800700b7` (`ERROR_ALREADY_EXISTS`); blocked-reader publication
`hr=0 moniker="PubMoniker"`.

### Reproducibility of the delivered source

- **Byte-identity.** `git diff cb1d117 -- dlls/kernelbase/security.c dlls/kernelbase/tests/security.c
  include/Makefile.in` is empty and `git status --short` for those paths is clean, so the kernelbase source
  compiled in the owned build tree is byte-identical to the committed source. The passing binaries were built
  from the committed source, not from uncommitted edits.
- **Exact build inputs** (owned out-of-tree tree `/root/wine-ac-build2`, srcdir = the wine worktree):
  - configure: `./configure --enable-archs=i386,x86_64 --prefix=/root/wine-ac-build2/install` (Wine 11.19)
  - toolchain: gcc (Debian 14.2.0-19) 14.2.0, GNU Make 4.4.1; cross compilers `i686-w64-mingw32-gcc` and
    `x86_64-w64-mingw32-gcc`.
  - build: `make -j16` (incremental recompile of `security.o` for impl+test, both arches).
- **Loaded module paths + input hashes** (this run):

  | artifact | path | sha256 |
  |----------|------|--------|
  | x86_64 kernelbase.dll | `/root/wine-ac-build2/install/lib/wine/x86_64-windows/kernelbase.dll` | `a46c9773c16838906fdc610de2885ad0a60097309efcf89762d3c8ae31aa79d7` |
  | i386 kernelbase.dll | `/root/wine-ac-build2/install/lib/wine/i386-windows/kernelbase.dll` | `1b363d8b2a338d665615ec79ea440a69db5c8908d029864d1f1e4753334865ff` |
  | x86_64 test exe | `/root/wine-ac-build2/dlls/kernelbase/tests/x86_64-windows/kernelbase_test.exe` | `1ea887fcbd3c998fa9462e5bf5d2cd481657bd8d828f4145a64d7ae630587f50` |
  | i386 test exe | `/root/wine-ac-build2/dlls/kernelbase/tests/i386-windows/kernelbase_test.exe` | `2fb9a9c9adfbc6034029f4d3a2f089c1939754372cbae56204cfe7a741061c92` |

  (Hashes identify this build's inputs; Wine embeds build metadata, so they are not a cross-build constant.
  The reproducibility claim rests on byte-identical source + the recorded configure/toolchain/make args above.)

**Pristine-archive finding (honest limitation, pre-existing, not from this change).** A fully isolated build
from `git archive cb1d117` still cannot complete `configure`: `tools/makedep` fails during `config.status`
because the committed tree references public `wex*.h` headers whose files are only present as untracked local
files in the fork's graphics baseline. The specific `wine/dcomp_underlay.h` inconsistency is now resolved by
defect 4 (the tracked header is in the install list), but the broader `wex*.h` condition is a pre-existing
property of the fork's graphics work and is **unrelated to and not required by** the kernelbase AppContainer
change: neither `security.c` nor `tests/security.c` includes any such header (they use only standard Wine/NT
headers). Fixing it would mean committing graphics files or altering the fork's build system, which is out of
scope. Per R2's option (b), the minimal prerequisite is clearly separated and documented here rather than
quietly used: the build environment carries the fork's local `wex*.h` headers; the AppContainer change itself
has no dependency on them.

## Limitations

- The registration is metadata only; it enforces no token/capability/ACL boundary.
- Two defensive branches are not covered by a dedicated deterministic test because they require fault
  injection that the conformance harness cannot induce: (a) the mid-publish rollback in defect 1 (a failing
  second `NtSetValueKey` on an owned key), and (b) the transient-read-failure branch of defect 2 (an
  `E_OUTOFMEMORY`/access error inside `ac_read_moniker_value`). Both are verified by code inspection and by the
  directly-tested adjacent cases; they are surfaced (WARN / stderr) rather than silently ignored.
- A pristine-archive full-tree `configure` is blocked by pre-existing uncommitted fork `wex*.h` graphics
  headers (see above); the AppContainer change is independent of them and was built/tested in the owned tree
  with those documented prerequisites present.
- Test SIDs are derived per-run and cleaned up; no persistent identities beyond the test's own mappings remain,
  and no arbitrary host registry keys are touched.

## Raw logs (private, D: only — not committed)

- `D:\DeepThought\Projects\OpenClaude\ac_build\ac_x64_stdout.log`, `ac_i386_stdout.log` (this run's per-arch
  test output; each ends "49 tests executed … 0 failures), 0 skipped").
- `ac_run.sh` reproducibility manifest, `ac_evidence.log` (sentinel + build facts).
