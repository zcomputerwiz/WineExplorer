<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work Order: Chromium Browser Verification Iteration 4 — 2026-10-29

- **Work Order ID:** `WO-20261029-CHROMIUM-VERIFICATION-ITERATION-4`
- **Lead Architect / Manager:** Antigravity (Gemini)
- **Implementer / Worker Agent:** Claude Code (`mailbox: "claude"`)
- **Parent Reviewer:** Codex (`mailbox: "codex"`)
- **Superproject Worktree:** `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer`
- **Root Branch:** `codex/chromium-boundary-diagnostic`
- **Wine Child Branch:** `codex/g4-appcontainer-correctness`
- **Wine Commit:** `4afbb4cd9753e185806c04f56e9c98a58a6984e1` (pushed to `https://github.com/zcomputerwiz/wine.git`)
- **Target Identity:** `chromium-browser-verify-r4-20261029`

---

## 1. Context & Diagnostic Root Cause Analysis

Following the execution of Iteration 3 (`chromium-browser-verify-r3-20261029`), trace analysis identified the exact blocker preventing the network service from launching under the AppContainer policy:

1. **Iteration 3 Observation**:
   - `AppContainerRegisterSid` succeeded normally (`hr=0x800700b7` / `ERROR_ALREADY_EXISTS`), and `AppContainerUnregisterSid` was **never called** (0 occurrences, verifying the unregister rollback defect remains completely fixed).
   - However, Chromium aborted the network service launch before process spawn at line 805 of `sandbox_win.cc`:
     `Sandbox cannot access executable ... Insufficient buffer. (0x7A)`
     followed by line 652: `Network service crashed, restarting service`.
2. **Root Cause Analysis**:
   - In `CreateAppContainerToken` (`dlls/kernelbase/security.c:2043`), caller token validation used `TOKEN_USER user;` with `sizeof(user)` (16 bytes) in `GetTokenInformation(token, TokenUser, &user, sizeof(user), &len)`.
   - In Windows NT security architecture, `TokenUser` information class writes both the `TOKEN_USER` structure and the variable-length SID (~28 to 44 bytes minimum) into the caller's buffer.
   - Calling with 16 bytes invariably caused Wine/NT to return `ERROR_INSUFFICIENT_BUFFER` (`0x7A` / 122).
   - Chromium's pre-spawn sandbox configuration checks (`sandbox_win.cc:805`) received this failure code and aborted the network service spawn.
3. **Repairs Implemented & Pushed (Wine Commit `4afbb4cd9753e185806c04f56e9c98a58a6984e1`)**:
   - **`CreateAppContainerToken` (`dlls/kernelbase/security.c`)**: Replaced `TOKEN_USER user;` with `char buf[sizeof(TOKEN_USER) + 256];` so that `GetTokenInformation(TokenUser)` has ample space for the user SID. Caller rights check now succeeds cleanly.
   - **`dlls/kernelbase/tests/security.c`**: Added graceful skip (`skip(...)`) for `TokenCapabilities` (class 30) when unhandled by Wine `ntdll` / `wow64`.
   - **`dlls/userenv/tests/userenv.c`**: Replaced non-numeric mock SID with valid numeric subauthorities (`S-1-15-2-12345-67890-99999`) to prevent `ERROR_INVALID_SID` in `ConvertStringSidToSidW`, and bounded the `unterminated` buffer to prevent `RegSetValueExW` stack-boundary reads.
   - **Unit Test Verification**:
     * `userenv_test.exe`: 289 assertions, **0 failures** on both `x86_64` and `i386`.
     * `kernelbase_test.exe security`: 74 assertions, **0 failures** on both `x86_64` and `i386`.
   - Zero whitespace errors verified (`git diff --check HEAD~1`), pushed to GitHub remote `origin/codex/g4-appcontainer-correctness`.

---

## 2. Scope & Assigned Tasks

### Task 1: Wine Candidate Rebuild & Unit Test Verification
1. Check out Wine commit `4afbb4cd9753e185806c04f56e9c98a58a6984e1` on branch `codex/g4-appcontainer-correctness`.
2. Recompile `{kernelbase.dll, userenv.dll}` for both `x86_64-windows` and `i386-windows`.
3. Execute the unit test suites:
   - `dlls/userenv/tests/userenv_test.exe`
   - `dlls/kernelbase/tests/security_test.exe` (or `kernelbase_test.exe security`)
   Verify all assertions pass with 0 failures on both architectures. Record exact pass counts.
4. Prepare an isolated candidate directory in the guest (`/home/debian/wex-g4/wine-verify-r4-20261029/build/wow64`):
   - Copy base candidate and overlay rebuilt wow64 DLLs `{kernelbase, ntdll, userenv} x {x86_64, i386}`.
   - Verify SHA-256 hashes of deployed modules.
   - Ensure copy operations do NOT mutate shared hardlink inodes (`cp -a --remove-destination` or `rm && cp`).

### Task 2: Fresh Private Environment & Path-Locked Helper PE
1. Target Identity: `chromium-browser-verify-r4-20261029`
2. Configure isolated fresh paths in the guest:
   - **STAGE:** `/home/debian/wex-g4/chromium-browser-verify-r4-20261029-stage`
   - **OUTPUT:** `/home/debian/wex-g4/chromium-browser-verify-r4-20261029`
   - **PROFILE:** `/home/debian/wex-g4/chromium-browser-verify-r4-20261029/profile`
3. Preserve all prior trial directories (`chromium-browser-verify-r1-20261029`, `chromium-browser-verify-r2-20261029`, `chromium-browser-verify-r3-20261029`).
4. Build path-locked helper PE for `chromium-browser-verify-r4-20261029`:
   - `WEX_EXIT_OUTPUT = L"Z:\\home\\debian\\wex-g4\\chromium-browser-verify-r4-20261029"`
   - Logging channels: `WINEDEBUG=-all,+timestamp,+pid,+tid,+winediag,+process,+userenv,+security`
   - Chromium flags: level 3 scoped VLOG on sandbox and network service:
     `--vmodule=*/content/browser/*=3,*/content/browser/renderer_host/*=3,*/content/browser/loader/*=3,*/services/network/*=3,*/sandbox/*=3,*/base/process/*=3`
     `--log-net-log=.../netlog.json --net-log-max-size-mb=8 --net-log-duration=40`
     `--trace-startup --trace-startup-duration=35 --trace-startup-file=.../trace.json`
5. Reconcile `ownership_proven` validation in `execution.json` prior to launch.

### Task 3: Normal 8 GiB Verification Trial (Iteration 4)
1. Execute the single authorized 8 GiB verification trial (`launch.py: -m 8192`).
2. Ensure graceful QMP shutdown (`qmp_powerdown` -> guest clean shutdown -> `qemu_exit_status: 0`).
3. Extract and preserve full native evidence under:
   `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-browser-verify-r4-20261029\chromium-native-evidence\`
   including `chromium.log`, `helper.stderr.log`, `netlog.json`, `results.json`, `trace.json`, `serial.log`, `execution.json`, and terminal front screenshots (`terminal-front.png`).

### Task 4: Verification Reporting
1. Analyze the trace and native evidence:
   - **Criterion Q1:** Did `utility!kNetwork` initialize under `--service-sandbox-type=network` without fallback to `none` or pre-spawn buffer failure?
   - **Criterion Q2:** Did the exact-nonce loopback fixture load `index.html` (637 bytes) and `scene.js` (3750 bytes) with HTTP 200?
   - **Visual Verification:** Does `terminal-front.png` display green background, blue marker, magenta/cyan canvases, and nonce-matched ready window title?
   - **Consumer Status:** Report consumer `results.json` honestly (`observation_complete: false, passed: false` due to helper timeout) alongside observed page evidence.
2. Document full results with file:line citations and timing in:
   `docs/CHROMIUM_BROWSER_VERIFICATION_RESULT_20261029.md` (add "Iteration 4" section, preserving Iterations 1, 2, and 3).
3. Check 0 whitespace errors (`git diff --check HEAD~1`), commit on `codex/chromium-boundary-diagnostic`, and push to GitHub origin (`https://github.com/zcomputerwiz/WineExplorer.git`).
4. Reply back to `antigravity` via bridge message with outcome summary and commit hash.

---

## 3. Constraints & Invariants
- 8 GiB RAM confirmed (`-m 8192`).
- Fresh isolated paths for every attempt; no reuse or clobbering of prior paths.
- Graceful QMP shutdown (`qemu_exit_status: 0`).
- No modifications to VM user passwords, SSH, or LightDM.
- Check live usage before long operation: 5h >= 95% checkpoint, weekly >= 99% stop. Never consume credits.
