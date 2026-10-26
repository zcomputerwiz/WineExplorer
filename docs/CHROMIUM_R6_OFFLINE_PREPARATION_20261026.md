<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium R6 offline preparation — registration metadata sanity probe

**Status: `OFFLINE_CODE_READY_ARTIFACTS_PENDING`**

All R6 code, tests and the two native PEs are complete and verified offline. The
final guest artifact inventory (the Wine-built `userenv.dll`/`kernelbase.dll`
modules as staged inside the VM) is intentionally **not** recorded here: it waits
for Luna's reviewed build. No module hashes are fabricated — the only hashes in
this document are of artifacts actually produced by this offline preparation and
of the source files consumed to produce them.

Spec: `docs/CHROMIUM_R6_REGISTRATION_PLAN_20261026.md`.

## Scope and constraints (offline only)

Zero VM boot, zero SSH, zero live browser/Wine/probe execution, zero new Wine
DLL builds, zero token imports. The two native PEs were cross-compiled on the
host with the mingw toolchain (a host compile of a small original C++ source),
which is permitted; they were **not executed**, and no Wine/guest/browser/mount
was run. `native_PE_executed: false` and `Wine_executed: false` are recorded in
the build receipt.

## Source base

- Branch: `codex/chromium-r6-preparation` (based on `20389e43`).
- Worktree: `D:/DeepThought/Worktrees/chromium-r6-preparation/WineExplorer`.
- Test environment: WSL Debian, Python 3.13.5.
- Compile toolchain: `x86_64-w64-mingw32-g++ (GCC) 14-win32`
  (`compiler_sha256 fb69bd14…d895b`).

## Deliverable 1 — R6 mode / paths / CLI

`scripts/check-g4-chromium-native-exit.py` gains a fixed `userenv-r6` mode and
a `--userenv-r6` selector. All historical modes (`r1`, `module-warn-r2`,
`userenv-r3`, `userenv-r4`) are preserved byte-for-byte; only the new mode adds
behavior.

| Path | Value (LAB = `/home/debian/wex-g4`) |
|------|-------------------------------------|
| HOST   | `chromium-registration-r6-20261026` |
| STAGE  | `LAB/chromium-registration-r6-20261026-stage` |
| OUTPUT | `LAB/chromium-registration-r6-20261026` |
| PROFILE| `LAB/chromium-registration-r6-20261026-profile` (a **sibling** of OUTPUT) |

A new module-level `PROFILE` global is introduced. For every pre-existing mode
it equals `OUTPUT/'profile'` (identical to the previous inline path), so existing
behavior is unchanged; only `userenv-r6` uses the sibling form, exactly as the
work order specifies. The compositor child-only WINEPREFIX stripping
(`strip_wineprefix: True`) now also applies to `userenv-r6`, matching R4.

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

## Deliverable 3 — pre-browser metadata sanity probe

A new bounded C/native PE (`scripts/g4_userenv_r6_probe.cpp`) runs inside the
existing 45 s API-probe window, before browser launch. It dynamically resolves
the five exports it needs (no link-time dependency on them):

- `kernelbase.dll`: `AppContainerRegisterSid`, `AppContainerUnregisterSid`,
  `AppContainerLookupMoniker`, `AppContainerFreeMemory`
- `userenv.dll`: `DeriveAppContainerSidFromAppContainerName`

It drives a full registration-metadata lifecycle for a fresh, nonce-derived
identity (name ≤ 64 WCHARs, moniker `wex-r6-<name>`) and performs **21 checks**:

1. normal builtin path for `userenv.dll`; 2. normal builtin path for
   `kernelbase.dll`; 3–7. the five export resolutions are non-null;
   8–10. `Derive…` rejects NULL name / empty name / NULL out with
   `E_INVALIDARG` (out untouched); 11. derive succeeds → valid SID;
   12. SID has exactly 8 sub-authorities; 13. identifier authority is
   `{0,0,0,0,0,15}` and `subauthority[0]==2` (i.e. `S-1-15-2-…`);
   14. initial lookup → absence (`0x80070002`); 15. register → `S_OK`;
   16. lookup → `S_OK`, non-null; 17. round-trips the exact moniker;
   18. duplicate register → `0x800700b7`; 19. original registration intact;
   20. unregister → `S_OK`; 21. final lookup → absence (`0x80070002`).

Every check is null-guarded so a missing export records a failure rather than
crashing. Looked-up monikers are freed with the matching `AppContainerFreeMemory`
and the derived SID with `FreeSid`. The PE emits a strict JSON record
(`checks==21`, `failures==0`, `passed:true`) and exits 0 only on full pass.

The Python phase (`scripts/g4_userenv_r6_probe.py`) reuses the reviewed 45 s
API-probe/reap/intermediate-cleanup timing from `g4_userenv_probe.phase` verbatim
via a new `decoder=None` keyword argument (default keeps R3/R4 behavior
byte-identical); only the decoded evidence schema differs. The runner gates
browser launch on probe failure: on any probe exception the run stops before
`FixtureServer.start()` and records a `PRIMARY …` error, with no browser cleanup
marker.

## Deliverable 4 — native PEs (host cross-compile, not executed)

Built into `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/userenv-browser-r6-20261026.build/`
reusing the reviewed R4/R5 recipe read-only. Both are AMD64 (`machine 34404`).
The reference original helper PE
(`native-exit-helper-r1-20261023.build/native-exit.exe`) was verified unchanged
(before == after == `366c6928…713b8d60`).

| Artifact | sha256 | bytes | machine |
|----------|--------|-------|---------|
| `userenv-browser-r6-20261026.exe` (parent) | `3fffb735 31aa1de6 cb870aaa 8c612463 51f96a6b bfef0c0f 9465358c d17c3bba` | 306537 | 34404 |
| `userenv-r6-probe.exe` (probe) | `24de50a9 3f5c6f86 7891906a 3671deda 336fe4c1 24f21dad 1309540d df44b5b3` | 300011 | 34404 |

Parent override (`output-only.h`, `override_sha256 7f6c6e91…476f1b46fe`):
`#define WEX_EXIT_OUTPUT L"Z:\home\debian\wex-g4\chromium-registration-r6-20261026"`.
Both compiles passed with `-Wall -Wextra -Werror` and empty stderr. Consumed
source hashes (see `build-results.json` in that build dir):
`g4_native_exit.cpp 8eb2bcf8…953b64fa6`, `g4_native_exit_contract.h
9ed90526…222d60bd`, `g4_userenv_r6_probe.cpp db8218f5…cf36deb10`.

> Note: the host toolchain is not byte-reproducible across runs, so a rebuild of
> the parent PE may yield a different hash. The hashes above are the real bytes
> produced by this preparation; the authoritative artifact inventory is Luna's
> reviewed guest build.

## Deliverable 5 — offline tests (100% pass)

Run in WSL Debian (Python 3.13.5), `python3 -B -m unittest`:

- `test_g4_userenv_view` — **Ran 15 tests … OK** (0 failures / 0 errors / 0 skips).
  New: r6 readonly view covers userenv+kernelbase for both arches; r6 CLI/path
  selection; strict r6 probe decode negatives; r6 probe-failure-before-browser
  gating and success ordering. The pre-existing `test_host_real_result_and_strict_json_negatives`
  reads the genuine 2026-10-24 `host-probe.stdout.json` from the lab (authentic
  evidence, not fabricated).
- `test_g4_chromium_userenv_r6_adapter` — **Ran 4 tests … OK** (0 failures / 0
  errors / 0 skips). Self-contained (no dependency on private VM-adapter
  artifacts, so it never skips): exact fresh r6 paths with no historical-path
  collision; in-repo source inventory; native-PE check-count consistency with the
  Python decoder.

Total: **19 tests, 0 failures, 0 errors, 0 skips.**

## Profile-coupling tension — FLAG for parent resolution before the live gate

Deliverable 1 sets the r6 `PROFILE` to a **sibling** of OUTPUT
(`LAB/<HOST>-profile`). Deliverable 4 reuses the R4/R5 recipe read-only, so the
parent PE is built with `WEX_EXIT_OUTPUT` = the Windows path of OUTPUT, and the
reviewed contract (`g4_native_exit_contract.h`) derives `--profile` as
`WEX_EXIT_OUTPUT + "\profile"` — i.e. **nested** under the output dir — and
requires that nested profile to be fresh/absent at runtime.

At **live-run** time these two disagree: the Python runner would pass the sibling
profile while the native helper's contract expects the nested one. This is outside
offline scope (no live run was performed). Both sides are implemented exactly as
their respective deliverables specify, and `g4_native_exit_contract.h` was **not**
modified offline (it is the reviewed R1–R5 contract). Before the live gate the
parent must reconcile this — either point the r6 output-only override at a dir
whose nested `\profile` equals the intended sibling path, or add an r6 variant of
the contract's profile derivation.

## Remaining live checks (deferred to post-Luna reviewed build)

Guest path absence, guest free space, actual module-load from the staged RO views,
the real 21-check probe result against the VM's `kernelbase.dll`/`userenv.dll`,
and actual VirGL rendering all remain future live observations, not offline
passes. The final artifact inventory (guest-side module hashes) is recorded only
after Luna's reviewed build.
