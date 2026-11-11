<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1 execution plan and dispatch ledger

Updated 2026-11-11. This schedules the existing roadmap in `README.md`; it does
not replace its scope, default-off gate, provenance policy or Uno decision.

## Current authorization and ownership (2026-11-11)

The owner authorizes continued work until the feature is complete: Codex plans,
reviews and handles difficult lab setup; Qwen and Antigravity implement. This
supersedes historical assignments requiring Claude review or excluding Codex
from review. Implementers still do not stage/commit/push or touch another
owner's edits. Codex reviews each package before accepting and committing it.
U1.0 is the next acceptance gate, followed by the existing lifecycle/presentation
and later gates; passing a preparatory probe is not completion of UWP support.

Codex established reliable Debian access through direct Node subprocess argv
on this date (`wsl.exe --distribution Debian --user root --exec /bin/echo ...`).
Earlier blank-output reports do not establish a current access denial. Setup
remains isolated from the elevation guests and wexbuild. Runtime work will use
clean pinned, separate sources rather than the dirty shared Wine checkout.

## Review repairs now assigned

* U1-M2R (Qwen), only `tests/uwp/identity/`: refuse existing output state rather
  than recursively deleting arbitrary `-OutDirectory`; use fresh owned output
  and fault-check directories, reject reparse/alias escapes, retain previous
  evidence. Validate positive finite run bounds. The current driver calls
  `Remove-Item -Recurse` on the caller's output and the fault checker deletes
  its `fault` child without verifying ownership. Add preservation/refusal checks.
  Correct the Rtl comments: there is no callable implementation behind the
  currently false flag; do not claim toggling it enables a complete ABI probe.
* U1-M1R (Antigravity), only `tests/uwp/u1_0/run-wine.sh`,
  `test-run-wine.sh`, `RUNNER_CHECK.md`: fail on nonzero/timed-out wineboot even
  if the marker exists; validate bounds and add kill-after deadlines; require
  a real matching wineserver path (Wine build trees use `server/wineserver`),
  use bounded cleanup of the owned prefix and fail/retain state if cleanup
  fails. Never treat missing process-inspection tools as successful cleanup,
  remove a prefix while the server remains, or delete a changed ownership lock.
  Use atomic fresh-directory creation for results and cover these failure paths.

Both repairs are implementation and test work only, no Wine runtime, WSL or
guest access. Codex reviews them while setting up the owned guest.

## Baseline inspected

* WineExplorer `master`: `91464b0e66f9a3a4b291095dde517d99a37e3b05`.
* Recorded gitlink and checked-out Wine `development`:
  `6cc8dce207274ddde5577be45a06cc6e2bb2e356`.
* Both trees have unrelated elevation edits. They are not U1 inputs or ours to
  stage, reset, sync, build or deploy. A lab build must use clean pinned sources.
* Fixture sources live in `tests/uwp/u1_0/` today. Separate fixture/tools repos
  remain proposed; creating them is not needed for these local work orders.
* Historical evidence: `BUILD_CHECK.md` records MSVC x64/x86 builds (2026-11-07);
  `ORACLE_RESULTS.md` records ten x64 Windows checks passing. Windows x86
  packaging/activation remains not run. These historical runs were not repeated
  in this inspection, and their raw artifact hashes have not been reverified.
* `WORK_ORDERS.md` and commit `063a5c7f` record Wine x64/x86 unpackaged control
  success (2026-11-10). The tracked `actual-wine-*.txt` contain only a PASS line,
  not complete source/build/prefix/hash evidence. Packaged Wine cases are NOT-RUN.
* Current source still returns `APPMODEL_ERROR_NO_PACKAGE` from
  `wine/dlls/kernelbase/version.c` current-package queries and `STATUS_NOT_FOUND`
  from `RtlQueryPackageIdentity` in `wine/dlls/ntdll/rtl.c`. No working packaged
  identity or UWP lifecycle support is established by the unpackaged control.
* MinGW omits the positive `async_folder` check (`probe.cpp`, `_MSC_VER` guard).
  It is suitable for the unpackaged control, not equivalent to the full oracle.

## Ordered delivery

| Stage | Owner | Deliverable / exit gate |
|---|---|---|
| U1-M1, now | Qwen | Reliable, bounded unpackaged runner; fake-loader regression checks; no live Wine or shared lab access. |
| U1-M2, independent | Qwen | Separate native unpackaged identity-observation probe, x64/x86 MSVC build/run and structured evidence; no Wine/runtime or lab dependencies. |
| U1-L1, parallel | Antigravity | Read-only lab preflight, then an exclusively owned guest only if safe; source, disk, port and cleanup ownership recorded. |
| Evidence closure | Antigravity, after L1 | Pin and hash the same MSVC-built fixture bits; recover/sanitize existing x64 oracle evidence, then fill the x86 oracle gap with explicit certificate/trust ownership. Do not execute this follow-up under L1. |
| U1 baseline | Qwen, after M1/L1 | Build clean pinned WoW64 sources in the owned guest; run both unpackaged controls and ordinary x86/x64 Win32 smoke tests. Record full commands, hashes, logs and exit status. |
| U1 design gate | Codex; existing Claude review | Specify per-prefix package record, trusted launch/context transfer, token/process identity consistency, package-local activation and dependency closure; review ABI/security and stock-change inventory before runtime edits. |
| U1 implementation | Bounded later orders | Store/rollback, context/identity, then resolver/activation; no changes to elevation token/server paths while their owner is active. |
| U1.0 acceptance | Existing Claude review | Same unchanged packaged fixture bits, positive identity/activation/async plus missing-class, missing-DLL, unpackaged and dependency negatives; off/default/malformed gate controls; clean-prefix x86/x64 regressions. |
| U1.1 and later | Separate orders | Lifecycle/CoreWindow/CoreDispatcher and DXGI target first; MRT/PRI and dependency closure early; XAML and conditional private application trials remain later gates. |

No runtime gate is passed by a document, a fake-loader test, a compile, or an
unpackaged failure matching Windows. Do not relax token/security behavior or
globally register the fixture class to obtain a pass.

## U1-M1: Qwen runner repair

Allowed edits only: `tests/uwp/u1_0/run-wine.sh`, new
`tests/uwp/u1_0/test-run-wine.sh`, and new `tests/uwp/u1_0/RUNNER_CHECK.md`.
Read `CLAUDE.md`, `docs/TOOLING.md`, U1 README and the existing runner first.

1. Make either architecture's failure, malformed/extra output, nonzero exit,
   timeout or oracle HRESULT mismatch cause a nonzero overall exit. Preserve
   CRLF normalization and explicit NOT-RUN package cases.
2. Canonicalize build and prefix paths before refusal checks (including symlink
   aliases and `..`). Require a fresh, empty, explicitly supplied prefix; take
   an exclusive ownership lock before mutation and refuse foreign/preexisting
   prefix state. Do not enumerate and kill arbitrary processes. Cleanup uses
   only this invocation's prefix and matching build's wineserver with bounds.
   If ownership cannot be proved, refuse and retain diagnostics.
3. Keep the two required positional arguments; allow an optional third fresh
   results directory. Default to a fresh temporary results directory, never the
   caller's current directory or tracked oracle files. Retain per-arch raw
   output, exit status and comparison results. Bound every Wine command and
   reject live installed system-role configuration. Record exact usage.
4. Add deterministic fake-loader checks for pass, wrong HRESULT, failed/malformed
   output, nonzero exit, timeout and foreign-prefix/alias/lock refusal. Run them
   in a copied disposable fixture without real Wine, builds, WSL or any VM.
   Check shell syntax. Git Bash is available at
   `C:/Program Files/Git/bin/bash.exe`; do not use the Windows WSL bash shim.

No probe/component, Wine, registry policy, global tooling or other file edits.
No commits/pushes; leave results for the existing Claude review before commit.

## U1-M2: Qwen unpackaged identity oracle

Allowed edits only: new files beneath `tests/uwp/identity/`. Keep the existing
`u1_0` fixture, runner, expected output and recorded oracle results unchanged.
This is test preparation, not a Wine implementation or packaged acceptance.

1. Add an original plain Win32/C++ ABI console probe for unpackaged package
   identity queries. Cover current-process full/family/path, explicit process
   full/family through both the pseudo-handle and an opened self-process handle,
   and full/family through the opened current process token. Do not target
   unrelated processes or tokens. Use valid, bounded buffers; record status,
   input/output lengths and handle-open errors without inventing expectations
   for unobserved error paths. No package paths/strings, token credentials or
   user identifiers in public output. No invalid-pointer/crash experiments.
2. Output one machine-readable JSON record per case with schema version,
   architecture, API, handle form, decimal/hex status, length and observed result.
   Win32 LONG status and NTSTATUS must never be conflated. Optional native
   `RtlQueryPackageIdentity` coverage is allowed only after checking the local
   declaration; missing exports are explicit NOT-RUN, never a silent pass.
   If the process unexpectedly has package identity, report the context and
   stop without printing its proprietary identity/path.
3. Add a PowerShell build/run driver with preview by default and explicit
   `-Execute`. Use the installed VS 2022/MSVC/SDK from `build.ps1`/`BUILD_CHECK.md`
   as a starting point, verify actual tools and record versions. Build x64 and
   x86 with static CRT in a fresh ignored output directory and execute only
   these self-owned probes, each with a 30-second bound and captured exit/output.
   No trust-store, certificates, package installation or policy changes.
4. Add a small results validator that rejects missing/duplicate records,
   malformed JSON, unsupported schema, omitted architecture/API cases or
   inconsistent unpackaged identity observations. Keep observed Windows values
   separate from prospective Wine assertions. Fault-check the validator using
   truncated, duplicate and altered synthetic copies of real captured records.
5. Document commands, hashes, actual Windows build/toolchain, observed cases,
   validation results and all NOT-RUN/limitations in `tests/uwp/identity/README.md`.
   If either compiler/run is unavailable, preserve the implementation and mark
   the exact blocker rather than installing tools or changing global settings.

Read `CLAUDE.md`, `docs/TOOLING.md`, U1 README and the existing fixture build
driver first. Use edit tools for code; no C escapes through shell heredocs or
`python -c`. No Python, Wine, WSL, VM, elevation fixture, network, registry
mutation, runtime edits, unrelated files, delegation or mailbox traffic. No
commits/staging/pushes; existing Claude review before commit remains. Return a
concise files/checks/evidence report. Qwen may use shell for these local builds,
our own executable runs, and validator checks only.

## U1-L1: Antigravity lab preparation

Allowed repository edits only: new `docs/uwp/LAB_STATUS.md` and, if needed,
new helpers beneath `scripts/lab/uwp/`. Scratch goes under
`unsloth-tmp/uwp-u1-20261111/`, never the shared resume state. Read `CLAUDE.md`,
`docs/TOOLING.md` and lab entry points/dependencies before invoking them.

1. Inspect capacity, existing QEMU processes/ports, immutable seed candidates
   and image/backing-file locks without changing other sessions. Treat A, B,
   elevation guests, wexbuild and their build/prefix trees as foreign. Never
   borrow their SSH sessions, stop them, or reset shared fixtures.
2. Provision a separate U1 guest only when there is headroom and a demonstrably
   quiescent/immutable seed. Use exclusively owned disk, run directory, SSH/QMP
   endpoints, PID metadata and logs. Never bypass image locking or copy a disk
   being written. A self-contained clone is preferred; an overlay requires an
   immutable backing chain for its entire lifetime. No host installers/browser
   runs and no external cloud resources.
3. Keep this job short: preflight and, if feasible, guest boot/SSH readiness only.
   No Wine build, UWP run or system-role provisioning yet. Record ownership,
   resource limits, exact launch/shutdown commands, source pins and state left
   behind. Helpers must refuse foreign state and stop only owned processes.
4. If no safe seed/capacity is available, or WSL reports
   `Wsl/Service/E_ACCESSDENIED`, stop that path and document the exact blocker and
   deferred provisioning steps. Do not try another distro to evade access denial.

No changes to existing lab launchers, shared disks, Wine or elevation files.
No commits/pushes. Report PASS/PARTIAL/BLOCKED accurately, commands and checks,
and every unrun step. Existing Claude review remains required before commit.

## Direct-agent dispatch

Qwen's native bridge reports ready with the configured local endpoint/model.
Antigravity's installed `C:/Users/Owner/.agents/skills/antigravity-bridge/`
contains `README.md` and `server.mjs` (no `SKILL.md`); it is not exposed as a
native tool here, so use its existing MCP stdio protocol through Node. CLI
version 1.5.4 returned READY in job `d2781531` with model
`gemini-3.8-flash-high`. No reinstall or provider/model substitution is needed.

User execution policy (2026-11-11): use Gemini 3.8 Flash while quota remains;
if its quota is exhausted, switch Antigravity to `gemini-4-argon-high` (verified
in the installed CLI's model list). Inspect per-job token/cache usage and
quota/rate-limit errors; the bridge does not expose remaining account quota.
For future long lab jobs use background execution with `timeout_s: 0` (no
whole-job deadline), while bounding individual commands and requesting durable
checkpoints. A positive `timeout_s` sets the CLI print deadline plus a bridge
kill timer 15 seconds later; positive values have a 30-second minimum. The
original 540-second and recovery 180-second limits were caller-supplied.

| Order | Direct bridge job ID | Dispatch state |
|---|---|---|
| U1-M2R, Qwen | `178354c0-0578-44b5-8583-9e7a2ed9dfa9` | Running; output preservation and review corrections, no lab access. |
| U1-M1R, Antigravity | `60be2d1d` | Running with no whole-job timeout; bounded runner and cleanup repair, no lab access. |
| U1-M1, Qwen | `91b59dea-bf75-4ceb-bf58-cdbb089d8343` | Done; 58/58 fake-loader checks, also rerun successfully by parent. Claude review and Linux/live cleanup verification pending. |
| U1-M2, Qwen | `b1fba388-d0c1-4a37-b6eb-ef6f083e38e5` | Done; x64/x86 builds/runs exit 0; 18 Win32 observations and 2 explicit native-query NOT-RUN records. Parent rechecked hashes, clean validation and rejection of truncated/duplicate/altered evidence. Claude review pending. |
| U1-L1, Antigravity | `9622abe3` | Failed: CLI print timeout after 9 minutes; no completion report or boot/SSH proof. |
| U1-L1 status recovery, Antigravity | `e5838065` | Done, PARTIAL/UNVERIFIED: LAB_STATUS.md written; QCOW2 header readable, integrity/boot/SSH unproven; blank WSL output stopped that inspection path. |

Parent status check (2026-11-11): the owned disk path
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/uwp-u1-lab/uwp-u1-disk.qcow2`
exists (12,768,443,672 bytes); conversion completion, image integrity and guest
readiness are unverified. Do not reuse it merely because it exists. The two SSH
helpers in `scripts/lab/uwp/` remain drafts pending ownership/host verification.
Codex subsequently ran read-only `qemu-img check` (no structural errors) and
`qemu-img compare`: the disk differs at offset 13,421,772,800. It is not an
accepted complete clone and will not be booted. A new self-contained clone is
being created on Linux storage under `/var/tmp/wex-uwp-u1-images-20261111/`,
with no shared backing file; conversion/check/comparison must all pass.

Runtime worktree: `C:/Users/Owner/.codex/worktrees/uwp-runtime/wine`, branch
`codex/uwp-runtime`, clean base `c3f59e05a37134aee529a7d636af3ba67a9efc5c`.
It excludes the shared checkout's uncommitted elevation registry edits. The
guest will use source from this pinned checkout, not a shared build tree.

U1-M2 evidence check (2026-11-11): executable and JSONL hashes match the
captured manifest for both architectures. All nine Win32 cases per architecture
record `15700` (`APPMODEL_ERROR_NO_PACKAGE`). `RtlQueryPackageIdentity` was
not called because its local SDK declaration was not verified; this is not a
passed observation. The parent reran the validator against retained evidence,
not the compiler or probe executables. No Wine or packaged acceptance is claimed.

Retrieve these exact jobs rather than dispatching duplicates. A running job or
successful delivery is not a completed or reviewed deliverable. These planning
updates and subsequent delegated results stay uncommitted pending the existing
review workflow; no unrelated elevation edits are included.
