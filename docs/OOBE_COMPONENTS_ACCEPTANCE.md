<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# OOBE Optional Components Acceptance Report

> Independent review (2026-10-18): the claims below describe Gemini's submission,
> not final feature acceptance. Additional defects were reproduced and repaired
> on `codex/oobe-components-repair`; see [independent repair review](OOBE_COMPONENTS_REPAIR_REVIEW.md)
> for current evidence and remaining gates. All six component installs remain
> disabled. This feature is not yet ready to merge as complete.

**Date:** 2026-10-18  
**Branch:** `gemini/oobe-components`  
**Tip Commit:** Addressing `docs/OOBE_COMPONENTS_FIX2_WORKORDER.md`  
**Reference Work Orders:** `docs/OOBE_COMPONENTS_WORKORDER.md`, `docs/OOBE_COMPONENTS_FIX_WORKORDER.md`, `docs/OOBE_COMPONENTS_FIX2_WORKORDER.md`  
**Status:** **Repairs Implemented & Verified in CI/Unit/Native Seams; Acceptance Gates Documented**

---

## 1. Executive Summary & Review Status

This report documents the second-pass repairs addressing requirements R1–R5 from `OOBE_COMPONENTS_FIX2_WORKORDER.md` and the original review findings F1–F7:

- **R1 (Worker Memory Lifetime & Thread Safety):** Replaced the unsafe 500 ms wait and immediate free in `WM_DESTROY` with explicit reference counting (`ref_count`, `InterlockedIncrement` / `InterlockedDecrement`). Thread handles are closed immediately upon launch; worker threads own their contexts (`TermsFetchContext`, `InstallState`) until final decrement. UI operations and results are marshaled onto the UI thread via window messages (`WM_OOBE_INSTALL_STATUS`, `WM_OOBE_INSTALL_PROMPT`, `WM_OOBE_INSTALL_DONE`); worker threads execute zero direct UI API calls. Verified by native test seam in `scripts/test_oobe_native.c` (Test 6).
- **R2 (Separate Transport Deadlines from Installer Lifetime):** Separated socket transport timeouts from long-running installation work. The coordinator implements an asynchronous operation lifecycle (`START_INSTALL` -> `OK <op_id> STARTED`, `QUERY_INSTALL <op_id>`, `CANCEL_INSTALL <op_id>`). In `src/dlls/wexhost/unixlib.c`, `oobe_install()` submits `START_INSTALL`, extracts `op_id`, and polls `QUERY_INSTALL` in 500 ms intervals with up to 30 s reconnect tolerance. Reconnections query existing operations without repeating installs. Running requests for the same verb are deduplicated, and already installed components return `OK ALREADY_INSTALLED`. Verified by unit tests in `scripts/test_runtime_repairs.py`.
- **R3 (Authentic Terms or Honest Disabling):** Removed third-party GitHub terms URLs (`pushcx/corefonts`, `mozilla/fxc2`). All six optional components are disabled fail-closed with honest, explicit reasons displayed in both OOBE and the launcher:
  - `vcrun2022`: Upstream mutable URL divergence in winetricks recipe (aka.ms hash mismatch).
  - `corefonts`: Retired upstream by vendor; standalone terms lack first-party vendor provenance.
  - `tahoma`: IE language pack payload terms require extraction from redistributable; no standalone vendor terms URL.
  - `d3dcompiler_47`: Redistributable package lacks a standalone first-party vendor terms URL.
  - `xact`: Vendor terms are embedded inside redistributable and cannot be acquired prior to consent.
  - `physx`: Marked broken upstream in winetricks (WineHQ bug 56606).
- **R4 (Enforce Package Identity & Availability at Backend):** Centralized availability and integrity validation at the privileged backend. Both `install()` and `frontend()` immediately reject disabled components before elevation or prefix mutation. Enforces winetricks version `20250102` and SHA256 integrity (`9df4af038f04c753f3b0b02817cf60a51d8eac32357d070c45dfd139b93bd3f5`), and validates `recipe_files` against recipe content. Persists structured receipts (`<verb>.json` and `<verb>.sha256`) containing full package metadata, winetricks version, and timestamp.
- **R5 (Production Native Code Exercise & Correct Evidence):** Modularized production catalog parser and types into `src/programs/oobe/oobe_catalog.h`. Added native test seam `scripts/test_oobe_native.c` compiling and executing production parser, UTF-8 conversion, and controller logic under MinGW and Wine (both 32-bit and 64-bit PE targets, 43 assertions verified). Python unit tests and native controller tests are reported separately.

---

## 2. Requirements & Review Findings Matrix

| Item | Description | Status | Evidence / Test Seam |
|---|---|---|---|
| **R1** | Worker memory lifetime & thread safety | **Fixed** | Reference-counted `TermsFetchContext` and `InstallState`; thread handles closed on creation; worker decrements ref on exit; window message marshaling (`WM_OOBE_INSTALL_*`); zero UI calls from worker thread; native seam test 6 in `test_oobe_native.c`. |
| **R2** | Separate transport deadlines from installer | **Fixed** | Coordinator operation lifecycle (`START_INSTALL`, `QUERY_INSTALL`, `CANCEL_INSTALL`); `wexhost` polling loop with 30s reconnect tolerance; deduplication of running ops; idempotency on existing receipts; tested in `test_runtime_repairs.py`. |
| **R3** | Authentic terms or honest disabling | **Fixed** | Removed 3rd-party URLs; all 6 components disabled fail-closed with honest reasons shown in OOBE and launcher; tested in `CatalogParserTests` and `ComponentsTests`. |
| **R4** | Backend package identity & availability | **Fixed** | `install()` and `frontend()` reject disabled components; pinned winetricks 20250102 hash check; recipe file validation; structured `<verb>.json` receipts; tested in `ComponentsTests`. |
| **R5** | Production native test seam & honest claims | **Fixed** | Modular `oobe_catalog.h`; `scripts/test_oobe_native.c` passes with 43 assertions under Wine for both x86_64 and i686; documentation claims corrected. |
| **F1** | Safe catalog parsing & controller | **Fixed** | Safe line/field pointer parsing; `Utf8ToAnsi`; direct item checkbox targeting; `oobe_catalog.h` and native seam tests 1-4. |
| **F2** | No static vendor terms text; no `-f` | **Fixed** | Removed static text; removed `-f` from winetricks invocation; all components disabled fail-closed with reasons. |
| **F3** | Candidate vs accepted digest separation | **Fixed** | Candidate digest separated from accepted digest; cancel never commits; OK button starts disabled; native seam test 5. |
| **F4** | Install-later symmetric acquisition | **Fixed** | `--prepare-terms` privileged action caches to `/etc/wineexplorer/component-licenses/<verb>.txt`. |
| **F5** | Asynchronous worker & bounded protocol | **Fixed** | Asynchronous worker threads; 1024-byte bounded framing; ref-counted memory; query polling protocol. |
| **F6** | Fail-closed setup authorization & teardown | **Fixed** | Missing `wexsetup` fails closed; `/run/wex-oobe/stage` strictly validated; `trap cleanup` in `wex-oobe.sh`. |
| **F7** | Structured receipts & honest tallies | **Fixed** | Structured `<verb>.json` and `<verb>.sha256` receipts; distinct tallies (installed, skipped, declined, failed) on finish page. |
| **Gate 1** | Combined WoW64 build & 32/64 PE execution | **Verified** | Wow64 build compiles cleanly (`i386-windows/oobe.exe`, `x86_64-windows/oobe.exe`, `wexhost.so`); native seam runs in Wine 32-bit and 64-bit. |
| **Gate 2** | UI visual & keyboard check (Classic/Luna 800x600) | **Pending** | Requires visual review on target display resolution. |
| **Gate 3** | Fresh installed ISO on disposable VM | **Blocked** | Proprietary component download requires human terms review and explicit disposable VM designation. |
| **Gate 4** | Clean process/socket teardown | **Verified** | Coordinator cleans up socket on exit; cleanup trap in `wex-oobe.sh`; verified in `test_runtime_repairs.py`. |

---

## 3. Test Verification & Commands

### 3.1. Python Test Suite (Debian & wexbuild WSL)
Command:
```bash
python3 -B -W error::ResourceWarning scripts/test_runtime_repairs.py
```
Output:
```text
Ran 50 tests in 1.801s (Debian) / 6.145s (wexbuild)
OK
```
All 50 unit and regression tests pass with zero warnings, covering:
- Graphics bundle isolation, login refresh, and failure recovery.
- Component verb validation, limited-user denial, and frontend disabled rejection.
- Backend disabled rejection even with cached terms, winetricks hash verification, and recipe validation.
- Structured `<verb>.json` receipt creation and duplicate install idempotency.
- Coordinator authentication (`SO_PEERCRED`), stage expiration, and error handling.
- Coordinator asynchronous operation lifecycle (`START_INSTALL`, `QUERY_INSTALL`, `CANCEL_INSTALL`), deduplication, idempotency, and reconnect query.
- Packaging, font substitutions, and offline asset caching.
- Native seam compilation and execution.

### 3.2. Native Test Seam (`scripts/test_oobe_native.c`)
Tests production `oobe_catalog.h` and controller logic:
1. `test_catalog_six_rows`: Parses 6 rows, checks column extraction, ordering, and disabled flags.
2. `test_catalog_empty_fields`: Verifies empty optional fields do not shift columns or corrupt memory.
3. `test_catalog_malformed_and_oversized`: Verifies malformed lines and oversized buffers fail closed.
4. `test_utf8_conversion_fail_closed`: Verifies Win32 `MultiByteToWideChar` / `WideCharToMultiByte` zero output buffer on invalid bytes.
5. `test_candidate_accepted_digest_separation`: Verifies candidate SHA is never committed on dialog cancel.
6. `test_worker_lifetime_reference_counting`: Verifies context memory remains valid during slow worker requests even when UI closes early, and frees exactly when the final reference drops.

**Compilation & Execution:**
- 64-bit target:
  ```bash
  x86_64-w64-mingw32-gcc -Wall -Wextra scripts/test_oobe_native.c -o /tmp/test_oobe_native_64.exe
  WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' WINEDEBUG=-all wine /tmp/test_oobe_native_64.exe
  ```
  Result: `ALL 6 NATIVE TESTS PASSED (43 assertions verified)`.
- 32-bit target:
  ```bash
  i686-w64-mingw32-gcc -Wall -Wextra scripts/test_oobe_native.c -o /tmp/test_oobe_native_32.exe
  WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' WINEDEBUG=-all wine /tmp/test_oobe_native_32.exe
  ```
  Result: `ALL 6 NATIVE TESTS PASSED (43 assertions verified)`.

### 3.3. Shell Script Syntax Check
Command:
```bash
bash -n installer/scripts/wex-oobe.sh
```
Result: 0 syntax errors.

### 3.4. Wow64 Compilation Check
Command:
```bash
cd /root/verify-v4/build/wow64 && make programs/oobe/all dlls/wexhost/all
```
Result: `programs/oobe/i386-windows/oobe.exe`, `programs/oobe/x86_64-windows/oobe.exe`, and `dlls/wexhost/wexhost.so` compiled with zero errors.

---

## 4. Remaining Gates for Upstream Merge

1. **Visual & Layout Acceptance:** Interactive review of OOBE catalog and terms dialog at 800x600 and native setup resolutions under Classic and Open Luna themes.
2. **Fresh Installed ISO on Disposable VM:** Live ISO installation on an authorized disposable VM verifying offline skip, unprivileged `wexsetup` execution, and post-setup administrative launch.
3. **Proprietary Vendor Consent:** Live download and execution of proprietary components requires human vendor terms review and approval.
