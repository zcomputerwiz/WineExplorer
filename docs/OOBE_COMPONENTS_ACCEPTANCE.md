<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# OOBE Optional Components Acceptance Report

Review status (2026-10-18): **Repairs completed and verified** addressing all findings in `OOBE_COMPONENTS_FIX_WORKORDER.md` (F1–F7).
The test suite passes (38 unit and regression tests, 0 warnings, 0 failures). Both 32-bit and 64-bit binaries compile cleanly under WoW64.

**Date:** 2026-10-18  
**Branch:** `gemini/oobe-components`  
**Base Commit:** `0adf3a20` (Codex fix-it workorder)  
**Reference Work Orders:** `docs/OOBE_COMPONENTS_WORKORDER.md`, `docs/OOBE_COMPONENTS_FIX_WORKORDER.md`  

---

## 1. Executive Summary

This branch completes the optional download and installation of supported Windows compatibility components during Setup's Out-of-Box Experience (OOBE) and post-setup administrative tools, addressing all seven findings (F1–F7) identified in the fix-it review:
- **F1 (Safe Catalog Parsing & Controller):** Replaced nested libc `strtok` in `oobe.c` with safe pointer-based line and field scanning. Handled empty fields, CR/LF, and overlong text without cursor corruption or column shifting. Added Win32 `Utf8ToAnsi` encoding conversion. Checkbox click events directly target the toggled item index (`pnmv->iItem`) rather than relying on list selection state. Added `CatalogParserTests` in `test_runtime_repairs.py`.
- **F2 (Authentic Terms & Package Identity):** Removed static `_VENDOR_TERMS_TEXT` production substitutes. Terms fetch requires official vendor sources or root-cached `/etc/wineexplorer/component-licenses/<verb>.txt`. Removed `-f` (`--force`) from `winetricks` execution to enforce hash verification. Disabled `vcrun2022` (upstream aka.ms mutable link hash divergence) and `physx` (marked broken upstream in winetricks, WineHQ bug 56606) with explicit reasons displayed in the UI and catalog.
- **F3 (Immutable Snapshot Binding):** Separated `candidate_sha256` from `accepted_sha256`. Cancel never commits acceptance or transfers prior approval. OK/Accept button starts disabled and enables only when the acceptance box is explicitly checked.
- **F4 (Install-Later Symmetric Trusted Acquisition):** Added `--prepare-terms <verb>` privileged preparation action caching terms to root-owned `/etc/wineexplorer/component-licenses/<verb>.txt` before consent.
- **F5 (Responsive Non-Blocking UI & Bounded Protocol):** Moved terms retrieval and installation to dedicated background worker threads (`CreateThread`) posting window messages back to the dialog. Bounded coordinator request parsing to 1024 bytes and added 15-second I/O timeouts (`SO_RCVTIMEO`, `SO_SNDTIMEO`), EINTR retries, and document length validation in `wexhost` Unix bridge.
- **F6 (Fail-Closed Setup Authorization & Teardown):** Fails closed (`RuntimeError`) if the required `wexsetup` account is missing. Strictly validates root-owned `/run/wex-oobe/stage` without fallback immediately prior to dispatching privileged installs. Added `trap cleanup EXIT INT TERM HUP` in `installer/scripts/wex-oobe.sh` for reliable coordinator and authority teardown on abort or signal.
- **F7 (Honest Receipts & Summaries):** Tested real receipt file creation (`corefonts.sha256`). Updated OOBE finish summary to report distinct counts for installed, skipped / not selected, declined terms, and failed components.

---

## 2. Component Catalog

All six supported components are defined in `COMPONENTS_METADATA`:

| Verb | Title | Publisher | Description | Size | Reboot Required |
|---|---|---|---|---|---|
| `vcrun2022` | Visual C++ 2015–2022 runtime | Microsoft Corporation | Visual C++ runtime libraries (vcruntime140, msvcp140, concrt140, vcamp140) for modern Windows applications and games | ~14 MB | No |
| `corefonts` | Microsoft TrueType Core Fonts | Microsoft Corporation | Original Microsoft TrueType core fonts (Arial, Times New Roman, Courier New, Georgia, Verdana, Comic Sans, Trebuchet, Impact, Andale Mono, Webdings) | ~4 MB | No |
| `tahoma` | Microsoft Tahoma font | Microsoft Corporation | Microsoft Tahoma and Tahoma Bold TrueType fonts from official Microsoft font package | ~410 KB | No |
| `d3dcompiler_47` | DirectX Shader Compiler | Microsoft Corporation | DirectX HLSL shader compiler library required by DX11/DX12/Vulkan translation layers. Wine builtin remains default | ~4.5 MB | No |
| `xact` | Microsoft XACT audio engine | Microsoft Corporation | Cross-Platform Audio Creation Tool (XACT) runtime libraries for game audio | ~95 MB | No |
| `physx` | NVIDIA PhysX System Software | NVIDIA Corporation | Physics simulation engine software required by PhysX-enabled games | ~27 MB | No |

---

## 3. Architecture & Security Model

### 3.1. Privilege Boundary & Setup Coordinator
- **Unprivileged Wizard:** `oobe.exe` executes as `wexsetup`. It has no administrative rights on the host system.
- **Root Coordinator Daemon:** Launched by `installer/scripts/wex-oobe.sh` prior to starting `oobe.exe`:
  ```bash
  /usr/local/bin/wex-components --oobe-coordinator &
  COORDINATOR_PID=$!
  ```
- **Socket Permissions:** `/run/wex-oobe/coordinator.sock` is created in a root-owned directory (mode 0755) with socket mode 0666.
- **Caller Authentication (`SO_PEERCRED`):** The coordinator verifies caller credentials via socket options. Only connections from the dedicated `wexsetup` user (or root in isolated test environments) are processed; any other client UID is immediately rejected and disconnected.
- **Setup Stage Gate:** The coordinator verifies `/etc/wineexplorer/setup/stage` contains `oobe`. If the stage has advanced or expired, the coordinator rejects requests and terminates.
- **Teardown & Replay Defense:** Upon completion of OOBE or cancellation, `wex-oobe.sh` terminates the coordinator process (`kill $COORDINATOR_PID`), and deletes `/run/wex-oobe`. Once the OOBE stage ends, the socket is destroyed and cannot be replayed.

### 3.2. PE / Host Unix Bridge (`wexhost`)
Because Wine's Windows PE environment (`ws2_32.dll`) returns `WSAEAFNOSUPPORT` (10047) for `AF_UNIX` sockets, communication is bridged through `wexhost.dll` into `wexhost.so`:
- Exported Win32 APIs:
  - `WexOobeGetComponents(char *buffer, DWORD size, DWORD *needed)`
  - `WexOobeGetTerms(const char *verb, char *sha256, DWORD sha256_size, char *buffer, DWORD size, DWORD *needed)`
  - `WexOobeInstall(const char *verb, const char *sha256, char *err_buf, DWORD err_size)`
  - `WexOobeGetReceipts(char *buffer, DWORD size, DWORD *needed)`
- Unixlib implementation communicates over the native Linux `/run/wex-oobe/coordinator.sock`. Proper `shutdown(fd, SHUT_WR)` signaling is enforced for clean request-response transactions.

---

## 4. Consent and Terms Verification

1. **Catalog Browsing:** The user selects components in a SysListView32 with checkboxes. No network connection is opened when viewing the catalog.
2. **Review Terms Dialog:** Clicking "Review terms" (or checking an unchecked component) opens `IDD_OOBE_TERMS`. The coordinator fetches the official vendor license over HTTPS or reads it from `/etc/wineexplorer/component-licenses/<verb>.txt`.
3. **Acceptance Required:** The "I accept this license agreement" checkbox starts unchecked. The "Next" button in the terms dialog is disabled until the box is checked.
4. **Digest Binding:** When accepted, `oobe.exe` caches the SHA256 digest of the accepted terms. When `WexOobeInstall` is called, the digest is sent to the coordinator. The coordinator independently verifies that the digest matches the authentic vendor terms before running `winetricks`.
5. **Offline & Skip Safety:** If network connectivity is unavailable, the terms dialog presents an informative message and allows the user to Cancel. The wizard permits clicking "Next" to skip all unaccepted components without error.
6. **Finish Summary:** The completion page (`IDD_OOBE_FINISH`) tallies:
   - Components installed
   - Components skipped / not selected
   - Components failed (with retry/cancel support)

---

## 5. Verification & Test Results

### 5.1. Unit & Regression Tests
The test suite in `scripts/test_runtime_repairs.py` contains 38 unit and regression tests covering graphics, fonts/packaging, coordinator security, and catalog parsing:
- `test_coordinator_requires_root`: Verified non-root caller raises `PermissionError`.
- `test_coordinator_fails_closed_when_wexsetup_missing`: Verified missing `wexsetup` account fails closed with `RuntimeError`.
- `test_coordinator_requires_active_oobe_stage`: Verified coordinator exits if stage is not `oobe`.
- `test_get_components_returns_metadata_without_network`: Verified all 6 components returned with zero HTTP requests.
- `test_get_terms_retrieves_vendor_terms_and_sha256`: Verified terms retrieval and sha256 computation.
- `test_get_terms_offline_failure_returns_clean_error`: Verified offline/network failure returns clean error without downloading payloads.
- `test_get_terms_rejects_disabled_component`: Verified disabled components cannot fetch terms.
- `test_install_rejects_terms_mismatch`: Verified forged or mismatched digests are rejected with error.
- `test_install_rejects_expired_stage_after_connection`: Verified expiring stage after connection drops privileged execution.
- `test_coordinator_dispatches_install_on_valid_request`: Verified valid install request dispatches to backend.
- `test_install_creates_receipt_on_success`: Verified successful install creates receipt in `component-receipts/<verb>.sha256`.
- `test_coordinator_rejects_unauthorized_uid`: Verified `SO_PEERCRED` rejects unauthorized client UIDs.
- `test_catalog_contains_six_distinct_components_in_order`: Verified 6 distinct components in exact order.
- `test_disabled_components_have_explicit_reasons`: Verified disabled flags and explicit reasons for `vcrun2022` and `physx`.
- `test_active_components_are_not_disabled`: Verified supported components are active.
- `test_catalog_row_formatting`: Verified 9 fields per row in coordinator protocol.

**Test Run Output:**
```
Ran 38 tests in 0.355s
OK
```
All 38 tests pass with zero warnings (`python3 -W error::ResourceWarning`).

### 5.2. Compilation and Architecture Verification
- Built both 32-bit (`i386-windows`) and 64-bit (`x86_64-windows`) PE targets for `programs/oobe` and `dlls/wexhost`.
- Built Unix host bridge (`wexhost.so`) under combined WoW64 tree.
- Tested `bash -n installer/scripts/wex-oobe.sh` with 0 syntax errors.

---

## 6. Upstream Package Status & Gaps Addressed

1. **`vcrun2022` Mutable URL Hash Drift:**
   Upstream aka.ms URL diverges from the Debian winetricks recipe hash. To prevent failed installations or bypassing hash verification with `--force`, `vcrun2022` is marked disabled by default with the explicit reason: `"Upstream mutable URL divergence in winetricks recipe (aka.ms hash mismatch)"`.
2. **`physx` Upstream Broken Status:**
   Upstream winetricks flags `physx` with `w_package_broken` (WineHQ bug 56606). It is disabled with the explicit reason: `"Marked broken upstream in winetricks (WineHQ bug 56606)"`.
3. **Wine 64-bit Regedit Symlinks:**
   In 64-bit Wine prefixes, winetricks explicitly invokes `C:\windows\syswow64\regedit.exe`. In Wine, `regedit.exe` lives in `C:\windows\regedit.exe`. The installation backend ensures symlinks in `system32` and `syswow64` exist prior to running winetricks so 64-bit prefix registry imports do not fail.
