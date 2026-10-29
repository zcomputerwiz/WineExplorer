<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium Browser Boundary Work Order — 2026-10-28

Work order: `WO-20261028-CHROMIUM-BROWSER-BOUNDARY`
Lead Architect / Manager: Antigravity (Gemini)
Worker Agent: Claude Code (`mailbox: "claude"`)
Root Branch: `codex/chromium-navigation-handoff` (at commit `7bb9510e`)
Wine Submodule Target: Owned child branch cut from combined `02d4ef2`/`fffa7de`

---

## 1. Context & Architectural Root-Cause Discovery

The Antigravity Lead Architect has performed deep Perfetto trace decoding (`TraceProcessor` SQL analysis of interned slice arguments, flow tables, and thread controllers) on retained evidence (`wine_trace.pftrace` vs native `r5_trace.pftrace`, `helper.stderr.log`, `chromium.log`, and `netlog.json`), settling the exact failing operation and timing gap.

### The Precise Failing Sequence

1. **First Process Launch Attempt (`169651.576 ms`)**:
   - The browser process (PID 216) dispatches process launches sequentially to `ThreadPoolSingleThreadForegroundBlocking` (TID 500).
   - At `169651.576 ms`, slice `StartProcessWithAccess` with `debug.tag = utility!kNetwork` runs to spawn the Network Service under its default Windows security profile: `SandboxType::kNetwork` (Low Privilege AppContainer / LPAC).
   - Inside Chromium's `sandbox/policy/win/sandbox_win.cc`, `GeneratePolicyForSandboxedProcess` attempts to configure the AppContainer profile via `policy->SetLowBox("cr.sb.net")`, calling Windows `CreateAppContainerProfile`.
   - In project Wine (`dlls/userenv/userenv_main.c:738`), `CreateAppContainerProfile` is an unfulfilled stub that unconditionally returns `E_NOTIMPL`.
   - Consequently, `GeneratePolicyForSandboxedProcess` fails, and `StartProcessWithAccess` returns failure at `169674.886 ms` without calling `LaunchProcess` or `NtCreateUserProcess`. No process is created.

2. **Mojo Channel Teardown & Log Trigger (`170565.909 ms`)**:
   - The unspawned Network Service's pending Mojo remote is marked broken.
   - At `170565.909 ms`, `SimpleWatcher::OnHandleReady` on `CrBrowserMain` signals disconnection, and slice `Closed mojo endpoint: network.mojom.NetworkService` executes.
   - At `170565.978 ms`, `content/browser/network_service_instance_impl.cc:652` catches the disconnection and logs:
     `[216:220:1028/143959.929:ERROR:content\browser\network_service_instance_impl.cc:652] Network service crashed or was terminated, restarting service.`
   - Chromium's crash handler marks `fallback_to_unsandboxed_ = true` and posts an emergency restart with `--service-sandbox-type=none`.

3. **Navigation Premature Failure (`170281 ms` -> `170478 ms`)**:
   - Meanwhile, document navigation started at `170073 ms` (`NavigationRequest::Create`).
   - At `170281.111 ms`, `ThrottlingURLLoader::StartNow` attempted to use the `URLLoaderFactory` bound to the initial (now dead) network context.
   - At `170477.943 ms`, Mojo signaled endpoint closure on `network.mojom.URLLoader` and `network.mojom.URLLoaderClient`.
   - At `170478.008 ms`, `NavigationURLLoaderImpl::NotifyRequestFailed` aborted the target document request with `net::ERR_ABORTED`.

4. **Late Fallback Network Service Spawning (`170665 ms` -> `171609 ms`)**:
   - The unsandboxed fallback launch finally reached `CreateProcessInternalW` at `170665 ms`, creating PID 029c (668 decimal) at `170689 ms`.
   - PID 029c finished initialization (`NetworkService::Initialize`) at `171609 ms`—**~1.1 seconds AFTER the document navigation had already aborted**.

### Why Native Windows Succeeded
On native Windows 10 (r5 reference), `CreateAppContainerProfile` succeeds immediately. The Network Service launches under LPAC on the first attempt, initializes at `+42.8 ms` (pre-nav), and `CorsURLLoaderFactory::CreateLoaderAndStart` lands in the live service 0.26 ms after `StartNow`.

---

## 2. Concrete Wine Divergence & Assigned Work

In commit `cb1d117`, Wine implemented `AppContainerRegisterSid`, `AppContainerLookupMoniker`, and SID mappings in `dlls/kernelbase/security.c`, and `DeriveAppContainerSidFromAppContainerName` exists in `dlls/userenv/userenv_main.c:41`. However, `CreateAppContainerProfile` in `dlls/userenv/userenv_main.c:730` was left as an unimplemented stub returning `E_NOTIMPL`.

### Task 1: Focused Win32 PE Repro for `CreateAppContainerProfile`
- Create `docs/navhandoff-repro-20261028/appcontainer_profile_repro.c`.
- Repro calls `CreateAppContainerProfile(L"cr.sb.net.test", L"cr.sb.net.test", L"Test Description", NULL, 0, &sid)`.
- Asserts that `sid` is non-NULL, verifies that `ConvertSidToStringSidW` produces the expected AppContainer SID format (`S-1-15-2-...`), and tests cleanup via `DeleteAppContainerProfile(L"cr.sb.net.test")`.
- Accepts either `S_OK` or `HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)` (with valid SID populated).
- Build once with mingw and execute on:
  1. Native Windows 10 (NT 10.0.26300) -> Must pass exit 0.
  2. Actual project Wine candidate (`/var/tmp/wex-g4-token-parent-20261027/build`) -> Demonstrates `E_NOTIMPL` failure before fix.

### Task 2: Implement Supported Wine Fix on Owned Wine Child Branch
- Cut an owned child branch from `02d4ef2` (e.g., `codex/g4-appcontainer-profile`).
- In `dlls/userenv/userenv_main.c`:
  - Implement `CreateAppContainerProfile`:
    - Derive the SID from `container_name` using `DeriveAppContainerSidFromAppContainerName`.
    - Register the SID mapping via `AppContainerRegisterSid`.
    - Ensure package folder exists or is recorded under user profile.
    - Return `S_OK` (or `HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)` if already present) and populate `*container_sid`.
  - Implement `DeleteAppContainerProfile`:
    - Unregister via `AppContainerUnregisterSid`.
    - Return `S_OK`.
- Rebuild Wine candidate wow64 incrementally in `/var/tmp/wex-g4-token-parent-20261027/build/wow64`.
- Re-run `appcontainer_profile_repro.exe` against the rebuilt Wine build to verify PASS (exit 0).

### Task 3: Normal 8 GiB Browser Verification Trial
- Deploy the updated candidate build to the VM `/home/debian/wex-g4/wine-control-channel-20261027/build/wow64`.
- Run one authorized normal 8 GiB browser verification trial using the standard launcher contract:
  - `launch.py: -m 8192`
  - Normal sandbox / security enabled
  - Private loopback HTTP fixture
  - Preserved R4 disk image and VirGL identical
  - Graceful QMP shutdown (`qemu_exit_status: 0`)
- Verify target criteria:
  - Network Service initializes on the FIRST attempt (`utility!kNetwork` succeeds; no crash/restart log).
  - Loopback navigation reaches HTTP fixture: `index.html` 200, `scene.js` 200.
  - Page commits: `has_committed=true`, `net_error=0`.

### Task 4: Documentation & Delivery
- Document full findings, exact commit hashes, test results, and browser verification metrics in `docs/CHROMIUM_BROWSER_BOUNDARY_RESULT_20261028.md`.
- Verify `git diff --check HEAD~1` passes with zero whitespace errors.
- Push changes to origin on `codex/chromium-navigation-handoff`.
- Report back to `antigravity` on bridge mailbox `antigravity`.
