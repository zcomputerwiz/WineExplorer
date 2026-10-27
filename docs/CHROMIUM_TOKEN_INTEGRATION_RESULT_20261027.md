<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium token-integration comparison result (20261027)

Worker execution of work order `docs/CHROMIUM_TOKEN_INTEGRATION_WORK_ORDER_20261027.md`
(parent commit `e076afb1`). One fresh normal Chromium comparison in the owned D: R4 VM/runtime,
against a combined token+graphics Wine candidate. The outcome is a comparison, not blanket
sandbox/G4 acceptance.

## Summary

- **NLS / wineserver issue: FIXED and proven in-guest.** The candidate build now finds
  `l_intl.nls` cleanly; wine boots a full Windows environment (wineboot.exe → services.exe →
  explorer.exe) with no ntdll/NLS load errors.
- **Combined token+graphics Wine: merged, built, deployed, and functionally exercised.** The
  in-guest token-integration API probe passes **21/21** checks across `userenv.dll` and
  `kernelbase.dll` (derive → register → owned → verify → unregister; the success transitions are
  all S_OK).
- **Focused host tests: 33 unit tests pass** (`Ran 33 tests in 6.854s — OK`), including the new
  `test_actual_token_integration_cli_and_paths`.
- **Actual browser result: the Chromium comparison did NOT load the page.** The diagnostic's strict
  `linux_identity` attestation rejected the helper on an argv[0] path mismatch before Chromium
  emitted any events; no HTTP request reached the fixture. Retained and stopped per work order (not
  chased).

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
- Bumps the wine gitlink to the combined candidate (below).

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
  → **`Ran 33 tests in 6.854s — OK`** (0 failures, 0 errors). Includes the new
  `test_actual_token_integration_cli_and_paths`.
- In-guest token-integration API probe (`api-probe.stdout.log`): **21/21 checks pass**,
  `failures: 0`, `passed: true`. `userenv.dll` and `kernelbase.dll` both resolved
  (`paths_normal: true`, `exports_found: true`). The success transitions are all S_OK (hr=0):
  derive → register → owned → post-register lookup → verify → unregister. The two expected
  non-success codes are a nonzero already-present result on duplicate register and a not-found
  result (0x80070002) on the initial and post-unregister lookups — exactly the outcome of a correct
  register→unregister cycle.

## Actual browser result

The one fresh Chromium comparison executed end-to-end (`execution.json`: `orchestration_complete =
true`; budgets all passed — readiness elapsed 1.07 s, preflight 41.5 s, ownership 41.6 s, consumer
91.5 s).

- Wine boots a full Windows environment cleanly: `api-probe.stderr.log` is a complete, clean wineboot
  (wineboot.exe → services.exe → explorer.exe) with **no ntdll/NLS load errors**.
- The Chromium helper (`native-exit.exe --chromium --image .../chrome.exe ...`) launched under the
  combined build, but the diagnostic's strict `linux_identity` attestation **rejected it** before
  Chromium emitted any events:
  - `error = LinuxIdentityError('Linux image/app URL/profile/argv mismatch')` (ValueError),
    `helper_phase = linux_identity`.
  - The launched cmdline was recorded with argv[0] = `.../build/wow64/wine`
    (`linux_helper_last_live.observed_cmdline[0]`; `prefix_match.matched = true`). By the attestation
    read, `/proc/<pid>/cmdline` argv[0] had become `.../build/wow64/loader/wine` (base64-decoded from
    `linux_helper_rejected_identity.fields.cmdline`); all other argv components (native-exit.exe,
    --chromium, --image, --profile, --log, --events, --nonce, --url) are identical.
  - Consequence: `native_journal_at_identity_rejection.present = false` (no `native-exit.jsonl`),
    HTTP fixture `requests = []` (the page was never fetched; classification "HTTP writes only;
    browser consumption NOT_PROVEN"), helper reaped with exit `-9`, `front_exit = 0`.
- Top-level: `passed = false`, `diagnostic_exit = 1`, `observation_complete = false`;
  classification "Changed parentage: ordinary CreateProcess held-handle Chromium status ONLY; never
  rendering/security acceptance".

## Remaining blocker (retained, not chased)

The strict `linux_identity` attestation rejects the helper on **argv[0] only**: the invoked path
`.../build/wow64/wine` transitions to `.../build/wow64/loader/wine` between launch and the
attestation read. This is because the candidate is deployed as a raw Wine **build tree**, where the
top-level `wine` (the preserved relative symlink → `tools/wine/wine`) re-execs itself as
`loader/wine`; `/proc/<pid>/cmdline` then reflects the post-re-exec loader path rather than the
invoked path. This is a property of deploying the raw combined build tree and is orthogonal to the
NLS/ntdll fixes (which are proven working in-guest).

Per the work order ("If still no page, retain exact new outcome and STOP; don't chase arbitrary
warning lines"), this exact outcome is retained as the remaining blocker. No second browser retry,
no-sandbox/workaround flag, debugger/attach, or unrelated production patch was applied. A likely
follow-up (for parent decision, not executed here) is a deployment form in which the invoked wine
path is stable across the process lifetime (e.g., top-level `wine` resolving directly to the loader
without an intermediate wrapper re-exec), which would present argv[0] = `.../wow64/wine` to the
attestation.

## Evidence

- `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-token-integration-20261027\execution.json`
- `...\chromium-native-evidence\results.json` (identity rejection: lines ~7796–7879)
- `...\chromium-native-evidence\api-probe.stdout.log` (21/21 probe JSON)
- `...\chromium-native-evidence\api-probe.stderr.log` (clean wineboot)

Private payload/art/profile/screenshots remain on D: / D-backed WSL; nothing secret or vendor is
committed.
