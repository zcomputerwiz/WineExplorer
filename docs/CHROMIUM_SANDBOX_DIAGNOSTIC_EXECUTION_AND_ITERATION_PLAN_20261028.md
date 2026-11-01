<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium Sandbox Diagnostic Execution & Iteration Plan — 2026-10-28

Lead Architect / Manager: Antigravity (Gemini)  
Worker Agent: Claude Code (`mailbox: "claude"`)  
Root Branch: `codex/chromium-boundary-diagnostic`  
Wine Branch: `codex/g4-appcontainer-profile` (GitHub `002153a4bd0c3174540afc2e01955373acc3d6f0`)

---

## 1. Executive Summary & Objective

Codex parent review `4f167e32` mandated that we stop guessing and synthetically probing isolated stubs. Instead, we must identify whether network sandbox creation is actually attempted for the target, and identify the relevant API/operation and first non-success result with process, timing, and error/status.

The user has explicitly authorized iterative targeted diagnostics:
> *"If a definitive cause is not identified with the first run, plan the next to continue targeted diagnostics to determine the cause and iterate. Remember we have the static code analysis for Chromium available to reference for guidance as well."*

This document formalizes the full execution pipeline, failure classification matrix, and iteration roadmap across up to two authorized verification iterations.

---

## 2. Chromium Sandbox Launch Pipeline (Static Architecture)

Tracing the execution flow from the Chromium browser process through to sandboxed utility process creation:

```
[Browser Main / UI Thread]
  │
  ▼
content::ChildProcessLauncherHelper::LaunchOnLauncherThread
  │
  ▼
sandbox::policy::SandboxWin::StartSandboxedProcess
  │
  ├── Gate 1: GeneratePolicyForSandboxedProcess
  │     │
  │     ├── Gate 1A: SandboxWin::AddAppContainerProfileToConfig (Sandbox::kNetwork)
  │     │     ├── Step 1: config->AddAppContainerProfile(profile_name)
  │     │     │     └── userenv!CreateAppContainerProfile
  │     │     ├── Step 2: SetupAppContainerProfile
  │     │     │     └── container->AddCapability(...)
  │     │     └── Step 3: config->GetAppContainer()->AccessCheck(program, FILE, READ|EXEC)
  │     │           [Failure Signature: SBOX_ERROR_CREATE_APPCONTAINER_ACCESS_CHECK]
  │     │           [VLOG: "Sandbox cannot access executable ... Check filesystem permissions"]
  │     │
  │     └── Gate 1B: config_base->Freeze()
  │
  ▼
sandbox::BrokerServicesBase::SpawnTargetAsync
  │
  ├── Gate 2: PreSpawnTarget
  │     │
  │     ├── Gate 2A: policy_base->MakeTokens()
  │     │     ├── app_container->BuildPrimaryToken(*primary)
  │     │     │     └── kernelbase!CreateAppContainerToken -> NtCreateLowBoxToken
  │     │     │         [Failure Signature: SBOX_ERROR_CANNOT_CREATE_LOWBOX_TOKEN]
  │     │     ├── ReplacePackageSidInDacl(primary->get(), package_sid, TOKEN_ALL_ACCESS)
  │     │     │     ├── GetSecurityInfo(token, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, ...)
  │     │     │     └── SetSecurityInfo(token, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, ...)
  │     │     │         [Failure Signature: SBOX_ERROR_CANNOT_MODIFY_LOWBOX_TOKEN_DACL]
  │     │     └── app_container->BuildImpersonationToken(*impersonation)
  │     │           └── kernelbase!CreateAppContainerToken -> NtCreateLowBoxToken
  │     │
  │     ├── Gate 2B: policy_base->InitJob()
  │     │     └── CreateJobObjectW / SetInformationJobObject
  │     │
  │     └── Gate 2C: startup_info->BuildStartupInformation()
  │           ├── UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, ...)
  │           ├── UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY, ...)
  │           └── UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_JOB_LIST, ...)
  │               [Failure Signature: SBOX_ERROR_PROC_THREAD_ATTRIBUTES]
  │
  ▼
sandbox::BrokerServicesBase::CreateTarget (Worker Thread)
  │
  ├── Gate 3: CreateSandboxProcess
  │     │
  │     └── ::CreateProcessAsUserW(tokens.lockdown_.get(), program, args, ..., startup_info)
  │           └── Wine kernelbase!CreateProcessInternalW:
  │                 ├── NtCreateLowBoxToken(&appcontainer_token, base_token, ...)
  │                 │     [Redundant second LowBox token creation on tokens.lockdown_]
  │                 └── create_nt_process(token, ...)
  │                       [Failure Signature: SBOX_ERROR_CREATE_PROCESS]
  │
  ├── Gate 4: Impersonation Setup
  │     │
  │     ├── ::SetThreadToken(&temp_thread, tokens.initial_.get())
  │     └── CheckImpersonationToken(process_info.thread_handle())
  │           [Failure Signature: SBOX_ERROR_SET_THREAD_TOKEN]
  │
  ▼
[Child Process Spawned: utility!kNetwork]
  │
  ├── Gate 5: Child Process Initialization
  │     ├── ntdll / kernelbase process bootstrap under LowBox token
  │     ├── Chromium TargetServices::Init (lowbox lockdown)
  │     ├── Mojo IPC channel connection to browser broker
  │     └── NetworkService::Create and socket creation under LPAC
  │           [Failure Signature: Early crash or termination -> Exit Code / Exception]
```

---

## 3. Diagnostic Signature Matrix & Remediation Actions

| Gate | Failing Operation | Expected Log / Trace Signature | Remediation Action |
| :--- | :--- | :--- | :--- |
| **Gate 1A** | Profile Creation | `userenv:CreateAppContainerProfile` fails or registration error | Fix `dlls/userenv` profile registration semantics and registry publication |
| **Gate 1A** | Executable AccessCheck | `chromium.log`: `"Sandbox cannot access executable... Check filesystem permissions"`<br>`ResultCode: SBOX_ERROR_CREATE_APPCONTAINER_ACCESS_CHECK` | Implement proper `AccessCheck` / file ACL evaluation for AppContainer SIDs in Wine |
| **Gate 2A** | Primary Token Generation | `kernelbase:CreateAppContainerToken` returns NULL or error<br>`ResultCode: SBOX_ERROR_CANNOT_CREATE_LOWBOX_TOKEN` | Fix `NtCreateLowBoxToken` parameter handling or Wine server token allocation |
| **Gate 2A** | Token DACL Modification | `advapi32:SetSecurityInfo` on `token` returns error<br>`ResultCode: SBOX_ERROR_CANNOT_MODIFY_LOWBOX_TOKEN_DACL` | Fix `wineserver token_set_sd` DACL handling for token objects |
| **Gate 2C** | Startup Attributes | `kernelbase:UpdateProcThreadAttribute` returns FALSE<br>`ResultCode: SBOX_ERROR_PROC_THREAD_ATTRIBUTES` | Update `validate_proc_thread_attribute` or attribute list storage in `dlls/kernelbase/process.c` |
| **Gate 2C / 3** | Process Creation | `kernelbase:CreateProcessAsUserW` fails (`GetLastError != 0`)<br>`ResultCode: SBOX_ERROR_CREATE_PROCESS` | Fix `CreateProcessInternalW` token inheritance or redundant `NtCreateLowBoxToken` call |
| **Gate 4** | Thread Impersonation | `kernelbase:SetThreadToken` fails on cross-process thread<br>`ResultCode: SBOX_ERROR_SET_THREAD_TOKEN` | Fix `NtSetInformationThread(ThreadImpersonationToken)` cross-process permissions |
| **Gate 5** | Early Child Crash | Process exits immediately; child stderr or crash dump present | Fix child runtime / Mojo initialization or LPAC socket binding permissions in `ws2_32` / `afd` |

---

## 4. Iteration Protocol (Authorized Runs)

1. **Run 1 (Targeted Diagnostic Run — `chromium-browser-diagnostic-20261028`)**:
   - Authorized 8 GiB trial with enriched debug channels:
     `WINEDEBUG=-all,+timestamp,+pid,+tid,+winediag,+process,+userenv,+security`
     `--vmodule=*/content/browser/*=2,*/content/browser/renderer_host/*=2,*/content/browser/loader/*=2,*/services/network/*=2,*/sandbox/*=2,*/base/process/*=2`
   - Purpose: Pinpoint which of Gates 1–5 is the first failing operation.
   - Outputs: `helper.stderr.log`, `chromium.log`, `trace.json`.

2. **Decision Point & Run 2 Planning**:
   - If Run 1 pinpoints an in-process policy gate (Gates 1–4):
     * Develop targeted semantic fix in Wine child branch.
     * Validate against standalone Windows-vs-Wine test.
     * Rebuild and deploy to fresh isolated guest path.
     * Execute **Verification Iteration 1**.
   - If Run 1 shows process spawn succeeds (Gate 3/4 pass) but child crashes during Mojo/network bootstrap (Gate 5):
     * Enable child process debug tracing (`WINEDEBUG=+all` on child or Chromium network debug log).
     * Isolate socket/IPC defect and execute **Verification Iteration 1**.
   - If Run 1 leaves ambiguity between two adjacent gates:
     * Add precise opt-in semantically inert tracepoints in Wine child (`dlls/kernelbase/process.c` or `dlls/kernelbase/security.c`).
     * Execute targeted disambiguation run.

3. **Verification Iteration 2 (Final Validation)**:
   - Reserved for full end-to-end confirmation:
     * `utility!kNetwork` initializes without fallback to `--service-sandbox-type=none`.
     * `http_fixture` loopback navigation succeeds (`index.html 200`, `scene.js 200`).
     * Page commits (`has_committed=true`, `net_error=0`).
     * Clean QMP shutdown (`qemu_exit_status: 0`).
