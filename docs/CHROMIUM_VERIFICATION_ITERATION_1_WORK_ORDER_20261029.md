<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work Order: Chromium Browser Verification Iteration 1 — 2026-10-29

- **Work Order ID:** `WO-20261029-CHROMIUM-VERIFICATION-ITERATION-1`
- **Lead Architect / Manager:** Antigravity (Gemini)
- **Implementer / Worker Agent:** Claude Code (`mailbox: "claude"`)
- **Parent Reviewer:** Codex (`mailbox: "codex"`)
- **Superproject Worktree:** `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer`
- **Root Branch:** `codex/chromium-boundary-diagnostic`
- **Wine Child Branch:** `codex/g4-appcontainer-correctness`
- **Wine Commit:** `3a47f7c3271b082ab82a10b7861ed7d4a137bb34` (pushed to `https://github.com/zcomputerwiz/wine.git`)
- **Target Identity:** `chromium-browser-verify-r1-20261029`

---

## 1. Context & Objective

Parent review `4f167e32` authorized:
> *"If instrumentation identifies a reproducible actual Wine defect, implement real semantic fix(es), targeted Windows-vs-Wine tests as needed, then up to TWO materially different justified browser verification iterations. No unnecessary second approval/paperwork gate, no unchanged replay or generic harness redesign."*

The single authorized 8 GiB diagnostic trial (`chromium-browser-diagnostic-20261028`, Run 1) definitively proved:
1. `AppContainerRegisterSid` was invoked for `utility!kNetwork` (`cr.sb.netED86A9060BF9087DCF4C7462DCD014893457BF94`) at `126.220`.
2. Exactly 3 ms later (`126.223`), `AppContainerUnregisterSid` was called (an immediate rollback/cleanup of the profile).
3. `CreateAppContainerProfile` / `AppContainerBase::CreateProfile` encountered immediate failure because Wine stored mappings under non-standard `Software\Wine\AppContainer\Mappings` (rather than standard Windows `Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppContainer\Mappings`), did not create the `%LOCALAPPDATA%\Packages\<container_name>\AC` directory hierarchy expected by Windows AppContainer callers, and did not implement `GetAppContainerFolderPath`.
4. As a direct consequence, `GeneratePolicyForSandboxedProcess` aborted before process spawn, triggering Chromium's `network_service_instance_impl.cc:652` ("Network service crashed or was terminated, restarting service") and fallback restart with unsandboxed `--service-sandbox-type=none`.
5. The in-flight navigation request for the target URL (`index.html`) was discarded when the initial sandboxed attempt aborted, resulting in zero HTTP requests reaching the fixture and a 45-second helper timeout.

Antigravity has implemented the semantic fix on Wine branch `codex/g4-appcontainer-correctness` (commit `3a47f7c3271b082ab82a10b7861ed7d4a137bb34`, verified on native Windows and published to GitHub origin).

Claude Code is now dispatched to execute **Verification Iteration 1** (`chromium-browser-verify-r1-20261029`).

---

## 2. Scope & Assigned Tasks

### Task 1: Wine Candidate Rebuild & Unit Test Verification
1. Check out Wine commit `3a47f7c3271b082ab82a10b7861ed7d4a137bb34` on branch `codex/g4-appcontainer-correctness`.
2. Recompile `userenv.dll` and `kernelbase.dll` for both x86_64 and i386.
3. Execute the new test suites:
   - `dlls/userenv/tests/userenv_test.exe` (specifically `test_appcontainer_profile`)
   - `dlls/kernelbase/tests/security_test.exe` (specifically `test_CreateAppContainerToken`)
   Verify all tests pass with zero failures.
4. Prepare an isolated candidate directory in the guest (e.g. `/home/debian/wex-g4/wine-appcontainer-verify-20261029/build/wow64`) with hash verification:
   - Verify SHA-256 hashes of deployed x86_64 and i386 `userenv.dll` and `kernelbase.dll`.
   - Do NOT overwrite shared loaded builds or prior candidate builds.

### Task 2: Fresh Private Environment & Path-Locked Helper PE
1. Target Identity: `chromium-browser-verify-r1-20261029`
2. Configure isolated fresh paths in the guest:
   - **STAGE:** `/home/debian/wex-g4/chromium-browser-verify-r1-20261029-stage`
   - **OUTPUT:** `/home/debian/wex-g4/chromium-browser-verify-r1-20261029`
   - **PROFILE:** `/home/debian/wex-g4/chromium-browser-verify-r1-20261029/profile`
3. Preserve all prior trial directories (`chromium-browser-boundary-20261028`, `.t1`, `.t2`, and `chromium-browser-diagnostic-20261028`).
4. Build path-locked helper PE for `chromium-browser-verify-r1-20261029` embedding:
   - `WEX_EXIT_OUTPUT = L"Z:\\home\\debian\\wex-g4\\chromium-browser-verify-r1-20261029"`
   - Logging channels: `WINEDEBUG=-all,+timestamp,+pid,+tid,+winediag,+process,+userenv,+security`
   - Chromium flags: `--vmodule=*/content/browser/*=2,*/content/browser/renderer_host/*=2,*/content/browser/loader/*=2,*/services/network/*=2,*/sandbox/*=2,*/base/process/*=2`
5. Reconcile `ownership_proven` validation in `execution.json` prior to launch.

### Task 3: Normal 8 GiB Verification Trial (Iteration 1)
1. Execute the 8 GiB verification trial (`launch.py: -m 8192`).
2. Ensure graceful QMP shutdown (`qmp_powerdown` -> guest clean shutdown -> `qemu_exit_status: 0`).
3. Extract and preserve full native evidence under:
   `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-browser-verify-r1-20261029\chromium-native-evidence\`
   including `chromium.log`, `helper.stderr.log`, `netlog.json`, `results.json`, `trace.json`, and terminal front screenshots.

### Task 4: Verification Reporting
1. Analyze the trace against the target criteria:
   - **Criterion A:** Did `utility!kNetwork` initialize under `--service-sandbox-type=network` without fallback to `none`?
   - **Criterion B:** Did `AppContainerRegisterSid` succeed and remain active without immediate rollback?
   - **Criterion C:** Did `CreateAppContainerToken` or `NtCreateLowBoxToken` succeed for the network child process?
   - **Criterion D:** Did `http://127.0.0.1:44175/.../index.html` load with HTTP 200 in the fixture?
   - **Criterion E:** Did WebGL readbacks and nonce check succeed?
2. Document full results with file:line citations and timing in:
   `docs/CHROMIUM_BROWSER_VERIFICATION_RESULT_20261029.md`.
3. Check 0 whitespace errors (`git diff --check HEAD~1`), commit on `codex/chromium-boundary-diagnostic`, and push to GitHub origin (`https://github.com/zcomputerwiz/WineExplorer.git`).
4. Reply back to `antigravity` via bridge message with outcome summary and commit hash.

---

## 3. Constraints & Invariants
- 8 GiB RAM confirmed (`-m 8192`).
- Fresh isolated paths for every attempt; no reuse of prior paths.
- Graceful QMP shutdown (`qemu_exit_status: 0`).
- No modifications to VM user passwords, SSH, or LightDM.
- Check usage before long operations: 5h >= 95% checkpoint, weekly >= 99% stop. Never consume credits.
