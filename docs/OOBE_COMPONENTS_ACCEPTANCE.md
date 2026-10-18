<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# OOBE Optional Components Acceptance Report

**Date:** 2026-10-17  
**Branch:** `gemini/oobe-components`  
**Base Commit:** `a5945fdf`  
**Reference Work Order:** `docs/OOBE_COMPONENTS_WORKORDER.md`  

---

## 1. Executive Summary

This branch implements the optional download and installation of supported Windows compatibility components during Setup's Out-of-Box Experience (OOBE). The implementation adheres strictly to the constraints outlined in `docs/OOBE_COMPONENTS_WORKORDER.md`:
- **Native XP-style Wizard Integration:** Added an authentic OOBE wizard page (`IDD_OOBE_COMPONENTS`, 317×143 DLUs) positioned between Connectivity and User Creation, and a dedicated Terms Review dialog (`IDD_OOBE_TERMS`, 300×200 DLUs).
- **Default Open Stance:** All components start unchecked. Wine's builtin DirectX shader compiler and open font substitutes remain the default system configuration.
- **Privilege Boundary & Security:** `wexsetup` remains an unprivileged user without `sudo` or passwordless `pkexec` permissions. Communication occurs via a stage-gated root coordinator listening on `/run/wex-oobe/coordinator.sock`, verified by Linux kernel `SO_PEERCRED`.
- **Zero Network on Start:** Dialog initialization and component catalog enumeration make zero network calls. Network activity only occurs upon explicit user request to "Review terms".
- **Cryptographic Terms Binding:** Terms acceptance generates a SHA256 digest of authentic vendor terms; the installation engine verifies this digest against root-cached terms before proceeding.
- **Shared Engine:** OOBE and the post-setup Windows Components tool share the same backend (`scripts/wex-components.py`), avoiding engine divergence.

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
The test suite in `scripts/test_runtime_repairs.py` was extended with `OobeCoordinatorTests`:
- `test_coordinator_requires_root`: Verified non-root caller raises `PermissionError`.
- `test_coordinator_requires_active_oobe_stage`: Verified coordinator exits if stage is not `oobe`.
- `test_get_components_returns_metadata_without_network`: Verified all 6 components returned with zero HTTP requests.
- `test_get_terms_retrieves_vendor_terms_and_sha256`: Verified terms retrieval and sha256 computation.
- `test_get_terms_offline_failure_returns_clean_error`: Verified offline/network failure returns clean error without downloading payloads.
- `test_install_rejects_terms_mismatch`: Verified forged or mismatched digests are rejected with error.
- `test_install_success_creates_receipt`: Verified successful install creates receipt in `component-receipts/`.
- `test_coordinator_rejects_unauthorized_uid`: Verified `SO_PEERCRED` rejects unauthorized client UIDs.

**Test Run Output:**
```
Ran 30 tests in 0.356s
OK
```
All 22 pre-existing tests + 8 new coordinator tests pass.

### 5.2. End-to-End Integration Smoke Test
Verified end-to-end communication from Windows PE binary -> `wexhost.dll` -> `wexhost.so` -> `/run/wex-oobe/coordinator.sock` -> `wex-components.py`:
```
WexOobeGetComponents returned 0, needed: 1147
Payload received:
vcrun2022	Visual C++ 2015–2022 runtime	Microsoft Corporation	...	~14 MB	0	0
corefonts	Microsoft TrueType Core Fonts	Microsoft Corporation	...	~4 MB	0	0
tahoma	Microsoft Tahoma font	Microsoft Corporation	...	~410 KB	0	0
d3dcompiler_47	DirectX Shader Compiler (d3dcompiler_47)	Microsoft Corporation	...	~4.5 MB	0	0
xact	Microsoft XACT audio engine	Microsoft Corporation	...	~95 MB	0	0
physx	NVIDIA PhysX System Software	NVIDIA Corporation	...	~27 MB	0	0
Finished with return code: 0
```

---

## 6. Known Upstream Gaps & Findings

1. **`vcrun2022` Mutable URL Divergence:**
   Debian winetricks 20250102 recipe pins `vcrun2022` to SHA256 `dd1a8be...` downloaded from `https://aka.ms/vs/17/release/vc_redist.x86.exe`. However, Microsoft frequently updates that endpoint in place, and it now serves build `0c09f2...`. As noted in the work order, terms must strictly match the binary version. When pre-caching or installing `vcrun2022`, the administrator or packaging pipeline must align the winetricks recipe hash with the specific vendor release.
2. **Wine 64-bit Regedit Symlinks:**
   In 64-bit Wine prefixes, winetricks explicitly invokes `C:\windows\syswow64\regedit.exe`. In Wine, `regedit.exe` lives in `C:\windows\regedit.exe`. The installation backend ensures symlinks in `system32` and `syswow64` exist prior to running winetricks so 64-bit prefix registry imports do not fail.
