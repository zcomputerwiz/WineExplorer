<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium Runtime Unblock & First Live Control Execution Report

**Status:** `CONTROL_EXECUTED` (Runtime Check Unblocked; Live Chromium Diagnostics Captured; Clean QMP Shutdown Verified)
**Execution Timestamp:** 2026-10-26T23:53:13Z to 23:54:58Z
**Branch:** `codex/chromium-runtime-unblock`
**Parent Work Order Reference:** `docs/CHROMIUM_R6_RUNTIME_INVENTORY_WORK_ORDER_20261026.md` (commit `76eb763b03e4d9817fcad8e6aa8a2011874bc394`)
**Parent Message Reference:** `msg_1793056561369_14892f4f`
**Evidence Archive:** `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-runtime-unblock-20261026\chromium-native-evidence.tar.gz`

---

## 1. Executive Summary

In accordance with parent instructions ("get Chromium working; a perfect test harness is not the objective"), Antigravity and Claude Code have:
1. **Identified the Exact Runtime Guard Discrepancy**: 0 missing required files; exactly 1 benign surplus file (`chrome-win/debug.log`) generated during earlier disk activity.
2. **Applied the Smallest Justified Runtime Guard Fix**: Modified `runtime_state()` in `scripts/check-g4-chromium-native-exit.py` to differentiate fatal missing files from surplus generated diagnostics, wired the fresh `--runtime-unblock` diagnostic identity across runner and module view (`scripts/g4_userenv_view.py`), and proved correct behavior with 7 focused unit tests (`scripts/test_g4_runtime_unblock.py`).
3. **Prepared Distinct Stage/Output Identity & Fresh Native Helper PE**: Recompiled `userenv-browser-unblock-20261026.exe` (SHA256 `6935e1d908498540ba3926d89499889d34efd36608638760cbcca39668231d4f`) matching output path `Z:\home\debian\wex-g4\chromium-runtime-unblock-20261026` and paired with accepted probe `userenv-r6-probe.exe` (SHA256 `8558081c1211a7d0298bf33b2fa0a9ef62d4b3b3079439fbb4efa398ea962c4d`).
4. **Executed ONE Authorized Live Diagnostic Control**:
   - Runtime file check **PASSED** (tolerated `chrome-win/debug.log` as diagnostic warning; verified all 258 required package files and PE signatures).
   - VirGL Wayland compositor (`wexcomp`) started with Mesa 25.0.7 virgl on D3D12 (Intel UHD Graphics 750).
   - Read-only module view bound accepted clean `userenv.dll` and `kernelbase.dll` for both architectures.
   - Native helper launched Chromium with all 8 standard arguments.
   - **First Full Multi-Process Chromium Launch Reached**: Main browser (PID 220), Crashpad handler (PID 256), GPU process (PID 568), Network Service (PID 724), Renderer (PID 916), and Utility process (PID 932) all spawned and ran.
   - **Observed Chromium Behavior & Next Real Blocker**: Browser logged 71.9 KB of verbose diagnostics. Detected 1280x720 display; GPU process initialized ANGLE and virgl; Network service confirmed private loopback; at ~16.8s post-launch, renderer tab encountered `Sad Tab: Tab Killed: http://127.0.0.1:33071/` followed by page discarding.
   - Terminal wait reached 45s timeout without crashing the host session.
5. **Clean Graceful QMP Powerdown**: QMP `system_powerdown` acknowledged, guest `SHUTDOWN` event observed, QEMU exited 0, Xvfb stopped, and Windows launcher controller exited 0. Zero fallback force-kill required.

---

## 2. Runtime Guard Mismatch & Justified Fix

### 2.1 Actual Runtime Discrepancy
- **Expected Manifest Files**: 258 files defined in `notice-review-r1/runtime-manifest.json`.
- **Missing Files**: **0**. All 258 required package files (including `chrome.exe`, `chrome.dll`, `chrome_elf.dll`, `D3DCompiler_47.dll`, `libEGL.dll`, `libGLESv2.dll`) are present, readable, and verified with correct PE headers.
- **Surplus Files Observed**: Exactly 1 file:
  `chrome-win/debug.log` (generated during prior process activity).
- **Prior Guard Behavior**: The previous check `if set(actual) != set(expected): raise ValueError('Existing partial/extra runtime; never overwrite')` treated any generated runtime file as a fatal installation corruption error.

### 2.2 Justified Guard Fix
In `scripts/check-g4-chromium-native-exit.py`:
- Computed `missing = set(expected) - set(actual)` and `extra = sorted(set(actual) - set(expected))`.
- If `missing` is non-empty: strictly raises `ValueError(f"Missing required runtime files ({len(missing)}): ...")`.
- If `extra` is non-empty: tolerated as benign surplus, recorded in `state['extras']` for diagnostic visibility, and allows execution to proceed.
- Verified all expected files via `observe_file(actual[name], expected[name])` and checked core PE signatures.
- Added mode `'runtime-unblock'` mapping to `chromium-runtime-unblock-20261026`.

In `scripts/g4_userenv_view.py`:
- Added `'runtime-unblock'` mapping to `chromium-runtime-unblock-20261026`.
- Staged all 4 accepted DLLs (`kernelbase.dll` and `userenv.dll` for both `x86_64` and `i386`).
- Wired `--runtime-unblock` CLI option.

### 2.3 Focused Unit Test Matrix (`scripts/test_g4_runtime_unblock.py`)
Ran under WSL Debian Python 3.13: **7 passed, 0 failures, 0 errors**:
1. `test_canonical_reuse_complete_no_extras`: Verifies clean runtime passes with `extras: []`.
2. `test_benign_surplus_completes_and_reports_extras`: Verifies surplus files (`debug.log`, `crashpad.dat`) pass with `extras` populated.
3. `test_missing_required_file_fails`: Asserts missing `chrome.dll` raises `ValueError('Missing required runtime files')`.
4. `test_symlink_inside_runtime_rejected`: Asserts symlink inside runtime directory raises `ValueError('Runtime link not allowed')`.
5. `test_runtime_unblock_mode_paths_agree_across_scripts`: Asserts path agreement across runner and view modules.
6. `test_runtime_unblock_cli_flag_selects_mode_and_rejects_combination`: Asserts mutual exclusivity and flag dispatch.
7. `test_runtime_unblock_selected_modules_both_arches`: Asserts staging of all 4 modules across `x86_64` and `i386`.

Existing unit matrices also passed:
- `scripts/test_g4_chromium_native_exit.py` & `scripts/test_g4_userenv_view.py`: 26 passed.
- `scripts/test_g4_chromium_userenv_r6_adapter.py`: 5 passed.

---

## 3. Live Trial Preflight & Identity

- **Run Name:** `chromium-runtime-unblock-20261026`
- **Disk Backing:** `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\system-native-identity-r4-20261024.qcow2` (clean, port 2235 verified idle before boot)
- **Preflight SHA256:** `cc7686b2bba804294fce30425438a2d127d2c9b15946b0357c74d1af2a62ebe6`
- **Native Helper PE:** `userenv-browser-unblock-20261026.exe` (SHA256 `6935e1d908498540ba3926d89499889d34efd36608638760cbcca39668231d4f`, 306,537 bytes)
- **Native Probe PE:** `userenv-r6-probe.exe` (SHA256 `8558081c1211a7d0298bf33b2fa0a9ef62d4b3b3079439fbb4efa398ea962c4d`, 301,064 bytes)
- **Accepted Clean DLLs Staged:**
  - `userenv.dll` (x86_64): `71de0bea2e3b85a9afcddf7dcdcc40a31b204605530b4d83e0e2cf08e2bf04ba` (196,841 bytes)
  - `userenv.dll` (i386): `02f1f5b914b8aa0ba846cf5c04fb616c8aaf6a2563cfeeb5a162b4c151db8313` (170,966 bytes)
  - `kernelbase.dll` (x86_64): `147fb181ff8ce1eb13aa2bb166e053f40c26191662d1b447247fdfd1e720aa24` (7,542,180 bytes)
  - `kernelbase.dll` (i386): `261a2fd0dfa4b04f6b3c16a122f3efbc3a703bb0d078cea8ab552d1476c5f0ed` (6,446,162 bytes)

---

## 4. Live Control Observations & Evidence

### 4.1 Live Guards & Pre-Browser Checks
- **Disk Free Space:** 1.72 TiB on D: host; 11.97 GB on `/home/debian/wex-g4` guest.
- **Prefix Idle Probe:** 156 tasks scanned (114 kernel threads, 42 user tasks); 0 busy tasks accessing prefix.
- **Deployed Source Integrity:** 27 transferred files matched SHA256 hashes 100%.
- **Live Runtime State:** `state: complete`, `extras: ["chrome-win/debug.log"]`, `required_free_bytes: 1073741824`. Runtime check passed cleanly without fatal exception.
- **Readonly Module View:** Bound mounts inside private mount namespace succeeded in 0.076s; outside physical DLLs verified completely unchanged post-run.

### 4.2 Chromium Multi-Process Execution
The native helper launched Chromium with the 8 normal arguments:
`"Z:\home\debian\wex-g4\chromium-headful-r1-20261023-stage\vendor\extracted\chrome-win\chrome.exe" "--app=http://127.0.0.1:33071/7de39821d121ca83cc357385afdd3d44/index.html#nonce=7de39821d121ca83cc357385afdd3d44" "--user-data-dir=Z:\home\debian\wex-g4\chromium-runtime-unblock-20261026\profile" "--no-first-run" "--no-default-browser-check" "--enable-logging" "--log-file=Z:\home\debian\wex-g4\chromium-runtime-unblock-20261026\chromium.log" "--v=1"`

The full Chromium process tree initialized:
| Role | Windows PID | Process Type Flag | Details |
|---|---|---|---|
| Main Browser | 220 (`0x00dc`) | (main) | Policy service, screen setup, display 1280x720 detected |
| Crashpad Handler | 256 (`0x0100`) | `--type=crashpad-handler` | Self-monitoring handler active |
| GPU Process | 568 (`0x0238`) | `--type=gpu-process` | ANGLE / VirGL initialized, D3D12/Intel UHD 750 |
| Network Service | 724 (`0x02d4`) | `--type=utility --utility-sub-type=network.mojom.NetworkService` | Isolated loopback binding, DNS fallback |
| Renderer | 916 (`0x0394`) | `--type=renderer` | Extension & Web content context created |
| Utility (Metrics) | 932 (`0x03a4`) | `--type=utility --utility-sub-type=chrome.mojom.ProcessorMetrics` | Processor metrics service |

### 4.3 Actual Next Chromium Behavior & Real Blocker
1. **Graphics & Display**:
   - `screen_win.cc:1113 Display[11762804] bounds=[0,0 1280x720], workarea=[0,0 1280x720], scale=1 external detected`.
   - `native-egl.stdout.log` confirmed EGL 1.5 with `OpenGL core profile renderer: virgl (D3D12 (Intel(R) UHD Graphics 750))`.
   - `terminal-front.png` was captured at 1280x720 RGB with non-trivial pixel content (`((0, 255), (0, 255), (0, 255))`).
2. **Tab Kill / Sad Tab**:
   - At timestamp `23:54:20.830` (~16.8 seconds into execution), the browser log recorded:
     `WARNING:chrome\browser\ui\sad_tab.cc:257 Tab Killed: http://127.0.0.1:33071/`
   - Immediately followed by:
     `WARNING:chrome\browser\performance_manager\policies\page_discarding_helper.cc:158 Discarding multiple pages with target (kb): 0`
   - Wine trace (`helper.stderr.log`) shows the renderer process (PID 916 / `0x0394`) hitting Wine stubs:
     - `fixme:process:NtQueryInformationProcess ProcessHandleCount (0xffffffffffffffff,...) stub`
     - `fixme:process:NtQueryInformationProcess ProcessHandleTable (0xffffffffffffffff,...) stub`
     - `fixme:process:SetProcessMitigationPolicy (3, ...): stub`
     - `fixme:process:SetProcessMitigationPolicy (8, ...): stub`
3. **HTTP Navigation Outcome**:
   - The HTTP fixture (`http://127.0.0.1:33071/...`) was bound and listening on private loopback.
   - Because the renderer was killed / discarded by Chromium's performance manager / sad-tab handler before completing the fetch, the HTTP server recorded `requests: []`.
4. **Terminal Event**:
   - Native helper waited 45,000 ms; Chromium child stayed alive until the 45s timeout expired (`wait_result: 258` / `WAIT_TIMEOUT`).

---

## 5. Graceful QMP Powerdown & Cleanup Verification

- **QMP System Powerdown:** `request_id: 90b01c789a8ee96180b0d76b20ec3992-powerdown` sent and acknowledged.
- **Guest Shutdown Observed:** `event: SHUTDOWN`, `data: {"guest": true, "reason": "guest-shutdown"}` received over QMP in 8.74s.
- **QEMU Exit Status:** `0` (clean normal exit).
- **Xvfb Stopped:** `true`.
- **Windows Controller Process:** PID 36788 exited `0`.
- **Remaining Servers / Prefix Processes:** Empty (`[]` and `{}`).
- **Prefix Idle:** Verified intact.

---

## 6. Deliverable Commits & Next Steps

- **Branch:** `codex/chromium-runtime-unblock`
- **Commits:**
  - `0206d03c`: Narrow runtime check guard unblock, mode wiring, and focused unit tests.
  - (Current): `docs/CHROMIUM_RUNTIME_UNBLOCK_20261026.md` documenting the live trial evidence and the renderer Sad-Tab finding.
- **Evidence Archive:** `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-runtime-unblock-20261026\chromium-native-evidence.tar.gz` (and uncompressed evidence folder `chromium-native-evidence/`).
- **Parent Review Gate:** STOPPING and standing by for Codex / parent review of this actual Chromium diagnostic behavior. Zero VM reboot or speculative patching attempted.
