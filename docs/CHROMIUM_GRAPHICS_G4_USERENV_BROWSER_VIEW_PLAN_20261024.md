<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R3: test the SID fix through a private normal-module view

Parent reviewed181261e3's57 artifacts and both actual builtin loader/assertion
logs: x64/i686260 tests each,0 failures/skips, one old unrelated todo. The new
builtin modules loaded via ordinary system32/default loader, no adjacent DLL or
userenv override, physical old modules unchanged outside private mount namespace.
Wine e096ae4 and its two builtin artifacts are accepted for consumer testing.

Use the same already-proven private file-bind technique in the existing owned
VM; no persistent build overwrite or extra disk image is needed. This next task
is OFFLINE CODE/BUILD/TEST/PREPARATION ONLY. Parent reviews before a live control.

1. Fresh fixed HOST/output/profile `chromium-userenv-r3-20261024` and source stage
   with -stage suffix. Compile the unchanged native parent with ONLY that output
   macro override; no calibration repeat. Original runtime and all R1/R2 evidence
   preserved. Keep normal eight browser args and R2 warn+module/trace+seh logging.
2. Small root view wrapper runs INSIDE existing run-isolated-graphics.py mount/
   network namespace. Refuse execution before any mount if it shares init's mount
   or network namespace, or selected source/target paths are wrong/noncanonical.
   Bind existing Wine BUILD read-only, then bind reviewed new builtin x64/i686
   userenv.dll files over only corresponding module paths, read-only, in this
   namespace. Old physical build files are never copied over. Log actual before/
   inside hashes/namespace/mount sources and exec fixed runuser/debian R3 runner;
   no arbitrary command/path passthrough. Emit bounded view JSON to consumer
   stdout BEFORE exec so evidence survives a runner guard failure. Capture exact
   outside physical hashes before/after run as diagnostics.
3. In R3 runner after guards/compositor and BEFORE browser launch, run one tiny
   original x64 API probe through default Wine/userenv loading, no userenv override.
   Probe dynamically finds the export, checks actual normal module path and exact
   parent-oracle SID vectors/invalid args/FreeSid, emits bounded JSON and exits0
   only for successful actual API assertions. No profile/token changes in probe.
   Archive its stdout/stderr/results. A missing export or mismatch stops before
   browser. Prefix server cleanup between probe and browser is a SEPARATE probe
   phase; do not set the browser helper's final cleanup-start marker prematurely.
   Validate idle before launching held-handle native browser parent.
4. Reuse current R2 consumer/parser/HTTP/backend/owned cleanup. Result separates
   API probe, browser native status, HTTP/readiness/frame observations, helper
   natural-vs-killed exit and final cleanup. Diagnostic1 remains nonacceptance.
   Do not claim graphics or effective sandboxing from API identity derivation.
   Ordinary retained-prefix writes/cache changes are recorded; never repair old
   profiles or force Wineboot updates. No persistent DLL deployment in this task.
5. Meaningful tests: private namespace/path validation before mounts; exactly two
   readonly file binds/no underlying writes; R3 CLI/helper contract unchanged
   browser flags; actual original probe on HOST Windows public APIs (safe read-only,
   no install) plus JSON negatives, probe-failure preventing browser, intermediate
   cleanup ordering, original defaults and current source-transfer integrity.
   Use new private D: build outputs/fixtures, no changes to existing binaries.
6. Small fresh R3 adapter/PREPARED_NOT_RUN inventory: source/probe/helper/module
   observations, exact normal module-view argv, existing R4 disk/runtime, fresh
   paths,285/55 runner/82 helper/300SSH/45archive and900/90/80 outer containment.
   Add bounded probe allowance WITHIN current total, not an extension. Space/
   readiness/complete-empty owners/privateLO/unique sockets/actualVirGL remain.
   Historical identities/hashes are warnings, not reintroduced refusal gates.

Commit AND push code/tests/report/preparation, STOP parent review. No VM/SSH/live
browser/mount dispatch in this task, no Wine rebuild/root gitlink/master merge/
other-worker changes/vendor publication. Private outputsD:, sparse Wine worktree
and rootsource stay C: graphics area.95%/99% usage policy, no reset credits.

After review ONE live control should establish either progress beyond this
demonstrated missing API or a precise remaining blocker. Do not preemptively fix
GetPointerDevice/PowerReadACValue/token/profile APIs. A normal API probe is a
functional implementation check, not security enforcement or capability spoofing.
