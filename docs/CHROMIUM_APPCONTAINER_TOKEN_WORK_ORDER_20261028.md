<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium AppContainer Token Work Order — 2026-10-28

Work order: `WO-20261028-CHROMIUM-APPCONTAINER-TOKEN`
Lead Architect / Manager: Antigravity (Gemini)
Worker Agent: Claude Code (`mailbox: "claude"`)
Root Branch: `codex/chromium-navigation-handoff`
Wine Branch Target: `codex/g4-appcontainer-profile` in `/var/tmp/wex-g4-token-parent-20261027/wine`

---

## 1. Context & Root Cause Analysis

In Trial 1 of `WO-20261028-CHROMIUM-BROWSER-BOUNDARY`, `userenv!CreateAppContainerProfile` and `DeleteAppContainerProfile` were implemented, built, deployed, and verified in the guest Wine build. However, Trial 1 resulted in a negative outcome: Chromium still logged `Network service crashed or was terminated, restarting service.` and fell back to `--service-sandbox-type=none`.

Antigravity performed binary string, export table, and Chromium source inspection (`base/win/access_token.cc` and `sandbox/policy/win/sandbox_win.cc`) to determine why:

1. **`CreateAppContainerProfile` is NEVER called by Chromium**:
   - Binary string search on `chrome.dll` and `chrome.exe` confirms zero occurrences of `CreateAppContainerProfile` (ASCII or UTF-16).
   - Chromium does not use profile storage for sandbox utility processes.

2. **The Actual Chromium LPAC Token Creation Primitive**:
   - In Chromium source [`base/win/access_token.cc:753`](base/win/access_token.cc#L753-L780) (`AccessToken::CreateAppContainer`):
     ```cpp
     static const CreateAppContainerTokenFunction CreateAppContainerToken =
         reinterpret_cast<CreateAppContainerTokenFunction>(::GetProcAddress(
             ::GetModuleHandle(L"kernelbase.dll"), "CreateAppContainerToken"));
     if (!CreateAppContainerToken) {
       ::SetLastError(ERROR_PROC_NOT_FOUND);
       return std::nullopt;
     }
     ```
   - Chromium dynamically resolves `kernelbase!CreateAppContainerToken` at runtime.
   - When `CreateAppContainerToken` is missing, `AccessToken::CreateAppContainer` returns `std::nullopt`, causing sandboxed `utility!kNetwork` policy setup to fail immediately before process spawn!

3. **Definitive Wine Defect**:
   - In project Wine (`dlls/kernelbase/kernelbase.spec:180`):
     ```spec
     # @ stub CreateAppContainerToken
     ```
   - The export is commented out. `GetProcAddress(kernelbase, "CreateAppContainerToken")` returns `NULL` and sets `ERROR_PROC_NOT_FOUND`.
   - Wine already has complete underlying support in `dlls/kernelbase/process.c:733-739`, where it invokes `NtCreateLowBoxToken` with `OBJECT_ATTRIBUTES`, `sec_cap->AppContainerSid`, and `sec_cap->Capabilities`.

---

## 2. Tasks Assigned to Claude Code

### Task 1: Check in and Validate Focused PE Repro
- Repro file provided at [`docs/navhandoff-repro-20261028/appcontainer_token_repro.c`](docs/navhandoff-repro-20261028/appcontainer_token_repro.c).
- Commit this repro to root branch `codex/chromium-navigation-handoff`.
- Compile with `x86_64-w64-mingw32-gcc -O2 -o appcontainer_token_repro.exe docs/navhandoff-repro-20261028/appcontainer_token_repro.c -luserenv`.
- Verify behavior:
  - On Native Windows 10: passes, exit 0 (`SUCCESS: CreateAppContainerToken created token handle ...`).
  - On Project Wine (pre-fix): fails, exit 2 (`GetProcAddress(kernelbase, CreateAppContainerToken) FAILED`).

### Task 2: Implement `CreateAppContainerToken` in Wine & Rebuild DLL
- On Wine branch `codex/g4-appcontainer-profile` in `/var/tmp/wex-g4-token-parent-20261027/wine`:
  1. In `dlls/kernelbase/kernelbase.spec`:
     Uncomment line 180 and export:
     ```spec
     @ stdcall CreateAppContainerToken(ptr ptr ptr)
     ```
  2. In `dlls/kernelbase/security.c`:
     Implement `CreateAppContainerToken`:
     ```c
     /******************************************************************************
      * CreateAppContainerToken   (kernelbase.@)
      */
     BOOL WINAPI CreateAppContainerToken( HANDLE token, PSECURITY_CAPABILITIES sec_cap, PHANDLE appcontainer_token )
     {
         OBJECT_ATTRIBUTES oa = { sizeof(oa) };
         NTSTATUS status;

         TRACE( "(%p, %p, %p)\n", token, sec_cap, appcontainer_token );

         if (!sec_cap || !sec_cap->AppContainerSid || !appcontainer_token)
         {
             SetLastError( ERROR_INVALID_PARAMETER );
             return FALSE;
         }

         if (!token)
             token = GetCurrentProcessToken();

         status = NtCreateLowBoxToken( appcontainer_token, token, TOKEN_ALL_ACCESS, &oa,
                                       sec_cap->AppContainerSid,
                                       sec_cap->CapabilityCount,
                                       sec_cap->Capabilities ? (SID_AND_ATTRIBUTES *)sec_cap->Capabilities : NULL,
                                       0, NULL );
         return set_ntstatus( status );
     }
     ```
  3. Rebuild `dlls/kernelbase` for both architectures in `/var/tmp/wex-g4-token-parent-20261027/build/wow64`:
     - Both `x86_64-windows` and `i386-windows`.
  4. Verify the rebuilt Wine passes the repro:
     - Run `appcontainer_token_repro.exe` under the rebuilt Wine. Verify it exits 0.
  5. Check `git diff --check` for zero whitespace errors, commit to `codex/g4-appcontainer-profile`, and push to `origin/codex/g4-appcontainer-profile`.

### Task 3: Trial 2 — Second Normal 8 GiB Browser Verification Run
- Deploy the rebuilt candidate `kernelbase.dll` for both `x86_64` and `i386` to the guest Wine build path:
  `/home/debian/wex-g4/wine-control-channel-20261027/build/wow64/dlls/kernelbase/<arch>/kernelbase.dll`
  (retaining the deployed `userenv.dll` from Trial 1).
- Verify guest sha256 checksums match host build.
- Execute the second normal 8 GiB browser trial using the existing trial framework (`chromium-browser-boundary-trial.py`).
- Evaluate Target Success Criteria:
  1. Sandboxed Network Service initializes on the FIRST attempt (`utility!kNetwork` succeeds without crashing or falling back to `--service-sandbox-type=none`).
  2. No crash/restart line in `chromium.log` (`Network service crashed or was terminated, restarting service.` is ABSENT).
  3. Loopback navigation reaches fixture: `index.html` 200.
  4. Loopback navigation reaches fixture: `scene.js` 200.
  5. Page commits: `has_committed=true`.
  6. `net_error=0`.
  7. Graceful QMP shutdown: `qemu_exit_status: 0`.

### Task 4: Documentation & Handoff
- Append Trial 2 results, checksums, logs, and evaluation to [`docs/CHROMIUM_BROWSER_BOUNDARY_RESULT_20261028.md`](docs/CHROMIUM_BROWSER_BOUNDARY_RESULT_20261028.md).
- Update the superproject gitlink for submodule `wine` to the new commit on `codex/g4-appcontainer-profile`.
- Verify clean whitespace: `git diff --check HEAD~1`.
- Commit on `codex/chromium-navigation-handoff` and push to origin.
- Send a completion message via `bridge_send_message` with `sender: "claude"`, `recipient: "antigravity"`.
