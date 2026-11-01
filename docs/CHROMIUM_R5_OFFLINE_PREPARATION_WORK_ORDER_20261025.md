<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Queued Antigravity assignment: fresh R5 startup preparation

Implement after delivering the existing shader correction, unless the parent
explicitly changes priorities. Timer fix e3ec82e0 is imported as832da342 on the
graphics branch. Parent reviews all new code/preparation before any live run.

Use a new own branch `codex/chromium-r5-preparation` and D:-resident worktree
based on832da342 or its work-order-only descendant. Do not modify the parent C:
checkout, shader/timer branches, existing private helpers or any consumed run.
This assignment is OFFLINE ONLY: no VM boot, SSH, mounts, browser/Wine/probe
execution, disk creation/changes, runtime extraction/download or package install.

## Required bounded adaptation

The R4 execution failed before the API/browser on the now-fixed45s arithmetic.
R4's guest stage/output, host run and source archive are consumed and preserved.
The native parent embeds fixed output paths, so do not retry or rename R4.
Prepare a genuinely fresh `chromium-userenv-r5-20261025` control using the same
disk, existing browser/runtime/prefix, accepted SID modules and API probe.

1. Add explicit `userenv-r5` fixed mode/CLI to the native runner and readonly
   module-view wrapper, following R4 exactly. Fixed host/output/profile name
   `chromium-userenv-r5-20261025`, guest stage with `-stage` suffix. R5 opts into
   compositor child-only WINEPREFIX stripping and API probe/intermediate cleanup.
   Preserve every legacy mode and original eight browser arguments. No new
   application compatibility workaround, feature/security flags or GL behavior.
2. Compile only the original native parent's OUTPUT macro into a new AMD64 PE
   in `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/userenv-browser-r5-20261025.build`.
   Reuse the original source/header and reviewed R4 build.py recipe read-only;
   save the copied source, exact output override, compiler command/receipt/logs.
   Strict compiler warnings must pass. Do not execute the PE or rebuild SID/Wine.
   Original image contract remains the installed Chromium runtime from R1.
3. Create a new small private R5 adapter in **your own worktree's** ignored
   `build/g4-virgl-20261022/`, following the existing private R4 adapter.
   It must select R5 mode/stage/output/new parent PE and corrected probe source.
   Parent's C: R4 adapter/preflight/build may be read as references, not edited.
4. Preparation needs launch.py/presentation-timing-trial.py and seed references.
   If necessary copy these small helper/seed files into your own ignored build
   folder, recording the originals and narrow path edits. Set helper REPO paths
   to your actual D: worktree; do not leave stale C:/D: main-checkout aliases.
   The existing launcher's disk selection stays the **same existing**
   `system-native-identity-r4-20261024.qcow2`; its name is not a reason to create
   another disk. Only script source roots/RunName change. Do not copy private
   runtimes, VM images or keys into Git/source. Use the durable existing native
   credential read-only; never print, rotate or replace it.
5. Fresh D:-lab `.preflight` and `.change` directories, no overwritten artifacts.
   Archive precisely the transitive R5 sources/fixture, original new helper PE,
   reused API probe, two accepted SID DLLs and runtime manifest. Validate current
   archive transfer contents and concrete paths. Record historical versions/
   IDs/hashes diagnostically; do not add equality gates or rehash whole disks.
   Preserve the normal guard ordering: guest readiness, free space before
   staging/profile, prefix idle, guest paths absent, transfer integrity, explicit
   backend/private network/socket/readonly-view checks, API/intermediate cleanup,
   then browser observer. Existing runtime must be reused, not reinstalled.
6. Produce `PREPARED_NOT_RUN` with exact Windows launch, WSL adapter run and
   identity-based stop commands for **your actual source root**. Keep900/90/80
   outer,674/650/435 raw UTC,285/55 consumer,82 observer,300SSH/45archive and
  45probe/25run/5reap/15cleanup bounds unchanged. Guest path absence, guest free
   space and actual VirGL remain future live observations, not offline passes.

## Required proof and completion

Add fixed-mode/path/CLI/constructor, failed-API-before-browser, successful
sequencing, compiled-path and adapter/transfer tests using R4's focused patterns.
Run the unchanged fractional-clock regressions too. Compile tests may compile
original small probes but never execute Wine or Windows binaries. If private
fixtures are unavailable in your worktree, report actual skips separately;
do not populate it with VM/runtime copies or touch another worker's build.

Keep tests/read-only adapter inspection distinct from live dispatch: mocked
remote operations do not authorize SSH. Test adapter `run` only with its remote
execution mocked. Test `prepare` only for the fresh new fixed directories, after
confirming the old R4/R3 inputs remain unchanged. Do not create the host RUN dir
or exercise launch/stop commands during offline preparation.

Deliver code/tests and
`docs/CHROMIUM_R5_OFFLINE_PREPARATION_20261025.md`: exact source base, changes,
commands/results/skips, fixed paths, PE recipe, private prep paths and remaining
live checks. Commit AND push to your R5 branch, then reply to codex with commit,
report and `PREPARED_NOT_RUN` verdict. Stop for parent review. Messages to codex
for acknowledgement/blocker/completion are authorized. No merge/PR or further
task expansion. Any unexpected original-path conflict is a blocker to report,
not permission to reset/delete evidence or create a new VM.
