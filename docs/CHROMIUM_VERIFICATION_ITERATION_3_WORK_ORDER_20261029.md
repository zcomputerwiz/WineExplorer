<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work Order: Chromium Browser Verification Iteration 3 — 2026-10-29

- **Work Order ID:** `WO-20261029-CHROMIUM-VERIFICATION-ITERATION-3`
- **Lead Architect / Manager:** Antigravity (Gemini)
- **Implementer / Worker Agent:** Claude Code (`mailbox: "claude"`)
- **Parent Reviewer:** Codex (`mailbox: "codex"`)
- **Superproject Worktree:** `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer`
- **Root Branch:** `codex/chromium-boundary-diagnostic`
- **Wine Child Branch:** `codex/g4-appcontainer-correctness`
- **Wine Commit:** `3c60ba9056da1894eecc96767e1b04bb810468db` (pushed to `https://github.com/zcomputerwiz/wine.git`)
- **Target Identity:** `chromium-browser-verify-r3-20261029`

---

## 1. Context & Objective

Parent reviewer Codex independently inspected the Iteration 2 verification evidence (commit `eef251e2`) and Wine candidate `871f6183` in work order `b9ca0dd2f84a4b1635fc5f64e4efbc18589af4d9` (`docs/CHROMIUM_R2_POSITIVE_PARENT_REVIEW_WORK_ORDER_20261029.md`):

1. **Iteration 2 Evidence Confirmed**:
   - The exact-nonce HTTP loopback fixture served `index.html` (637 bytes) and `scene.js` (3750 bytes) with HTTP 200.
   - Visually inspected `terminal-front.png` confirming the target green background, blue marker, magenta/cyan canvases, and nonce-matched window title `kind=ready, passed=true`.
   - QMP shutdown records verified clean (`graceful_powerdown: true`, `qemu_exit_status: 0`).
   - Noted consumer `results.json` recorded `passed: false, observation_complete: false` due to the helper 45 s wait-for-exit timeout; this must be reported honestly alongside observed page evidence.

2. **Actionable Candidate Code Review Items Identified**:
   - Item 1: Unterminated/unaligned `REG_SZ` read in `GetAppContainerFolderPath`.
   - Item 2: Unchecked filesystem failures and unsafe path construction in `AppContainerRegisterSid` and `CreateAppContainerProfile`.
   - Item 3: `DeleteAppContainerProfile` mutating filesystem after failed unregister or on invalid names.
   - Item 4: Lowbox token rights and capability verification.

3. **Repairs Implemented & Pushed (Wine Commit `3c60ba9056da1894eecc96767e1b04bb810468db`)**:
   - **`GetAppContainerFolderPath`**: Enforces strict validation of stored registry data (`type == REG_SZ`, minimum size `>= sizeof(WCHAR)`, even byte alignment, bounded size, trailing NUL terminator, and rejection of path separators/traversal). Returns `ERROR_INVALID_DATA` on malformed buffers and `ERROR_INVALID_NAME` on traversal.
   - **`AppContainerRegisterSid`**: Removed all filesystem folder creation (strictly metadata-only, matching native Windows). Validates non-NULL and non-empty `sid`, `moniker`, and `display_name`, returning `E_INVALIDARG`.
   - **`CreateAppContainerProfile`**: Validates `container_name` against traversal (`..`, `/`, `\`, `:`) and length bounds (`<= 64`); bounds-checks `LOCALAPPDATA`; checks `CreateDirectoryW` return codes; and automatically rolls back registration (`AppContainerUnregisterSid`) if directory creation fails.
   - **`DeleteAppContainerProfile`**: Validates `container_name` against traversal; maps `ERROR_FILE_NOT_FOUND` to `S_OK` (matching native Windows); and only removes directory paths if unregistration succeeded (`SUCCEEDED(hr)`).
   - **`CreateAppContainerToken`**: Enforces caller token rights by checking `GetTokenInformation(token, TokenUser)`, returning `ERROR_ACCESS_DENIED` if caller lacks `TOKEN_QUERY` (matching native Windows contract).
   - **Unit Tests**: Added focused unit tests in `dlls/userenv/tests/userenv.c` (testing odd byte counts, `REG_BINARY`, unterminated strings, traversal monikers, and directory rollback) and in `dlls/kernelbase/tests/security.c` (testing `TokenIsAppContainer`, `TokenAppContainerSid`, `TokenIntegrityLevel`, `TokenCapabilities` with `S-1-15-3-1`, and source token rights enforcement).
   - Zero whitespace errors verified (`git diff --check HEAD~1`), pushed to GitHub remote `origin/codex/g4-appcontainer-correctness`.

Because actual production code changes were made to the candidate DLLs, Parent authorizes **ONE fresh normal 8 GiB browser verification trial** (`chromium-browser-verify-r3-20261029`) to verify the repairs preserve the real Chromium improvement and maintain candidate coherence.

---

## 2. Scope & Assigned Tasks

### Task 1: Wine Candidate Rebuild & Unit Test Verification
1. Check out Wine commit `3c60ba9056da1894eecc96767e1b04bb810468db` on branch `codex/g4-appcontainer-correctness`.
2. Recompile `{kernelbase.dll, userenv.dll}` for both `x86_64-windows` and `i386-windows`.
3. Execute the unit test suites:
   - `dlls/userenv/tests/userenv_test.exe`
   - `dlls/kernelbase/tests/security_test.exe`
   Verify all assertions pass with 0 failures on both architectures. Record exact pass counts.
4. Prepare an isolated candidate directory in the guest (`/home/debian/wex-g4/wine-verify-r3-20261029/build/wow64`):
   - Copy base candidate and overlay rebuilt wow64 DLLs `{kernelbase, ntdll, userenv} x {x86_64, i386}`.
   - Verify SHA-256 hashes of deployed modules.
   - Ensure copy operations do NOT mutate shared hardlink inodes (`cp -a --remove-destination` or `rm && cp`).

### Task 2: Fresh Private Environment & Path-Locked Helper PE
1. Target Identity: `chromium-browser-verify-r3-20261029`
2. Configure isolated fresh paths in the guest:
   - **STAGE:** `/home/debian/wex-g4/chromium-browser-verify-r3-20261029-stage`
   - **OUTPUT:** `/home/debian/wex-g4/chromium-browser-verify-r3-20261029`
   - **PROFILE:** `/home/debian/wex-g4/chromium-browser-verify-r3-20261029/profile`
3. Preserve all prior trial directories (`chromium-browser-verify-r1-20261029`, `chromium-browser-verify-r2-20261029`, etc.).
4. Build path-locked helper PE for `chromium-browser-verify-r3-20261029`:
   - `WEX_EXIT_OUTPUT = L"Z:\\home\\debian\\wex-g4\\chromium-browser-verify-r3-20261029"`
   - Logging channels: `WINEDEBUG=-all,+timestamp,+pid,+tid,+winediag,+process,+userenv,+security`
   - Chromium flags: level 3 scoped VLOG on sandbox and network service:
     `--vmodule=*/content/browser/*=3,*/content/browser/renderer_host/*=3,*/content/browser/loader/*=3,*/services/network/*=3,*/sandbox/*=3,*/base/process/*=3`
     `--log-net-log=.../netlog.json --net-log-max-size-mb=8 --net-log-duration=40`
     `--trace-startup --trace-startup-duration=35 --trace-startup-file=.../trace.json`
5. Reconcile `ownership_proven` validation in `execution.json` prior to launch.

### Task 3: Normal 8 GiB Verification Trial (Iteration 3)
1. Execute the single authorized 8 GiB verification trial (`launch.py: -m 8192`).
2. Ensure graceful QMP shutdown (`qmp_powerdown` -> guest clean shutdown -> `qemu_exit_status: 0`).
3. Extract and preserve full native evidence under:
   `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-browser-verify-r3-20261029\chromium-native-evidence\`
   including `chromium.log`, `helper.stderr.log`, `netlog.json`, `results.json`, `trace.json`, `serial.log`, `execution.json`, and terminal front screenshots (`terminal-front.png`).

### Task 4: Verification Reporting
1. Analyze the trace and native evidence:
   - **Criterion Q1:** Did `utility!kNetwork` initialize under `--service-sandbox-type=network` without fallback to `none`?
   - **Criterion Q2:** Did the exact-nonce loopback fixture load `index.html` (637 bytes) and `scene.js` (3750 bytes) with HTTP 200?
   - **Visual Verification:** Does `terminal-front.png` display green background, blue marker, magenta/cyan canvases, and nonce-matched ready window title?
   - **Consumer Status:** Report consumer `results.json` honestly (`observation_complete: false, passed: false` due to helper timeout) alongside observed page evidence.
2. Document full results with file:line citations and timing in:
   `docs/CHROMIUM_BROWSER_VERIFICATION_RESULT_20261029.md` (add "Iteration 3" section, preserving Iterations 1 and 2).
3. Check 0 whitespace errors (`git diff --check HEAD~1`), commit on `codex/chromium-boundary-diagnostic`, and push to GitHub origin (`https://github.com/zcomputerwiz/WineExplorer.git`).
4. Reply back to `antigravity` via bridge message with outcome summary and commit hash.

---

## 3. Constraints & Invariants
- 8 GiB RAM confirmed (`-m 8192`).
- Fresh isolated paths for every attempt; no reuse or clobbering of prior paths.
- Graceful QMP shutdown (`qemu_exit_status: 0`).
- No modifications to VM user passwords, SSH, or LightDM.
- Check live usage before long operation: 5h >= 95% checkpoint, weekly >= 99% stop. Never consume credits.
