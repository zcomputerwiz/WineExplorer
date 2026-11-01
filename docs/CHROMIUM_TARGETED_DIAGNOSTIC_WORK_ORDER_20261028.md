<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium Targeted Diagnostic Work Order — 2026-10-28

Work order: `WO-20261028-CHROMIUM-TARGETED-DIAGNOSTIC`
Lead Architect / Manager: Antigravity (Gemini)
Worker Agent: Claude Code (`mailbox: "claude"`)
Root Branch: `codex/chromium-boundary-diagnostic` (at commit `6563cc23`)
Wine Branch: `codex/g4-appcontainer-profile` (published on GitHub `https://github.com/zcomputerwiz/wine.git` at commit `002153a4bd0c3174540afc2e01955373acc3d6f0`)

---

## 1. Context & Parent Directives

Codex has reviewed root `a9a92a40` and the two 8 GiB verification trials in parent work order `4f167e32` ([`docs/CHROMIUM_BROWSER_BOUNDARY_PARENT_REVIEW_WORK_ORDER_20261028.md`](docs/CHROMIUM_BROWSER_BOUNDARY_PARENT_REVIEW_WORK_ORDER_20261028.md)).

Key directives from Codex and user policy:
1. **Stop guessing**: Determine whether network sandbox creation is actually attempted for the target, and identify the relevant API/operation and first non-success result with process, timing, and error/status.
2. **Targeted Instrumentation**: One genuinely new normal 8 GiB browser diagnostic trial is authorized to capture the real caller and result.
3. **Fresh, Isolated Paths**: Never reuse or overwrite old run directories. Use fresh private candidate, stage, output, and profile paths (`chromium-browser-diagnostic-20261028`), and reconcile `ownership_proven`.
4. **Remote Ref**: The Wine branch `codex/g4-appcontainer-profile` has now been pushed directly to GitHub `https://github.com/zcomputerwiz/wine.git` and verified via `ls-remote` (`002153a4bd0c3174540afc2e01955373acc3d6f0`).

---

## 2. Chromium Static Code Analysis (Sandbox Launch Pipeline)

From our audit of Chromium source (`sandbox/policy/win/sandbox_win.cc`, `sandbox/win/src/broker_services.cc`, `sandbox/win/src/sandbox_policy_base.cc`, `base/win/access_token.cc`):

1. **Launcher Thread Entry Point** (`content/browser/child_process_launcher_helper_win.cc:170`):
   ```cpp
   *launch_result = sandbox::policy::SandboxWin::StartSandboxedProcess(
       *command_line(), options->handles_to_inherit, delegate_.get(), ...);
   ```

2. **Policy Generation Failure Gate** (`sandbox/policy/win/sandbox_win.cc:982-988`):
   ```cpp
   ResultCode result = GeneratePolicyForSandboxedProcess(
       cmd_line, handles_to_inherit, delegate, policy.get());
   if (SBOX_ALL_OK != result) {
     DWORD last_error = GetLastError();
     std::move(result_callback).Run(base::Process(), last_error, result);
     return result;
   }
   ```
   **Crucial Insight**: If `GeneratePolicyForSandboxedProcess` fails, `StartProcessWithAccess::LAUNCHPROCESS` is NEVER called, no child process is ever created, and the callback immediately fails.

3. **Inside `GeneratePolicyForSandboxedProcess` for `Sandbox::kNetwork`** (`sandbox_win.cc:586-595`):
   ```cpp
   std::string appcontainer_id;
   if (SandboxWin::IsAppContainerEnabledForSandbox(cmd_line, sandbox_type) &&
       delegate->GetAppContainerId(&appcontainer_id)) {
     if (const auto result = SandboxWin::AddAppContainerProfileToConfig(
             cmd_line, sandbox_type, appcontainer_id, config);
         result != SBOX_ALL_OK) {
       return result;
     }
   }
   ```

4. **Inside `AddAppContainerProfileToConfig`** (`sandbox_win.cc:787-809`):
   - Step A: `ResultCode result = config->AddAppContainerProfile(profile_name);` -> calls `AppContainerBase::CreateProfile(package_name, L"Chrome Sandbox")`.
   - Step B: `result = SetupAppContainerProfile(config->GetAppContainer(), command_line, sandbox_type);` -> configures capabilities.
   - Step C: `bool access_check = config->GetAppContainer()->AccessCheck(program.value(), base::win::SecurityObjectType::kFile, GENERIC_READ | GENERIC_EXECUTE, &granted_access, &granted_access_status) && granted_access_status;` -> returns `SBOX_ERROR_CREATE_APPCONTAINER_ACCESS_CHECK` if access check fails.

5. **Token Generation Gate** (`sandbox/win/src/sandbox_policy_base.cc:606-619`):
   - `primary = app_container->BuildPrimaryToken(*primary);` -> calls `AccessToken::CreateAppContainer` (`kernelbase!CreateAppContainerToken`).
   - `ReplacePackageSidInDacl(primary->get(), app_container->GetPackageSid(), TOKEN_ALL_ACCESS)` -> modifies DACL on the lowbox token.
   - `impersonation = app_container->BuildImpersonationToken(*impersonation);`

---

## 3. Tasks Assigned to Claude Code

### Task 1: Prepare Targeted Diagnostic Instrumentation
- **New Identity**: `chromium-browser-diagnostic-20261028`
  - STAGE: `/home/debian/wex-g4/chromium-browser-diagnostic-20261028-stage`
  - OUTPUT: `/home/debian/wex-g4/chromium-browser-diagnostic-20261028`
  - PROFILE: `/home/debian/wex-g4/chromium-browser-diagnostic-20261028/profile`
- **Consumer Configuration** (in `scripts/check-g4-chromium-native-exit.py` or diagnostic mode):
  - Add debug channels to `WINEDEBUG`:
    `WINEDEBUG=-all,+timestamp,+pid,+tid,+winediag,+process,+userenv,+security`
  - Add sandbox & subprocess logging to Chromium command-line:
    `--vmodule=*/content/browser/*=2,*/content/browser/renderer_host/*=2,*/content/browser/loader/*=2,*/services/network/*=2,*/sandbox/*=2,*/base/process/*=2`
- **Helper PE**:
  - Compile fresh path-locked helper PE for `chromium-browser-diagnostic-20261028`.

### Task 2: Fresh Private Environment & Ownership Validation
- Verify ownership proof before launch (`validate_response(PROBE, ...)`).
- Ensure no shared loaded build writes or overwriting of prior attempt directories (`chromium-browser-boundary-20261028` and `.t1` must remain preserved).

### Task 3: Execute Single Authorized 8 GiB Diagnostic Trial
- Execute normal 8 GiB diagnostic trial (`launch.py: -m 8192`).
- Verify graceful QMP shutdown (`qemu_exit_status: 0`).
- Extract the newly captured logs:
  - `helper.stderr.log` (now logging all `+userenv` and `+security` TRACE calls).
  - `chromium.log` (now logging all `*/sandbox/*=2` policy generation and access check steps).
  - `trace.json` / Perfetto slices.

### Task 4: Root-Cause Synthesis & Next Steps
- Analyze the trace to answer:
  1. Did `GeneratePolicyForSandboxedProcess` get called for `utility!kNetwork`?
  2. Did `AddAppContainerProfileToConfig` get called? Which step failed (Profile creation, Capability setup, or Executable AccessCheck)?
  3. Did `CreateAppContainerToken` or `ReplacePackageSidInDacl` get called? What were the return codes?
- Document full findings in `docs/CHROMIUM_BROWSER_DIAGNOSTIC_RESULT_20261028.md`.
- Verify zero whitespace errors (`git diff --check HEAD~1`), commit on `codex/chromium-boundary-diagnostic`, push to origin, and report back to `antigravity`.
