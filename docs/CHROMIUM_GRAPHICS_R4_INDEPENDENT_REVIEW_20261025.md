<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium Graphics R4 Independent Review Report

**Review Date:** 2026-10-25  
**Reviewer:** `qwen-orchestrator` (Gemini 3.8 Flash High)  
**Review Target Commit:** `5cb366f9af26cc77e7dc80e996cfab95de0a8111`  
**Work Order Commit:** `fcd34809513530fa96800446823fb64034fa50df`  
**Branch:** `codex/chromium-r4-independent-review`  
**Repository:** `https://github.com/zcomputerwiz/WineExplorer`  

---

## 1. Executive Verdict

**Verdict:** `READY_FOR_PARENT_LIVE_REVIEW`

Independent source, test, and preparation review confirms that the R4 native compositor prefix scope correction:
- Defaults `strip_wineprefix` to `False`, maintaining strict backward compatibility for all prior modes (`r1`, `module-warn-r2`, `userenv-r3`).
- Opts into `strip_wineprefix=True` only for explicit `--userenv-r4` invocation.
- Scopes prefix removal strictly to the copied native child process environment without mutating caller `WINEPREFIX` or shared Wayland endpoint settings (`WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`).
- Preserves `/proc`-based `graphics_resources.sample` filtering and real leak assertions with zero PID blanket exclusions.
- Accurately executes the 91-test focused test suite (37.239s, 0 failures, 0 skips) in Debian WSL.
- Maintains bit-for-bit integrity of the prepared transfer archive (`source-stage.tar`) against the immutable preflight manifest.
- Correctly sequesters artifacts as `PREPARED_NOT_RUN` with all live guards intact before any VM dispatch.

---

## 2. Target Files & Source Changes Reviewed

Diff analyzed: `1538e7c3..5cb366f9`

1. **`scripts/graphics_wexcomp.py`**:
   - `OwnedWexcomp.__init__`: Added keyword-only parameter `strip_wineprefix=False`.
   - `OwnedWexcomp.start()`: Clones caller environment via `env = dict(self.environment)`. If `strip_wineprefix` is active, pops `WINEPREFIX` from the child copy only.
   - Preserves caller `self.environment['WINEPREFIX']` intact.
   - Retains Wayland endpoint updates on caller environment (`WAYLAND_DISPLAY='wex-shell-0'`, `XDG_RUNTIME_DIR`).
   - Records `wineprefix_stripped` in the returned start record.

2. **`scripts/check-g4-chromium-native-exit.py`**:
   - Added `'userenv-r4'` to allowed modes, with host `chromium-userenv-r4-20261025`, stage `...-stage`, output `...`.
   - Explicitly sets `compositor_options={'strip_wineprefix': True} if MODE == 'userenv-r4' else {}`.
   - Passes `**compositor_options` to `OwnedWexcomp`.
   - Sequences `g4_userenv_probe.phase` for `MODE in ('userenv-r3', 'userenv-r4')`.
   - CLI adds `--userenv-r4` to the mutually exclusive argument group.

3. **`scripts/g4_userenv_view.py`**:
   - Supports `configure('userenv-r4')` mapping to `chromium-userenv-r4-20261025` stage and output.
   - Executes `check-g4-chromium-native-exit.py` with `--userenv-r4` and `--output-dir`.

4. **`scripts/test_g4_compositor_scope.py`**:
   - `test_real_native_children_default_opt_in_and_marked_owner`: Confirms real Linux child processes with and without prefix stripping, verified by `/proc` sampler. Confirms genuine marked owner rejects intermediate cleanup.
   - `test_actual_control_opts_in_only_r4`: Verifies that only `userenv-r4` passes `strip_wineprefix=True`.
   - `test_fixed_r4_view_and_runner_contract`: Verifies 8 browser arguments, R4 stage/output paths, and view execution sequencing.

5. **`scripts/test_g4_chromium_userenv_r4_adapter.py`**:
   - Verifies guest consumer dispatch, contract paths, and diagnostic exit 1 handling as orchestration non-product failure.

6. **`scripts/test_g4_native_exit.py` & `scripts/test_g4_userenv_view.py`**:
   - Extended test coverage for R4 output override and probe failure sequencing.

---

## 3. Independent Test Execution & Verification

### Test Environment
* **Platform:** Debian WSL (Linux 6.6.x on x86_64, GCC 14.2.0, Python 3.13.5 -B)
* **Test Source Location (Read-Only):** `/mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts`
* **Temporary Directory:** `/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/tmp-independent-review-20261025` (isolated, cleaned up post-run)

### Test Command
```bash
TMPDIR=/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/tmp-independent-review-20261025 \
python3 -B -m unittest test_g4_compositor_scope test_g4_wexcomp_harness \
 test_graphics_resources test_g4_userenv_view test_g4_chromium_userenv_r4_adapter \
 test_g4_chromium_userenv_adapter test_g4_chromium_module_adapter \
 test_g4_chromium_native_adapter test_g4_chromium_native_exit test_g4_native_exit \
 test_g4_native_exit_calibration test_g4_edge_http_fixture -v
```

### Test Results
* **Total Tests Ran:** 91
* **Passed:** 91
* **Failures:** 0
* **Errors:** 0
* **Skips:** 0
* **Elapsed Time:** 37.239s
* **Status:** OK

---

## 4. Actual vs. Mocked Evidence Analysis

| Subsystem / Test Component | Actual Observed Evidence | Mocked / Synthetic Boundaries |
| :--- | :--- | :--- |
| **Native Child Sampler (`test_g4_compositor_scope.py`)** | Spawned real Linux processes (`subprocess.Popen`), checked `/proc/<pid>/stat` start ticks, confirmed unmarked child excluded from `graphics_resources.sample(prefix)` while marked legacy child and marked owner are sampled. Real reaper verification. | Synthetic Wayland endpoint (`wayland-info`), synthetic protocol response (`wex_window_manager_v1`). |
| **Intermediate Prefix Cleanup** | Confirmed real marked owner triggers `RuntimeError('not idle')` with PID recorded in `remaining_processes`. | Probe execution loop simulated via mock `Popen`. |
| **Adapter & Runner Contracts** | Verified CLI dispatch arguments (`--userenv-r4`), stage/output path resolution, 8 browser arguments. | Guest VM execution and remote SSH commands mocked. |
| **Private Adapter Staging** | Full filesystem and archive verification on disk. | Offline only; no VM boot, SSH connection, or live browser launched. |

---

## 5. Offline Preparation & Transfer Manifest Validation

Validated private artifacts under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

1. **Preflight Manifest (`chromium-userenv-r4-20261025.preflight/preflight.json`):**
   * Computed SHA256: `0da5d96ae891d06d5b3d7f2781f4349185230b0fb9bbb15632ba9ee61793637b` (Matches documented manifest SHA)
   * Status: `PREPARED_NOT_RUN`
   * Transfer Files Count: 21

2. **Transfer Archive (`chromium-userenv-r4-20261025.preflight/source-stage.tar`):**
   * Computed SHA256: `d318b5231ac5bc34996675ca780214a6045d1960867764c6c527fbf5924f24b0` (Matches documented archive SHA)
   * Member Count: 21 files
   * Checksum Verification: All 21 files inside the archive were extracted in-memory and matched byte-for-byte with the individual SHA256 and byte sizes recorded in `transfer_files`.
   * Key Binary Assets:
     - `original/native-exit.exe`: `e15cf23578a9...` (306,537 bytes)
     - `original/userenv-probe.exe`: `fb0dee8dcc3f...` (251,972 bytes)
     - `modules/x86_64/userenv.dll`: `89e93bd07742...` (227,261 bytes)
     - `modules/i386/userenv.dll`: `3452a717e91f...` (207,814 bytes)

3. **Marker File (`PREPARED_NOT_RUN`):**
   * Verified present with exact guard:
     `OFFLINE ONLY; no boot/SSH/browser authority. Fixed guest paths require live guards.`

---

## 6. Findings & Blocker Assessment

* **Blocking Defects:** **None**
* **Foreign Overwrites / Paths:** None observed.
* **Scope Creep:** None observed. No unobserved APIs or extraneous dependencies introduced.
* **Integrity Assessment:** The R4 changes isolate the native compositor's environment strictly as intended, ensuring the intermediate cleanup no longer misidentifies `labwc` as a leaked Wine process while maintaining strict resource tracking for genuine Wine processes.

---

## 7. Recommendation

The fix is mathematically and behaviorally sound in all offline unit and contract tests. The repository is ready to proceed to parent live review:

**`READY_FOR_PARENT_LIVE_REVIEW`**
