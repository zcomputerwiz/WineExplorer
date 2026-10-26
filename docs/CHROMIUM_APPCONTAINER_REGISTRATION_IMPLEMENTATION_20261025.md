<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# AppContainer registration APIs — implementation & rework report (R1–R5)

Status: rework complete, pushed for independent parent review. This is the
implementation companion to `CHROMIUM_APPCONTAINER_REGISTRATION_IMPLEMENTATION_WORK_ORDER_20261025.md`
and addresses the parent-review rework in
`docs/CHROMIUM_APPCONTAINER_REGISTRATION_REVIEW_REWORK_20261026.md`.

Scope reminder: these kernelbase exports register **metadata only** (a
current-user, SID-keyed UTF-16 moniker/display mapping). They create **no**
lowbox token, directory profile, or security boundary. No test in this change
makes a token/sandbox success claim from the mapping APIs.

## Branches and commits

| Repo | Branch | Base | Commit pushed |
|------|--------|------|---------------|
| wine (owned worktree) | `codex/g4-appcontainer-registration` | `e096ae4` (accepted SID derivation) | `44c8989453c2b9ff20773c5669cf220fdc6bc2b7` (parent `74cb1f3`) |
| root (owned worktree) | `codex/chromium-appcontainer-registration-implementation` | `e405e33` | this report + wine gitlink bump `74cb1f3 → 44c8989` |

Files changed in the wine commit (`44c8989`), exactly two:
- `dlls/kernelbase/security.c` (implementation)
- `dlls/kernelbase/tests/security.c` (conformance tests)

No other production, test, spec, or build file was modified. The accepted
`userenv` SID derivation and the normal userenv route are untouched.

## R1 — NT mutant object path & locking (no fail-open)

The session lock is a named **mutant** in the fully-qualified base namespace:

```
\BaseNamedObjects\WineAppContainerRegistrationLock
```

Acquisition (`ac_lock_acquire`):
1. `NtCreateMutant(&lock, SYNCHRONIZE, &attr, FALSE)` with
   `OBJ_CASE_INSENSITIVE | OBJ_OPENIF`. A create error other than
   `STATUS_OBJECT_NAME_EXISTS` (e.g. the name is occupied by a different object
   type) returns an error HRESULT and **does not proceed unlocked** — there is no
   fail-open path.
2. `NtWaitForSingleObject(lock, FALSE, NULL)`; the returned status is evaluated:
   only `WAIT_OBJECT_0` or `WAIT_ABANDONED` are treated as success. Any other
   status closes the handle, nulls it out, and returns an error.

Both **writers and readers** take the lock, so a mapping is never observed
half-published. Release (`ac_lock_release`) does `NtReleaseMutant` + `NtClose`.

Deterministic abandoned-writer policy: if a writer dies after creating its leaf
key but before both values are present, the next registrant (under the lock) sees
an **incomplete** leaf and deterministically **repairs** it by publishing both
values — it does not treat an incomplete leaf as an existing registration and it
does not invent existence from partial data. A complete, valid mapping on a
duplicate register returns `ERROR_ALREADY_EXISTS` and is left intact.

Tests proving the required properties:
- `test_AppContainerBlockedReader`: parent registers, then **holds** the real
  mutant; a lookup child is launched and observed to block (`WAIT_TIMEOUT`) while
  the lock is held, then completes after release — proves a second process blocks
  while a writer holds the lock.
- `test_AppContainerLockFailure`: the lock name is pre-occupied by a different
  object type (an event), so `NtCreateMutant` fails; register and unregister both
  return an error (not success, not `ALREADY_EXISTS`) and a direct registry probe
  confirms **no leaf was created** — proves a lock-acquisition failure leaves the
  registry untouched.

## R2 — test-runner isolation & reproducibility (zero broad pkill)

Isolation: the owned runner (`ac_run.sh`) shuts down Wine **only** for its own
prefix via `WINEPREFIX=$PREFIX wineserver -k0`. It never issues a name/pattern
`pkill` of `wineserver`/`explorer`/`services`, so no other worker's prefix or
process can be touched. The old broad-`pkill` runner (`p26.sh`) is **not** run.

Foreign-sentinel isolation regression: a throwaway foreign prefix
(`/root/wine-ac-foreign-sentinel`) was initialized and given a long-lived client;
the foreign wineserver was identified by reading `/proc/<pid>/environ` for that
exact `WINEPREFIX`. Our scoped cleanup (`WINEPREFIX=$OURS wineserver -k0`) left the
foreign wineserver **alive** → **PASS** (isolation holds). The throwaway prefix and
its processes were then torn down with a scoped, prefix-matched kill (no broad
pkill) and removed.

Reproducibility of the delivered source:
- **Byte-identity.** `git diff 44c8989 -- dlls/kernelbase/security.c
  dlls/kernelbase/tests/security.c` is empty and `git status --short --
  dlls/kernelbase/` is clean, so the kernelbase source compiled in the owned build
  tree is byte-identical to the committed source. The passing binaries were built
  from the committed source, not from uncommitted edits.
- **Exact build inputs** (owned out-of-tree tree `/root/wine-ac-build2`,
  srcdir = the wine worktree):
  - configure: `./configure --enable-archs=i386,x86_64 --prefix=/root/wine-ac-build2/install`
    (Wine 11.19)
  - toolchain: gcc (Debian 14.2.0-19) 14.2.0, GNU Make 4.4.1; cross compilers
    `i686-w64-mingw32-gcc` and `x86_64-w64-mingw32-gcc`.
  - build: `make -j16` (incremental recompile of `security.o` for impl+test, both arches).
- **Loaded module paths + input hashes** (this run):

  | artifact | path | sha256 |
  |----------|------|--------|
  | x86_64 kernelbase.dll | `/root/wine-ac-build2/install/lib/wine/x86_64-windows/kernelbase.dll` | `2ea8862e0247e8232f39433a711ecb3dac7ca24e028f5cefeca6a3690b07a472` |
  | i386 kernelbase.dll | `/root/wine-ac-build2/install/lib/wine/i386-windows/kernelbase.dll` | `01cfa57ba50e570db2d7cdd8286b916debc8f8d0768c74abeb89d6b014d77888e` |
  | x86_64 test exe | `/root/wine-ac-build2/dlls/kernelbase/tests/x86_64-windows/kernelbase_test.exe` | `de095cc995085a3b37e40be58f0c7ddb686e599786a4aa1a6078726dada2f644` |
  | i386 test exe | `/root/wine-ac-build2/dlls/kernelbase/tests/i386-windows/kernelbase_test.exe` | `76e3f62075fcc5181cd16b12e9e5bf43e030be351157bb73e287a245fc61733a` |

  (Hashes identify this build's inputs; Wine embeds build metadata, so they are
  not a cross-build constant. The reproducibility claim rests on byte-identical
  source + the recorded configure/toolchain/make args above.)

**Pristine-archive finding (honest limitation, pre-existing, not from this change).**
A fully isolated build from `git archive 44c8989` **cannot complete `configure`**:
`tools/makedep` fails during `config.status` because the committed tree references
local graphics headers that were never committed to this branch — e.g.
`wine/dcomp_underlay.h` (included by the pre-existing `dlls/dcomp/device.c`) and,
via the committed `include/Makefile.in`, public `wex*.h` headers whose files are
only present as untracked local files in the fork's graphics baseline. This is a
pre-existing property of the fork's graphics work and is **unrelated to and not
required by** the kernelbase AppContainer change: neither `security.c` nor
`tests/security.c` includes any such header (they use only standard Wine/NT
headers). Fixing it would mean committing graphics files or altering the fork's
build system, which is out of scope for this work order. Per R2's option (b), the
minimal prerequisite is therefore **clearly separated and documented** here rather
than quietly used: the build environment carries the fork's local graphics headers;
the AppContainer change itself has no dependency on them. The kernelbase source was
built and tested in the owned tree that contains those documented prerequisites,
with byte-identity to the committed source verified above.

## R3 — legitimate derived SIDs, robust child lifecycles & race tests

Test identities are **package SIDs derived at runtime** via
`DeriveAppContainerSidFromAppContainerName` from unique per-run container names
(`"<tag>-<tickcount>-<pid>"`). No hardcoded `S-1-5-21-...` test SIDs remain and no
foreign/derived identity is ever unregistered.

Child processes are launched with `CreateProcessW`, tracked by handle, awaited with
**bounded** timeouts, and reaped by an **idempotent** helper (wait → terminate on
timeout → reap → close both handles exactly once). There are no sleep-based
readiness barriers: the concurrent test uses a manual-reset named event so two real
children are released together deterministically. Required cases that fail to
execute count as **failures**, not skips.

Coverage (all under `START_TEST(security)`):
- lifecycle: fresh register → exact Unicode round-trip; independent-allocation check
  (two lookups return distinct pointers, both freed); duplicate → `ALREADY_EXISTS`
  with original intact; second SID isolation; unregister leaves the other mapping;
  double-unregister / missing lookup → `FILE_NOT_FOUND` with output set to NULL.
- corrupt stored value: REG_DWORD, unterminated (explicit 4-byte `{ 'A',0,'B',0 }`),
  and odd-length REG_SZ each rejected as `ERROR_INVALID_DATA` with NULL output.
- abandoned-writer repair: pre-created empty leaf is repaired to a complete mapping.
- cross-process persistence: child process looks up a parent-registered moniker via
  normal builtin loading.
- deterministic concurrent same-SID publication: exactly one child `S_OK`, the other
  `HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)` (`0x800700b7`); post-race lookup is valid.
- blocked-reader serialization and lock-acquisition-failure (see R1).
- invalid/NULL inputs handled safely.

**Results — both architectures, fresh build from committed source:**

| arch | tests executed | todo | flaky | failures | skipped | exit |
|------|----------------|------|-------|----------|---------|------|
| x86_64-windows | 49 | 0 | 0 | **0** | **0** | 0 |
| i386-windows | 49 | 0 | 0 | **0** | **0** | 0 |

Observed child traces (both arches): persistence lookup `hr=0 moniker="PersistMoniker"`;
concurrent race winner `hr=0` / loser `hr=0x800700b7`; blocked-reader publication
`hr=0 moniker="PubMoniker"`.

## R4 — storage error propagation & value validation

- `AppContainerRegisterSid` returns `ERROR_ALREADY_EXISTS` **only** when the key
  disposition was `REG_OPENED_EXISTING_KEY` and the existing mapping is complete and
  valid; real `NtCreateKey` failures are propagated faithfully (not conflated with
  already-exists). A failed second value publish rolls back only the newly owned
  incomplete leaf (`NtDeleteKey`) and returns the actual error.
- `ac_read_moniker_value` sets `*moniker=NULL` up front, queries with a real buffer
  (two-pass grow on `STATUS_BUFFER_OVERFLOW`), and validates before allocating:
  type must be REG_SZ; byte length even, ≥ `sizeof(WCHAR)`, ≤ bound; and the final
  WCHAR must be NUL. Malformed/unterminated values return `ERROR_INVALID_DATA` with
  `*moniker=NULL`.
- `AppContainerLookupMoniker` opens HKCU with **KEY_READ** only (write/create rights
  are requested only by mutating paths); the real NTSTATUS is preserved rather than
  collapsed to `ACCESS_DENIED`. Missing key → `FILE_NOT_FOUND`, output NULL.

HKCU access uses `RtlOpenCurrentUser` (this fork's ntdll rejects the raw HKCU
pseudo-handle for Nt key APIs), and all registry I/O uses the Nt* API set (kernelbase
is below advapi32).

## Limitations

- The registration is metadata only; it enforces no token/capability/ACL boundary.
- A pristine-archive full-tree `configure` is blocked by pre-existing uncommitted
  fork graphics headers (see R2); the AppContainer change is independent of them and
  was built/tested in the owned tree with those documented prerequisites present.
- Test SIDs are derived per-run and cleaned up; no persistent identities beyond the
  test's own mappings remain, and no arbitrary host registry keys are touched.

## Raw logs (private, D: only — not committed)

- `D:\DeepThought\Projects\OpenClaude\ac_build\ac_x64_stdout.log`, `ac_i386_stdout.log`
  (this run's per-arch test output; each ends "49 tests executed … 0 failures), 0 skipped").
- `ac_run.sh` reproducibility manifest, `ac_evidence.log` (sentinel + build facts),
  `ac_clean_configure.log` / `ac_diag.sh` output (pristine-archive makedep finding).
