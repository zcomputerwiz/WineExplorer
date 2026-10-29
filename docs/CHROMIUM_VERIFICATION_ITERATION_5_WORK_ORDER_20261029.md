<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work Order: Chromium Browser Verification Iteration 5 — 2026-10-29

- **Work Order ID:** `WO-20261029-CHROMIUM-VERIFICATION-ITERATION-5`
- **Lead Architect / Manager:** Antigravity (Gemini)
- **Implementer / Worker Agent:** Claude Code (`mailbox: "claude"`)
- **Parent Reviewer:** Codex (`mailbox: "codex"`)
- **Superproject Worktree:** `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer`
- **Root Branch:** `codex/chromium-boundary-diagnostic`
- **Wine Child Branch:** `codex/g4-appcontainer-correctness`
- **Wine Commit:** `f01bba4564c78351fb3e753b844ca83842c6c39f` (pushed to `https://github.com/zcomputerwiz/wine.git`)
- **Target Identity:** `chromium-browser-verify-r5-20261029`

---

## 1. Context & Parent Review Findings Resolution

Following the successful execution of Iteration 4 (`chromium-browser-verify-r4-20261029`, commit `34fb016d`), which confirmed that the `TokenUser` 272-byte buffer fix eliminated the `0x7A` (`ERROR_INSUFFICIENT_BUFFER`) regression and preserved the sandboxed `utility!kNetwork` launch with HTTP 200 and visual rendering, parent reviewer Codex issued Parent Review `docs/CHROMIUM_R4_CANDIDATE_PARENT_REVIEW_20261029.md` (parent commit `eb3b5b0755da072809dec672abbbe0fb6a71d8b9`).

This Work Order directs the implementation, testing, and VM verification of the remaining Parent Review findings:

### Finding 1 (P1): Missing Profile Deletion Must Preserve Unowned Folders
- **Observation:** `DeleteAppContainerProfile` in `dlls/userenv/userenv_main.c` previously mapped `ERROR_FILE_NOT_FOUND` from unregister to `S_OK`, after which line 871 (`if (SUCCEEDED(hr))`) entered directory cleanup and called `RemoveDirectoryW` on `%LOCALAPPDATA%\Packages\<name>\AC` and `%LOCALAPPDATA%\Packages\<name>`. Consequently, deleting an unregistered/unowned profile could remove an existing empty directory structure on disk.
- **Fix:** Track `BOOL unregister_succeeded = (hr == S_OK);` before mapping `ERROR_FILE_NOT_FOUND` to `S_OK`. Directory removal is guarded by `if (unregister_succeeded)`, ensuring that file-system cleanup is executed only when this operation positively established ownership of the registered profile. Pre-existing unowned directories, empty `AC` subdirectories, and sentinel files are preserved intact.
- **Unit Test:** Added test in `dlls/userenv/tests/userenv.c` creating an unregistered profile folder hierarchy with an empty `AC` subdirectory and a `sentinel.txt` file, calling `DeleteAppContainerProfile`, and asserting that all directories and files remain untouched on disk (`S_OK` returned). Uses `broken(...)` to account for native Windows deleting unowned folders if run on native Windows.

### Finding 2 (P2): Profile Creation Directory Kind Validation & Collision Rollback
- **Observation:** `CreateAppContainerProfile` in `dlls/userenv/userenv_main.c` accepted `ERROR_ALREADY_EXISTS` from `CreateDirectoryW` without verifying that the existing path is actually a directory (`FILE_ATTRIBUTE_DIRECTORY`). A regular file located at `Packages\<name>\AC` or `Packages\<name>` would falsely report success while leaving no usable profile directory.
- **Fix:** Introduced `ensure_directory(path)` helper which checks `attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)` when `CreateDirectoryW` returns `ERROR_ALREADY_EXISTS` or `ERROR_FILE_EXISTS`. If a regular file collision exists, `ensure_directory` sets `ERROR_ALREADY_EXISTS` and returns `FALSE`. The function branches to `err_fs`, rolls back the registration via `pAppContainerUnregisterSid(sid)`, frees the SID, preserves the colliding regular file without modification, and returns `HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)`.
- **Unit Test:** Added test in `dlls/userenv/tests/userenv.c` creating a regular file at `Packages\<name>\AC`, calling `CreateAppContainerProfile`, and verifying that the call fails, the registration mapping is rolled back from the registry, and the collision file remains an intact regular file.

### Finding 3 (P3): Security Semantics & Effective Access Documentation
- **Clarification:** Wine's token implementation sets `token->integrity_rid = SECURITY_MANDATORY_LOW_RID` and records capability SIDs in `token->capabilities`.
- **Limitations & Boundaries:**
  1. `NtQueryInformationToken(TokenCapabilities)` (class 30) is not implemented in Wine `ntdll` / `wow64` and returns `STATUS_NOT_IMPLEMENTED`.
  2. Wine server does NOT implement Windows NT lowbox DACL filtering (where DACLs lacking `ALL APPLICATION PACKAGES` or AppContainer/capability SIDs are denied access even if standard User/Group SIDs are present).
  3. Therefore, Wine does not enforce native capability isolation; it provides structural lowbox token metadata and Low integrity labeling. No false isolation claims or success stubs are asserted.

---

## 2. Scope & Assigned Tasks

### Task 1: Wine Candidate Rebuild & Unit Test Verification
1. Check out Wine commit `f01bba4564c78351fb3e753b844ca83842c6c39f` on branch `codex/g4-appcontainer-correctness`.
2. Recompile `{kernelbase.dll, userenv.dll}` for both `x86_64-windows` and `i386-windows`.
3. Execute the unit test suites:
   - `dlls/userenv/tests/userenv_test.exe`
   - `dlls/kernelbase/tests/security_test.exe` (or `kernelbase_test.exe security`)
   Verify all assertions pass with 0 failures on both architectures. Record exact pass counts.
4. Prepare an isolated candidate directory in the guest (`/home/debian/wex-g4/wine-verify-r5-20261029/build/wow64`):
   - Copy base candidate and overlay rebuilt wow64 DLLs `{kernelbase, ntdll, userenv} x {x86_64, i386}`.
   - Verify SHA-256 hashes of deployed modules.
   - Ensure copy operations do NOT mutate shared hardlink inodes (`cp -a --remove-destination` or `rm && cp`).

### Task 2: Fresh Private Environment & Path-Locked Helper PE
1. Target Identity: `chromium-browser-verify-r5-20261029`
2. Configure isolated fresh paths in the guest:
   - **STAGE:** `/home/debian/wex-g4/chromium-browser-verify-r5-20261029-stage`
   - **OUTPUT:** `/home/debian/wex-g4/chromium-browser-verify-r5-20261029`
   - **PROFILE:** `/home/debian/wex-g4/chromium-browser-verify-r5-20261029/profile`
3. Preserve all prior trial directories (`chromium-browser-verify-r1-20261029` through `chromium-browser-verify-r4-20261029`).
4. Build path-locked helper PE for `chromium-browser-verify-r5-20261029`:
   - `WEX_EXIT_OUTPUT = L"Z:\\home\\debian\\wex-g4\\chromium-browser-verify-r5-20261029"`
   - Logging channels: `WINEDEBUG=-all,+timestamp,+pid,+tid,+winediag,+process,+userenv,+security`
   - Chromium flags: level 3 scoped VLOG on sandbox and network service:
     `--vmodule=*/content/browser/*=3,*/content/browser/renderer_host/*=3,*/content/browser/loader/*=3,*/services/network/*=3,*/sandbox/*=3,*/base/process/*=3`
     `--log-net-log=.../netlog.json --net-log-max-size-mb=8 --net-log-duration=40`
     `--trace-startup --trace-startup-duration=35 --trace-startup-file=.../trace.json`
5. Reconcile `ownership_proven` validation in `execution.json` prior to launch.

### Task 3: Normal 8 GiB Verification Trial (Iteration 5)
1. Execute ONE normal 8 GiB verification trial (`launch.py: -m 8192`).
2. Ensure graceful QMP shutdown (`qmp_powerdown` -> guest clean shutdown -> `qemu_exit_status: 0`).
3. Extract and preserve full native evidence under:
   `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-browser-verify-r5-20261029\chromium-native-evidence\`
   including `chromium.log`, `helper.stderr.log`, `netlog.json`, `results.json`, `trace.json`, `serial.log`, `execution.json`, and terminal front screenshots (`terminal-front.png`).

### Task 4: Verification Reporting
1. Analyze the trace and native evidence:
   - **Criterion Q1:** Did `utility!kNetwork` initialize under `--service-sandbox-type=network` without fallback to `none` or pre-spawn buffer failure?
   - **Criterion Q2:** Did the exact-nonce loopback fixture load `index.html` (637 bytes) and `scene.js` (3750 bytes) with HTTP 200?
   - **Visual Verification:** Does `terminal-front.png` display green background, blue marker, magenta/cyan canvases, and nonce-matched ready window title?
   - **Consumer Status:** Report consumer `results.json` honestly (`observation_complete: false, passed: false` due to helper timeout) alongside observed page evidence.
2. Document full results with file:line citations and timing in:
   `docs/CHROMIUM_BROWSER_VERIFICATION_RESULT_20261029.md` (add "Iteration 5" section, preserving Iterations 1-4).
3. Check 0 whitespace errors (`git diff --check HEAD~1`), commit on `codex/chromium-boundary-diagnostic`, and push to GitHub origin (`https://github.com/zcomputerwiz/WineExplorer.git`).
4. Reply back to `antigravity` via bridge message with outcome summary and commit hash.

---

## 3. Constraints & Invariants
- 8 GiB RAM confirmed (`-m 8192`).
- Fresh isolated paths for every attempt; no reuse or clobbering of prior paths.
- Graceful QMP shutdown (`qemu_exit_status: 0`).
- No modifications to VM user passwords, SSH, or LightDM.
- Check live usage before long operation: 5h >= 95% checkpoint, weekly >= 99% stop. Never consume credits.
