<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium R6 offline preparation — registration metadata sanity probe

**Status: `PREPARED_NOT_RUN`**

All R6 code, tests and the two native PEs are complete and verified offline, and
the genuine artifact inventory is now recorded: the four accepted Wine-built
`userenv.dll`/`kernelbase.dll` modules (Luna's reviewed clean build), plus the
host cross-compiled parent helper PE and the recompiled registration-metadata
probe PE. Every hash below is of bytes actually present on disk at preparation
time — none are fabricated. The preparation is **ready for the parent's
authorized live gate but has NOT been run**: no VM boot, no SSH, no live
browser/Wine/probe execution, and `native_PE_executed: false` / `Wine_executed:
false` remain recorded in the build receipt.

Spec: `docs/CHROMIUM_R6_REGISTRATION_PLAN_20261026.md`.
Reconciled against the parent rework
(`CHROMIUM_R6_PREPARATION_REVIEW_REWORK_20261026.md`): the three bounded
corrections (profile-path contract, probe lifecycle/ownership hardening, and this
genuine inventory) are all applied.

## Scope and constraints (offline only)

Zero VM boot, zero SSH, zero live browser/Wine/probe execution, zero new Wine
DLL builds, zero token imports. The two native PEs were cross-compiled on the
host with the mingw toolchain (a host compile of a small original C++ source),
which is permitted; they were **not executed**, and no Wine/guest/browser/mount
was run. `native_PE_executed: false` and `Wine_executed: false` are recorded in
the build receipt.

## Source base

- Branch: `codex/chromium-r6-preparation`.
- Worktree: `D:/DeepThought/Worktrees/chromium-r6-preparation/WineExplorer`.
- Test environment: WSL Debian, Python 3.13.5.
- Compile toolchain: `x86_64-w64-mingw32-g++ (GCC) 14-win32`
  (`compiler_sha256 fb69bd149cff01c8d63a42c223c4ca4d8a3913cbfdcd41e1df25c8b321bd895b`).

## Deliverable 1 — R6 mode / paths / CLI (profile contract aligned)

`scripts/check-g4-chromium-native-exit.py` gains a fixed `userenv-r6` mode and
a `--userenv-r6` selector. All historical modes (`r1`, `module-warn-r2`,
`userenv-r3`, `userenv-r4`) are preserved; the module-level `PROFILE` global is
now **nested under OUTPUT for every mode** (`OUTPUT/'profile'`), which aligns the
Python runner with the native helper's contract derivation
(`WEX_EXIT_OUTPUT + L"\profile"`). This resolves the earlier profile-coupling
tension (see below) — there is no longer a sibling-vs-nested disagreement.

| Path | Value (LAB = `/home/debian/wex-g4`) |
|------|-------------------------------------|
| HOST   | `chromium-registration-r6-20261026` |
| STAGE  | `LAB/chromium-registration-r6-20261026-stage` |
| OUTPUT | `LAB/chromium-registration-r6-20261026` |
| PROFILE| `OUTPUT/'profile'` = `LAB/chromium-registration-r6-20261026/profile` (**nested**) |

The compositor child-only WINEPREFIX stripping (`strip_wineprefix: True`) applies
to both `userenv-r4` and `userenv-r6`, matching R4. Because OUTPUT is fresh for
the r6 host, the nested profile is guaranteed fresh/absent at run time, exactly
as the reviewed contract requires.

## Deliverable 2 — readonly module view generalization

`scripts/g4_userenv_view.py` gains `userenv-r6`. For that mode `selected()`
stages **both** builtin modules across **both** architectures (four RO bind
views total); all other modes keep staging `userenv` only:

- `userenv.dll`  — `x86_64-windows` (machine `0x8664`) and `i386-windows` (`0x14c`)
- `kernelbase.dll` — `x86_64-windows` (`0x8664`) and `i386-windows` (`0x14c`)

Each is a read-only bind view in the private mount namespace: per module,
`mount --bind <builtin> <stage>` then `mount -o remount,bind,ro <stage>`, with
the BUILD root itself RO-bound first. No shared install/prefix is overwritten;
the builtin sources are left byte-identical (asserted in the test). The fixed
exec argv ends with `--userenv-r6 --output-dir <OUTPUT>`.

## Deliverable 3 — pre-browser metadata sanity probe (hardened lifecycle)

A bounded C/native PE (`scripts/g4_userenv_r6_probe.cpp`) runs inside the
existing 45 s API-probe window, before browser launch. It dynamically resolves
the five exports it needs (no link-time dependency on them):

- `kernelbase.dll`: `AppContainerRegisterSid`, `AppContainerUnregisterSid`,
  `AppContainerLookupMoniker`, `AppContainerFreeMemory`
- `userenv.dll`: `DeriveAppContainerSidFromAppContainerName`

It drives a full registration-metadata lifecycle for a fresh, nonce-derived
identity. The **same prefixed moniker** (`wex-r6-<nonce>`, ≤ 64 WCHARs) is used
for both SID derivation and registration; the raw `nonce` hex is preserved in the
JSON record. Hardened ownership/mutation semantics:

- **Fail-before-mutation:** the initial lookup must return absence
  (`HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)` = `0x80070002`) with a NULL
  moniker; any other result aborts immediately with **no register and no
  unregister**.
- **Strict fresh ownership:** `owned` is set only when the *fresh* registration
  returns `S_OK`; a duplicate can never confer ownership. A separate `held` flag
  tracks whether this run still holds the identity at exit.
- **Finally-style cleanup:** after the lifecycle, an unregister runs **only if**
  `held` is still true (i.e. we registered but aborted before the planned
  teardown). The outcome is recorded as `cleanup_attempted` /
  `cleanup_succeeded`.

The PE performs **21 checks**:

1. normal builtin path for `userenv.dll`; 2. normal builtin path for
   `kernelbase.dll`; 3–7. the five export resolutions are non-null;
   8–10. `Derive…` rejects NULL name / empty name / NULL out with
   `E_INVALIDARG` (out untouched); 11. derive (from the moniker) succeeds →
   valid SID; 12. SID has exactly 8 sub-authorities; 13. identifier authority is
   `{0,0,0,0,0,15}` and `subauthority[0]==2` (i.e. `S-1-15-2-…`);
   14. initial lookup → absence (`0x80070002`); 15. fresh register → `S_OK`;
   16. lookup → `S_OK`, non-null; 17. round-trips the exact moniker;
   18. duplicate register → `0x800700b7`; 19. original registration intact;
   20. planned-teardown unregister → `S_OK`; 21. final lookup → absence
   (`0x80070002`).

Every check is null-guarded so a missing export records a failure rather than
crashing. Looked-up monikers are freed with the matching `AppContainerFreeMemory`
and the derived SID with `FreeSid`. The PE emits a strict **v=2** JSON record and
exits 0 only on full pass (`checks==21`, `failures==0`). Beyond the pass/fail
fields it records genuine per-step HRESULTs (`derive_hr`, `initial_lookup_hr`,
`register_hr`, `post_register_lookup_hr`, `duplicate_register_hr`,
`verify_lookup_hr`, `unregister_hr`, `final_lookup_hr`) and ownership/cleanup
flags (`register_attempted`, `owned`, `unregister_attempted`,
`cleanup_attempted`, `cleanup_succeeded`).

The Python phase (`scripts/g4_userenv_r6_probe.py`) reuses the reviewed 45 s
API-probe/reap/intermediate-cleanup timing from `g4_userenv_probe.phase` verbatim
via a `decoder=None` keyword argument (default keeps R3/R4 behavior
byte-identical); only the decoded evidence schema differs. The v=2 decoder
enforces, in addition to the exact key set and types: (a) no ownership without a
fresh `S_OK` registration; (b) no release/cleanup without ownership; (c) no
mutation of a non-absent identity; and (d) the full canonical HRESULT sequence.
The runner gates browser launch on probe failure: on any probe exception the run
stops before `FixtureServer.start()` and records a `PRIMARY …` error, with no
browser cleanup marker.

## Deliverable 4 — native PEs (host cross-compile, not executed)

Built into `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/userenv-browser-r6-20261026.build/`
reusing the reviewed R4/R5 recipe read-only. Both are AMD64 (`machine 34404`).
The reference original helper PE
(`native-exit-helper-r1-20261023.build/native-exit.exe`) was verified unchanged
(before == after == `366c69286a7eb6469e4bfd9921643202b746fc987de46e9f9ded2bf6713b8d60`).

| Artifact | sha256 | bytes | machine |
|----------|--------|-------|---------|
| `userenv-browser-r6-20261026.exe` (parent) | `433841b75e11b82035ec9f5602e1af4dcd2e165f39819da3e4f20c8135df5ad7` | 306537 | 34404 |
| `userenv-r6-probe.exe` (probe) | `8558081c1211a7d0298bf33b2fa0a9ef62d4b3b3079439fbb4efa398ea962c4d` | 301064 | 34404 |

The probe PE was **recompiled** with the hardened lifecycle from Deliverable 3
(source `g4_userenv_r6_probe.cpp`, sha256
`22bfa475e9fb659fb3e5fc03b930d18d442eff9370a6c50825573d18f3fb9e14`). The parent
helper source (`g4_native_exit.cpp`) is **unchanged** in this rework; its hash
here reflects the current on-disk bytes.

Parent override (`output-only.h`, `override_sha256
7f6c6e913083e749d386bb92916450878a6f4995ddc1caf202fe9c476f1b46fe`):
`#define WEX_EXIT_OUTPUT L"Z:\home\debian\wex-g4\chromium-registration-r6-20261026"`.
Both compiles passed with `-Wall -Wextra -Werror` and empty stderr. Full consumed
source hashes are in `build-results.json` in that build dir.

> Note: the host toolchain is not byte-reproducible across runs, so a rebuild of
> unchanged source may yield a different PE hash (identical logic). The hashes
> above are the real bytes present at preparation time and are what the live gate
> deploys — it does **not** rebuild these PEs.

## PREPARED_NOT_RUN artifact inventory (genuine, on-disk)

The four guest modules are Luna's accepted clean build, staged read-only into the
fresh r6 STAGE at run time. Their authoritative hashes (re-verified against the
bytes on disk):

| Module | arch (machine) | sha256 | bytes | source path (WSL) |
|--------|----------------|--------|-------|-------------------|
| `kernelbase.dll` | x86_64 (34404) | `147fb181ff8ce1eb13aa2bb166e053f40c26191662d1b447247fdfd1e720aa24` | 7542180 | `/var/tmp/wex-g4-ac-parent-20261026/build/dlls/kernelbase/x86_64-windows/kernelbase.dll` |
| `kernelbase.dll` | i386 (332) | `261a2fd0dfa4b04f6b3c16a122f3efbc3a703bb0d078cea8ab552d1476c5f0ed` | 6446162 | `/var/tmp/wex-g4-ac-parent-20261026/build/dlls/kernelbase/i386-windows/kernelbase.dll` |
| `userenv.dll` | x86_64 (34404) | `71de0bea2e3b85a9afcddf7dcdcc40a31b204605530b4d83e0e2cf08e2bf04ba` | 196841 | `/var/tmp/wex-g4-ac-parent-20261026/build/dlls/userenv/x86_64-windows/userenv.dll` |
| `userenv.dll` | i386 (332) | `02f1f5b914b8aa0ba846cf5c04fb616c8aaf6a2563cfeeb5a162b4c151db8313` | 170966 | `/var/tmp/wex-g4-ac-parent-20261026/build/dlls/userenv/i386-windows/userenv.dll` |

Plus the two host cross-compiled PEs from Deliverable 4 (parent
`userenv-browser-r6-20261026.exe`, probe `userenv-r6-probe.exe`). These six
artifacts constitute the complete, immutable preparation the parent reviews
before authorizing the live run.

## Live gate — D:-backed WSL worker launch / run / stop (documented, NOT executed)

The same exact-owned runner pattern reused from R5 drives all three phases in a
single invocation on the WSL Debian worker, where LAB `/home/debian/wex-g4` is
the D:-backed mount (`D:\VirtualBox_VMs\WEX-G4-VirGL-20261022`). **None of this
was executed offline**; it is documented so the preparation is complete and
reviewable for the parent's authorized gate.

**Run (single command, from the repo on the worker):**

```
python3 -B scripts/check-g4-chromium-native-exit.py --userenv-r6
```

Internally this one invocation performs:

- **Launch:** build the private environment (`WINEPREFIX`, `WINEDEBUG`), configure
  the VirGL GL driver, start the owned compositor
  (`OwnedWexcomp(LAB/labwc, LAB/wayland, …, 'gles2', strip_wineprefix=True)`), and
  verify the outer GPU renderer.
- **Run:** execute the pre-browser R6 metadata probe
  (`g4_userenv_r6_probe.phase(… STAGE/original/userenv-r6-probe.exe …)`) within the
  45 s allowance; on success start the private-loopback fixture server and launch
  the native browser via `cal.collect(…)`, observing native terminal status, HTTP
  HTML/JS, readiness and front pixels. On any probe failure it stops before
  `FixtureServer.start()` (no browser).
- **Stop / cleanup:** parse the flushed journal, capture one optional front, run
  prefix teardown, `wineserver -k` then `-w`, close the fixture server, stop the
  compositor, and re-verify no remaining servers/prefix processes and that private
  loopback is restored — recording `cleanup_complete`.

The probe PE and parent helper PE are staged into the fresh r6 STAGE before this
run; they are deployed as the recorded bytes above (no rebuild).

## Deliverable 5 — offline tests (100% pass)

Run in WSL Debian (Python 3.13.5), `python3 -B -m unittest`:

- `test_g4_userenv_view` — **Ran 15 tests … OK** (0 failures / 0 errors / 0 skips).
  New/updated: r6 readonly view covers userenv+kernelbase for both arches; r6
  CLI/path selection asserting the nested `PROFILE == OUTPUT/'profile'`; strict r6
  probe decode negatives against the v=2 schema; r6 probe-failure-before-browser
  gating and success ordering. The pre-existing
  `test_host_real_result_and_strict_json_negatives` reads the genuine
  2026-10-24 `host-probe.stdout.json` from the lab (authentic evidence, not
  fabricated).
- `test_g4_chromium_userenv_r6_adapter` — **Ran 5 tests … OK** (0 failures / 0
  errors / 0 skips). Self-contained (no dependency on private VM-adapter
  artifacts, so it never skips): exact fresh r6 paths with no historical-path
  collision; in-repo source inventory; native-PE check-count consistency with the
  Python decoder; and a new **lifecycle sequence + ownership-invariant** test
  covering the canonical success plus each failure mode (initial collision → no
  mutation/no unregister; failed fresh registration → no unregister; post-register
  lookup failure → owned cleanup runs; cleanup failure → fail) and four safety
  invariants (no ownership without a fresh `S_OK`; no release/cleanup without
  ownership; no mutation of a non-absent identity).
- `test_g4_chromium_native_exit` — **Ran 11 tests … OK** (0 failures / 0 errors /
  0 skips). The runner's own module, re-run because Deliverable 1 changed the
  shared `PROFILE` global; confirms the nested-profile contract did not regress
  any historical mode's CLI/path/logging behavior.

Total: **31 tests, 0 failures, 0 errors, 0 skips** (view 15 + adapter 5 +
native-exit runner 11).

## Profile-coupling tension — RESOLVED

The earlier draft set the r6 `PROFILE` to a *sibling* of OUTPUT while the native
helper's contract derives `--profile` as `WEX_EXIT_OUTPUT + "\profile"` (nested).
Per the parent rework, Deliverable 1 now sets `PROFILE = OUTPUT/'profile'` for
**all** modes, so the runner and the native helper agree on the nested path. With
the r6 OUTPUT fresh, that nested profile is absent at run time as required. No
change to the reviewed `g4_native_exit_contract.h` was needed.

## Remaining live checks (deferred to the parent's authorized gate)

Guest path absence, guest free space, actual module-load from the staged RO views,
the real 21-check probe result against the VM's `kernelbase.dll`/`userenv.dll`,
and actual VirGL rendering all remain future **live** observations, not offline
passes. The artifact inventory is now recorded above; only the authorized live run
itself is deferred.
