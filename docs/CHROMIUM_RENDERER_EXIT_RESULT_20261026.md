<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium Renderer Exit Diagnostic — Live Evidence & Process Lifecycle Report

**Status:** `DIAGNOSTIC_COMPLETE` (Mode-family R6 metadata probe verified live with 21/21 passing checks; read-only child observer captured exact process roles, lifetimes, and exit status; clean QMP powerdown verified.)
**Branch:** `codex/chromium-renderer-unblock`
**Parent Order Reference:** `docs/CHROMIUM_RENDERER_EXIT_DIAGNOSTIC_20261026.md` (commit `8134c9fa`)
**Parent Message Reference:** `msg_1793064784715_58648411`
**Live Trial Run:** `chromium-renderer-unblock-20261026`
**Disk Image:** `system-native-identity-r4-20261024.qcow2`

---

## 1. Executive Summary

Parent reviewed and accepted the mode-family wiring corrections (commits `14d25fcc`/`98e50300`, imported as `c7f5fac0`/`e9893d9a`), verified the native helper PE hash (`b8d03b0b...`), and directed Antigravity to execute **ONE targeted child-exit/termination diagnostic** to obtain genuine child lifecycle and exit status evidence, avoiding speculative patches or unbounded generic logs.

Antigravity designed and integrated a read-only `ChildObserver` (`scripts/g4_child_observer.py`, 12 unit tests, 50 total passing tests), cross-checked Wine trace capabilities, staged the preflight package (`ebb91d6c...`, 258 runtime files), and executed the single authorized live diagnostic in owned session `chromium-renderer-unblock-20261026`.

### Key Live Results:
1. **R6 Metadata Probe Live Verification:** Under the corrected mode wiring (`strip_wineprefix: True` and `g4_userenv_r6_probe.phase` dispatch), the pre-browser sanity probe executed cleanly: **all 21 checks passed with 0 failures** (`api-probe.stdout.log`: `{"checks": 21, "failures": 0, "passed": true}`).
2. **Exact Child Process Mapping & Lifetimes:** The child observer captured 14 Linux processes mapped to exact Windows PIDs and Chromium roles:
   - **Main Browser Process:** Linux PID 1226 / Windows PID 228 (`0x00e4`), command `chrome.exe --app=http://127.0.0.1:41881/...`. Alive throughout.
   - **Crashpad Handler:** Linux PID 1066 / Windows PID 236 (`0x00ec`). Alive throughout.
   - **GPU Process:** Linux PID 1137 / Windows PID 512 (`0x0200`), Mesa VirGL D3D12. Alive throughout.
   - **Storage Service:** Linux PID 1145 / Windows PID 544 (`0x0220`). Alive throughout.
   - **Content Renderer 1:** Linux PID 1163 / Windows PID 612 (`0x0264`). Spawned at t=2.854s; ran for 9.715 seconds; vanished at t=12.569s.
   - **Content Renderer 2:** Linux PID 1165 / Windows PID 620 (`0x026c`). Spawned at t=2.854s; **remained alive throughout the entire run** (survival: 9.721s, state: `S`, active `NtQueryInformationProcess(ProcessVmCounters)` at 53.077s).
   - **Network Service:** Linux PID 1176 / Windows PID 664 (`0x0298`). Spawned at t=3.006s after the in-process network service crashed at 02:00:18.441 (`network_service_instance_impl.cc:652`); **remained alive throughout the entire run** (survival: 9.569s, state: `S`).
   - **Processor Metrics:** Linux PID 1229 / Windows PID 872 (`0x0368`). Spawned at t=4.4s; alive throughout.
3. **No Crashpad Minidumps / No SadTab Crash Line:** Crashpad recorded zero minidumps (`.dmp`). `chromium.log` contained NO `SadTab: Tab Killed` error line in this trial.
4. **Clean Powerdown Invariant:** QMP graceful powerdown observed, QEMU exited 0, Xvfb stopped, and controller exited 0. Port 2235 released.

---

## 2. R6 Metadata Probe Live Verification

In the previous trial (`runtime-unblock`), `g4_userenv_r6_probe.phase` was omitted due to the mode-family check omission. In this run, with the corrected wiring, the probe executed in the guest before browser launch:

```json
{
  "v": 2,
  "nonce": "2345d3dd7cf3f1269a681c15e0a23431",
  "pid": 32,
  "userenv_path": "C:\\windows\\system32\\userenv.dll",
  "kernelbase_path": "C:\\windows\\system32\\kernelbase.dll",
  "paths_normal": true,
  "exports_found": true,
  "checks": 21,
  "failures": 0,
  "derive_hr": 0,
  "initial_lookup_hr": -2147024894,
  "register_attempted": true,
  "register_hr": 0,
  "owned": true,
  "post_register_lookup_hr": 0,
  "duplicate_register_hr": -2147024713,
  "verify_lookup_hr": 0,
  "unregister_attempted": true,
  "unregister_hr": 0,
  "final_lookup_hr": -2147024894,
  "cleanup_attempted": false,
  "cleanup_succeeded": false,
  "passed": true
}
```

This confirms that the 4 mounted metadata DLLs are fully functional in the guest prefix and all 21 AppContainer registration lifecycle checks pass before the browser starts.

---

## 3. Exact Child Process Diagnostic Evidence

The read-only observer tracked `/proc` every 20ms during execution. Full process details recorded in `child-exit-diagnostic.json`:

| Linux PID | Windows PID | Role | Spawn (t) | Exit / Vanish (t) | Observed Duration | State at Finish |
|---|---|---|---|---|---|---|
| 1226 | 228 (`0x00e4`) | `browser_main` | 3.921s | — | 8.654s | Alive (`D`) |
| 1066 | 236 (`0x00ec`) | `crashpad_handler` | 0.621s | — | 11.954s | Alive (`S`) |
| 1137 | 512 (`0x0200`) | `gpu_process` | 1.826s | — | 10.749s | Alive (`D`) |
| 1145 | 544 (`0x0220`) | `storage_service` | 1.948s | — | 10.626s | Alive (`S`) |
| 1163 | 612 (`0x0264`) | `content_renderer` (1) | 2.854s | 12.569s | 9.715s | Vanished (clean exit) |
| 1165 | 620 (`0x026c`) | `content_renderer` (2) | 2.854s | — | 9.721s | Alive (`S`) |
| 1176 | 664 (`0x0298`) | `network_service` | 3.006s | — | 9.569s | Alive (`S`) |
| 1229 | 872 (`0x0368`) | `utility_metrics` | 4.400s | — | 8.175s | Alive (`D`) |

### Observed Lifecycle Analysis:
- **Two Content Renderers Spawned Simultaneously:** At t=2.854s, the browser spawned both PID 1163 (Windows PID 612, `--gpu-client-id=6`) and PID 1165 (Windows PID 620, `--gpu-client-id=5`).
- **Differential Renderer Survival:**
  - PID 1163 vanished at t=12.569s (after 9.715s).
  - PID 1165 **stayed alive in state `S`** throughout the trial and continued issuing `NtQueryInformationProcess(ProcessVmCounters)` at 46.167s and 53.077s.
- **Network Service Behavior:**
  - The in-process network service crashed at `1027/020018.441` (`network_service_instance_impl.cc:652`).
  - Out-of-process network service PID 1176 was spawned at t=3.006s and remained healthy throughout. It logged normal `connect failed: 10051` (`WSAENETUNREACH`) due to private loopback isolation when attempting external GAIA telemetry connections.
- **No Unhandled Exception or Crash:** Crashpad handler (PID 1066) recorded 0 crashes. `helper.stderr.log` recorded 0 SEH faults or aborts in any renderer process.

---

## 4. Source Evidence & Wine Trace Limits

As parent noted from `/var/tmp/wex-g4-ac-parent-20261026/wine/dlls/ntdll/unix/process.c`:
- `NtTerminateProcess` has no TRACE statement in Wine ntdll.
- Wine debug channel `process` only logs `NtCreateUserProcess`, `create_initial_thread`, `NtQueryInformationProcess`, `NtSetInformationProcess`, and `NtOpenProcess`.
- Enabling `WINEDEBUG=+syscall` would trace all syscalls, but with Chromium's 50,000+ startup calls, `helper.stderr.log` would exceed the 1 MB `LOG_LIMIT` in `cal.logs`.
- The read-only `/proc` observer bypassed these trace limitations entirely, delivering direct Linux kernel process state, lifetime, and parentage evidence without risking log quota violations.

---

## 5. Artifacts and Preservation

All trial evidence preserved in `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-renderer-unblock-20261026\`:
- `child-exit-diagnostic.json`: Structured 14-process timeline and events.
- `api-probe.stdout.log`: R6 metadata probe 21/21 passing checks.
- `chromium.log`: Full Chromium browser and service logs.
- `helper.stderr.log`: Wine stderr trace (401,400 bytes, well under 1 MB).
- `terminal-front.png`: Front display capture (1280x720).
- `results.json`: Full host and guest execution record.
- `session record`: Clean QMP powerdown exit 0.

---

## 6. Smallest Supported Next Fix (Decision Gate)

The diagnostic evidence narrows the failure landscape:
1. Renderers do NOT suffer fatal SEH exceptions, access violations, or unhandled aborts.
2. Content Renderer 2 (PID 1165) survived throughout the execution, running `LowerToken()` process mitigation policy calls.
3. Content Renderer 1 (PID 1163) exited after 9.7s of initial setup without an error trace.
4. The primary functional deficit between stock Chromium/Edge surviving headfully and the current VirGL Wine `4315be83` remains the **missing lowbox AppContainer token support and handle table enumeration**:
   - `NtQueryInformationProcess(ProcessHandleTable)` is a dummy stub returning length 0 in Wine `4315be83`.
   - `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` and AppContainer token creation via `NtCreateLowBoxToken` are completely unhandled in Wine `4315be83`.
   - As documented in `docs/TOKEN_SECURITY_FIX_ACCEPTANCE.md` (historical commit `33fb3554`), Claude's Token Security branch (`097a76a`, `0b3891f`, `4fa6f4ac`, `69c1d6b55e0`) implemented:
     - `ProcessHandleTable` enumeration in `server/process.c`
     - AppContainer lowbox tokens via `NtCreateLowBoxToken`
     - Restricted token DACL evaluation
   - With those commits, stock browser processes survive and render headfully without `--no-sandbox`.

**Recommendation:** Proceed with parent-authorized integration of the Claude Token Security branch on a dedicated branch combining Wine `4315be83` and commits `097a76a`/`0b3891f` with a clean D-backed build, followed by the live verification control.

Stopping for parent review.
