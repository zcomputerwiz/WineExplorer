<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium token-integration comparison result (20261027)

Worker execution of work order `docs/CHROMIUM_TOKEN_INTEGRATION_WORK_ORDER_20261027.md`
(parent commit `e076afb1`). One fresh normal Chromium comparison in the owned D: R4 VM/runtime,
against a combined token+graphics Wine candidate. The outcome is a comparison, not blanket
sandbox/G4 acceptance.

This revision records the **re-run after the identity-attestation fix** (commit `4628119b`), per
the parent's root-cause directive: point the strict `linux_identity` attestation at the candidate
build's own loader rather than a stale import-time path, then re-run the live trial.

## Summary

- **NLS / wineserver issue: FIXED and proven in-guest.** The candidate build now finds
  `l_intl.nls` cleanly; wine boots a full Windows environment (wineboot.exe → services.exe →
  explorer.exe) with no ntdll/NLS load errors.
- **Combined token+graphics Wine: merged, built, deployed, and functionally exercised.** The
  in-guest token-integration API probe passes **21/21** checks across `userenv.dll` and
  `kernelbase.dll` (derive → register → owned → verify → unregister; the success transitions are
  all S_OK).
- **Focused host tests: 33 unit tests pass**, including `test_actual_token_integration_cli_and_paths`,
  which now also asserts the identity fix.
- **Identity attestation fix (commit `4628119b`): APPLIED and PROVEN effective.** The re-run's strict
  `linux_identity` check now **passes** — `argv_style` resolves across both live observations, no
  `LinuxIdentityError`, the helper reaches `natural_exit`. This removes the prior hard blocker that
  had rejected the helper on an argv[0] path mismatch before Chromium ever launched.
- **Actual browser result (re-run): the page still did NOT load — but for a different, downstream
  reason.** With the identity gate now passed, Chromium actually launches and begins navigation to
  the fixture URL, but its network service crashes/restarts early and the tab for the fixture URL is
  killed (`sad_tab` "Tab Killed"); the helper then times out after 45 s waiting for a native-exit
  that never arrives. Retained and stopped per work order (not chased).

## Actual refs / merge decisions

Root branch `codex/chromium-token-integration` (from parent work-order commit `e076afb1`):
- Adds a fixed `token-integration` diagnostic identity to the existing consumer/module-view
  machinery (no new generic harness, no repeated offline approval gate):
  - `scripts/check-g4-chromium-native-exit.py`: new mode → HOST `chromium-token-integration-20261027`,
    BUILD `lab/wine-token-integration-20261027/build/wow64`; routed through the existing
    `g4_userenv_r6_probe.phase` with `strip_wineprefix`; new `--token-integration` flag.
  - `scripts/g4_userenv_view.py`: same mode/paths; the module view covers **both** `userenv` and
    `kernelbase` (x86_64 + i386) for this mode; new `--token-integration` dispatch.
  - `scripts/test_g4_userenv_view.py`: `test_actual_token_integration_cli_and_paths` pins the
    HOST/STAGE/OUTPUT/PROFILE/BUILD paths, the run() output dir, a strict-negative mutual-exclusion
    case (SystemExit 2), and that the view selects 4 module/arch entries.
- **Commit `4628119b`** "Point token-integration identity attestation at the candidate loader": in
  `configure('token-integration')` set `cal.BUILD=BUILD` and `cal.APPROVED_LOADER=BUILD/'loader/wine'`
  so the strict `linux_identity` check names the candidate's own loader (the wine build tree re-execs
  top-level `wine` → `loader/wine`). Extends `test_actual_token_integration_cli_and_paths` to assert
  both are set on configure and restore them in `finally` for test isolation.

Wine branch `codex/g4-token-integration` @ **`fffa7de`** (already pushed to origin):
- HEAD is a merge commit: **"Merge token security history (`ae5fec1`) into accepted
  graphics/SID/registration history (`cb1d117`)"**. Both histories are preserved; the graphics
  gitlink is NOT replaced with token-only Wine.
- Token-security side merged in on top of the accepted registration line: `cb1d117` (kernelbase:
  fix AppContainer registration publication & evidence, R2) ← `44c8989` (harden AppContainer
  registration locking/validation/tests) ← `74cb1f3` (genuine AppContainer SID registration APIs)
  ← `e096ae4` (userenv: app-container SID derivation).
- Graphics/SID/registration side retained down through `4315be8` (dxgi: retain composition presents
  made before target binding) and the earlier graphics history.

Both branches pushed to origin; no master merge. The candidate root gitlink names the combined
candidate, clearly unmerged/unaccepted for master.

## Build recipe / results

- Combined candidate built in the OWN D-backed lab via normal committed project assembly; guest
  server/client/ntdll all come from the SAME combined build (not a four-DLL view).
- Staged privately at a NEW guest location: `/home/debian/wex-g4/wine-token-integration-20261027/build/wow64`.
  Old build/prefix evidence preserved; fresh profile/output under `chromium-token-integration-20261027`.

### Two deploy root causes (both fixed, both proven in-guest)

1. **NLS / l_intl.nls.** The candidate tarball shipped `nls/` (77 files incl. `l_intl.nls`) and
   `fonts/` as **absolute symlinks** to the WSL build-host path
   `/var/tmp/wex-g4-token-parent-20261027/wine/...`, which are broken in the guest → wineserver
   "failed to load l_intl.nls". Fix: materialize the absolute-target data symlinks into real files.

2. **ntdll.so loader anchor.** A blanket `tar --dereference` also materialized the critical
   **relative** symlink `wine -> tools/wine/wine`. With `/proc/self/exe` then pointing at a bare
   regular file, the loader lost its `tools/wine/` build-tree anchor and fell back to the
   installed-layout path `<dir>/../lib/wine/<arch>-unix/ntdll.so` (nonexistent) → "could not load
   ntdll.so". Fix: materialize ONLY absolute-target data symlinks; **preserve** relative structural
   symlinks (including `wine -> tools/wine/wine`).

Selective-deref runtime tarball: `/var/tmp/wex-g4-token-parent-20261027/wine-token-build-selderef.tar.gz`
(996 MB, sha256 `468f9dcebe640ecb53b8cb94d40a0187d1e48cc13408c1be3f41e8abc1879954`). 83 absolute
data symlinks materialized; `wine` preserved as a relative symlink; 0 absolute symlinks remain.

In-guest deploy verification (`execution.json`): `guest_wine_deploy = "True 5654 True"` —
`nls/l_intl.nls` is a real 5654-byte regular file (not a symlink) and `wine` remains a symlink.
`outside_modules_unchanged = true`, `archive_exit = 0`.

## Focused tests

- Host unit tests (run from `scripts/` under WSL Debian):
  `python3 -m unittest test_g4_chromium_native_exit.py test_g4_userenv_view.py test_g4_chromium_observation_tolerance.py`
  → **33 tests, OK** (0 failures, 0 errors), both before and after the identity fix. The fix added
  assertions to the existing `test_actual_token_integration_cli_and_paths`, not a new test, so the
  count stays 33.
- In-guest token-integration API probe (`api-probe.stdout.log`): **21/21 checks pass**,
  `failures: 0`, `passed: true`. `userenv.dll` and `kernelbase.dll` both resolved
  (`paths_normal: true`, `exports_found: true`). The success transitions are all S_OK (hr=0):
  derive → register → owned → post-register lookup → verify → unregister. The two expected
  non-success codes are a nonzero already-present result on duplicate register and a not-found
  result (0x80070002) on the initial and post-unregister lookups — exactly the outcome of a correct
  register→unregister cycle.

## Identity attestation fix and re-run

- **Root cause** (parent's analysis, verified against source): `check-g4-native-exit-calibration.py`
  computes `APPROVED_LOADER=BUILD/'loader/wine'` at import time from the calibration build
  (`install.BUILD`, a stale g3-repair tree), and `collect()` passes it to
  `linux_identity(..., approved_loader=str(APPROVED_LOADER))` (line 207). The identity logic accepts
  style `approved_loader_argv_array` when the observed `/proc/<pid>/cmdline` satisfies
  `args == [approved_loader, *argv[1:]]`. In token-integration mode the wine build tree re-execs
  top-level `wine` → `<candidate>/loader/wine`, so a /proc read landing post-re-exec sees
  argv[0]=`<candidate>/loader/wine`; with the stale g3 approved_loader that matched no accepted form
  and raised `LinuxIdentityError('Linux image/app URL/profile/argv mismatch')` (the pre-fix trial,
  attempt5).
- **Fix:** in the token-integration branch of `configure()`, set `cal.BUILD=BUILD` and
  `cal.APPROVED_LOADER=BUILD/'loader/wine'`. Safe: `cal.guards()` (which contains the loader sha256-pin)
  is called only from test files, not the Chromium run path, so changing `cal.APPROVED_LOADER` affects
  only the line-207 identity check — no sha256-pin side effect.
- **Re-run mechanics:** re-ran `prepare()` to bake the fixed scripts into `source-stage.tar` (the guest
  executes the staged source, not the working tree); cleared the persisted guest STAGE/OUTPUT; fresh
  launch; fired `run()` immediately so the readiness budget passes at low elapsed. `orchestration_complete
  = true`; all budgets passed (readiness elapsed ~21.5 s).

## Actual browser result (re-run)

The one fresh Chromium comparison executed end-to-end (`execution.json`: `orchestration_complete =
true`).

- **Identity attestation PASSES** — the prior hard blocker is gone:
  - `linux_helper_first_live.argv_style` and `linux_helper_last_live.argv_style` are both
    `"argv_array"`; there is no `linux_helper_rejected_identity`, no `LinuxIdentityError`;
    `helper_phase = "natural_exit"`.
- **Chromium launched under the combined build:** the helper bound to `chrome.exe` (child_pid 220) at
  tick ~45.7 s; navigation to `http://127.0.0.1:41879/<nonce>/index.html` began (the data-protection
  observer logged the fixture URL in `chromium.log`).
- **The page did NOT complete:**
  - `chromium.log` line 132 (~0.9 s in): "Network service crashed or was terminated, restarting
    service."
  - `chromium.log` line 364 (~12 s in): `sad_tab.cc Tab Killed: http://127.0.0.1:41879/` — the tab for
    the fixture URL was killed.
  - The many `connect failed: 10051` lines are Chromium's **background** attempts to reach
    `accounts.google.com` / `update.googleapis.com`, refused because the VM is air-gapped — NOT the
    fixture; the co-occurring GAIA/update `ERR_ADDRESS_UNREACHABLE` failures are expected offline.
- **Helper outcome:** `journal_outcome = "native_timeout"` (`native-exit.jsonl`: a single `bound`
  event then a `timeout` terminal event; `wait_result = 258` = WAIT_TIMEOUT after `wait_ms = 45000`).
  Helper exit code 1 → `error = ValueError('Linux helper failed/exit mismatch')`. No load /
  navigation-completion event; `manifest_marker_verified = false`, `accepted_observation = false`.
- **Top-level:** `passed = false`, `diagnostic_exit = 1`, `observation_complete = false`;
  `front_exit = 0`, `cleanup_complete = true`, `remaining_servers = []`, `remaining_prefix_processes = {}`;
  `outside_modules_unchanged = true` (the readonly userenv/kernelbase view was intact across the run).

## Remaining blocker (retained, not chased)

- The prior **argv[0] identity-attestation** blocker is **RESOLVED** by commit `4628119b`: the re-run
  passes the attestation and launches Chromium.
- The **NEW remaining blocker is a Chromium runtime page-load failure**, downstream of the identity
  gate: the browser's network service crashes/restarts early and the fixture tab is killed, so no page
  load/render occurs and the helper times out after 45 s. This is orthogonal to the NLS/ntdll fixes
  (proven working in-guest) and to the token registration APIs (21/21 probe).
- Per the work order ("If still no page, retain exact new outcome and STOP; don't chase arbitrary
  warning lines"), this exact outcome is retained as the remaining blocker. No second browser retry,
  no-sandbox/workaround flag, debugger/attach, or unrelated production patch was applied.

## Evidence

- `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-token-integration-20261027\execution.json`
  (`orchestration_complete = true`)
- `...\chromium-native-evidence\results.json` (identity pass: first/last live `argv_style`;
  `journal_outcome = native_timeout`; helper exit 1)
- `...\chromium-native-evidence\native-exit.jsonl` (`bound` → `timeout`)
- `...\chromium-native-evidence\chromium.log` (network service crash line 132; tab killed line 364)
- Prior pre-fix trial preserved at
  `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-token-integration-20261027.attempt5-argv0-mismatch\`

Private payload/art/profile/screenshots remain on D: / D-backed WSL; nothing secret or vendor is
committed.
