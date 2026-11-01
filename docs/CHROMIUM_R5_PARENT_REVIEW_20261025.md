<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R5 parent review — ready for one startup diagnostic

Reviewed code cd1b90aa and report9b49ce70 on codex/chromium-r5-preparation.
**No blocking findings. READY_FOR_ONE_R5_TRIAL**, subject to existing live
prelaunch guards. No guest/browser/SSH execution occurred during this review.

The source changes add only fixed R5 mode/path/CLI selection and the existing
R4 compositor-scope/API-probe behavior. Timer fix832da342 remains present.
Original eight browser arguments, prefix, runtime, SID modules, namespace,
backend selection and all budget/cleanup checks are preserved. New tests cover
R5 CLI mutual exclusion, readonly view dispatch, API failure before browser,
successful sequencing, helper contract and mocked adapter orchestration.

Independent Debian WSL tests from the R5 worktree's scripts directory, Python
-B and D:-resident TMPDIR:

```text
python3 -B -m unittest test_g4_chromium_userenv_r5_adapter test_g4_userenv_view
 test_g4_chromium_native_exit test_g4_native_exit test_g4_compositor_scope -q

python3 -B -m unittest test_g4_virgl_launcher test_g4_virgl_budget
 test_g4_qmp_shutdown test_g4_guest_readiness -q
```

**32 tests in9.711s and52 tests in4.102s: all pass, no skips.** Each displayed
module list is one command. This does not relabel the worker's separate43-test
run with11 reported historical-adapter skips. The old R4 contracts had already
passed the parent's full95-test run after the timer fix. VM/remote operations
in these tests are mocked; native process/socket checks do not prove actual
guest shutdown or browser rendering.

Independent preparation inspection:

- All32 sealed entries match their retained contents. All21 safe regular TAR
  members match the transfer manifest and current source/helper/module inputs.
  All10 host launch/run/stop/readiness/budget helper contents match the prep.
- Staged probe has the absolute-deadline fix; runner and readonly view select
  userenv-r5. Helper is AMD64 with actual fixed R5 output and existing Chromium
  runtime strings. Its source/header equal the original current source/header;
  only its output macro is changed. No PE was executed by these checks.
- Copied launch.py and presentation-timing-trial.py are exact original helpers
  with only the explicit source-root substitution from C: parent to D: R5
  worktree. Existing disk selection is unchanged. Exact Windows/WSL launch,
  run and stop commands in preflight consistently use that D: root.
- PREPARED_NOT_RUN marker exists; fresh host run directory remains absent.
  Earlier R4 output is consumed and must not be reused. Guest free space,
  actual guest stage/output absence, prefix idle, runtime/backend and current
  process ownership remain live prelaunch checks, not offline observations.

Use the existing immutable
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-userenv-r5-20261025.preflight/`
and its reviewed D: worker-root helpers for the next trial; do not repoint that
preparation at the C: checkout or overwrite its source inventory. Its source
commit cd1b90aa differs from report-only HEAD9b49ce70, as documented; this is
not a historical identity blocker. Preserve raw logs and any failure, then
inspect the actual outcome before retrying or changing another API.

The independent shader fixture is not integrated or required for R5. Remaining
shader fake-uniform state rework is recorded separately. Main master/Mwine,
OOBE/token/other VMs and guest production binaries remain untouched. This review
does not imply Chromium startup, rendering, sandbox or G4 acceptance and does
not authorize a master merge.
