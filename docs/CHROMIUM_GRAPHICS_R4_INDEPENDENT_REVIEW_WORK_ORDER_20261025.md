<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Assignment: independent review of the next Chromium startup control

Assigned to the Antigravity bridge worker (`qwen-orchestrator`, described by the
user as Gemini). Review target: `5cb366f9af26cc77e7dc80e996cfab95de0a8111` on
`origin/codex/g4-r1-completion`. This is a bounded independent review, not another
implementation of Luna's completed work or authority to run the VM.

## Why this advances Chromium

The actual browser previously exited with delay-load exception `0xc06d007f`.
Same-process/thread logs identified missing
`USERENV!DeriveAppContainerSidFromAppContainerName`. Its implementation and real
guest API probe now pass. The last control (R3) did **not launch Chromium**:
intermediate cleanup counted its own native labwc compositor, which inherited
WINEPREFIX. R4 removes that marker from only the native compositor child. Review
this correction so the parent can proceed to the first browser control with the
new API. Do not claim the browser is fixed from the API probe or mocked tests.

## Ownership and checkout

Read these target-commit files first:

- `docs/CHROMIUM_GRAPHICS_G4_COMPOSITOR_SCOPE_R4_PLAN_20261025.md`
- `docs/CHROMIUM_GRAPHICS_G4_COMPOSITOR_SCOPE_R4_PREP_20261025.md`
- `docs/CHROMIUM_GRAPHICS_G4_SOURCE_WORKTREE_20261023.md`

Use a separate worktree and branch `codex/chromium-r4-independent-review` based
on the target commit. Suggested D:-resident location:
`D:/DeepThought/Worktrees/chromium-r4-independent-review/WineExplorer`.
Check whether that branch/path already exists; reuse only if yours and suitable.
Do not reset, switch, clean, update submodules or stage gitlinks in any existing
worker checkout. Main `D:/DeepThought/Projects/WineExplorer` is master; leave it.
Parent source `C:/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer`
is read-only to this assignment. Its existing M wine is unrelated.

## Required review and proof

1. Read the complete `1538e7c3..5cb366f9` diff. Verify `strip_wineprefix` defaults
   to false; only explicit userenv-r4 opts in. Verify removal is from the copied
   native-child environment only. Caller WINEPREFIX and shared Wayland endpoint
   updates must survive. `graphics_resources.sample` and real Wine leak checks
   must remain intact; no blanket PID exclusions.
2. Independently run the actual native-child regression in Debian WSL. Confirm
   that the legacy marked child and separate genuine marked owner are sampled,
   the opted-in unmarked child is absent, and the genuine marked owner still
   rejects intermediate cleanup. Endpoint/protocol callbacks are mocked: report
   that limitation explicitly. Check all spawned children are reaped.
3. Check R4 runner/view CLI dispatch, original eight browser arguments, fresh
   R4 paths, API failure before HTTP/browser, and successful API -> intermediate
   cleanup -> HTTP/browser sequencing. R1/R2/R3 behavior must stay unchanged.
   Diagnostic exit 1 is not product acceptance.
4. Review the existing R4 private adapter and PREPARED_NOT_RUN manifest for
   correct source/module/probe/PE/path selection and intended call ordering.
   Read them, do not execute their dispatch/preparation/launch commands. Validate
   fresh current transfer TAR contents against its own manifest; historical
   filesystem IDs, timestamps, hashes and versions are diagnostic warnings, not
   reasons to restart provenance investigations. Report concrete wrong paths,
   overwrites, foreign owners or transfer corruption as blockers.
5. Report actual findings with severity, file/line and reproducible trigger.
   If no issue is found, say so with remaining limitations. Do not invent a
   production patch for another unobserved missing API. If a defect requires a
   fix, add a small reproducer on your branch when practical, document the fix
   needed, and stop for parent review rather than silently editing the target.

## Exact focused test command

Run in Linux, not Windows Python. Use `python3 -B` and a unique TMPDIR under
`/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/` for new private test artifacts.
Do not use any existing R1/R2/R3/R4 run/preflight/output directory as TMPDIR.
The native-child test needs the existing LAB directory but does not start Wine
or a compositor. Private adapter tests need ignored files already present in the
parent checkout; they will otherwise skip in a fresh worktree. For that exact
suite, run the existing parent scripts **read-only**, after checking its tracked
code still matches review target. Do not copy runtime files or mutate its build.
Record that test-source location separately from your review worktree.

```text
python3 -B -m unittest test_g4_compositor_scope test_g4_wexcomp_harness
 test_graphics_resources test_g4_userenv_view test_g4_chromium_userenv_r4_adapter
 test_g4_chromium_userenv_adapter test_g4_chromium_module_adapter
 test_g4_chromium_native_adapter test_g4_chromium_native_exit test_g4_native_exit
 test_g4_native_exit_calibration test_g4_edge_http_fixture -v
```

This is one shell command with the listed modules. Prior report: 91 tests, no
skips. Independently record counts, failures, skips, elapsed time and command;
never convert skipped private tests into passes. Missing inputs are a specific
limitation to report, not authority to rebuild/reinstall. No broader suite needed.

Private read-only inputs:

- Parent `build/g4-virgl-20261022/chromium-userenv-r4-trial.py`.
- `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-userenv-r4-20261025.preflight/`.
- `.../chromium-userenv-r4-20261025.change/` and
  `.../userenv-browser-r4-20261025.build/`.

## Deliverables and stop point

Commit a concise independent report as
`docs/CHROMIUM_GRAPHICS_R4_INDEPENDENT_REVIEW_20261025.md` on your branch. Include
target SHA, reviewed files, exact tests/results, actual vs mocked evidence,
blocking findings (or none), and one of `READY_FOR_PARENT_LIVE_REVIEW` or
`BLOCKED` with a specific reason. Keep raw private logs outside Git; publish only
sanitized summaries/reproducers. Commit AND push to origin; do not merge or open
a PR. Reply through the bridge to mailbox `codex`, identifying your worker/model,
branch, commit, report path and verdict. This assignment explicitly authorizes
that progress/completion or blocker reply; do not contact other recipients.

No VM boot/SSH/browser launch, mounts, disk changes, Wine builds, package installs,
new runtime downloads or root gitlink changes. Leave OOBE/security/other agents'
VMs and checkouts alone. No sandbox bypass, feature spoofing or vendor payload/art
publication. This review does not approve G4 or a master merge. Parent retains
final review and later live-trial dispatch. Stop after delivering the report.
