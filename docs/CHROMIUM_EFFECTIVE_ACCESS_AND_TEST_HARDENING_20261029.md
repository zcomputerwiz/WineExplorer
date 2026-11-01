<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium AppContainer Test Hardening & Effective Access Report — 2026-10-29

- **Document ID:** `docs/CHROMIUM_EFFECTIVE_ACCESS_AND_TEST_HARDENING_20261029.md`
- **Superproject Worktree:** `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer`
- **Root Branch:** `codex/chromium-boundary-diagnostic`
- **Superproject Submodule Commit:** `3a4443e621c2bd6c1aca1936d47166be92838237` (Wine `codex/g4-appcontainer-correctness`)
- **Parent Review References:**
  - `docs/CHROMIUM_R5_CANDIDATE_PARENT_REVIEW_20261029.md` (`a2dc103ac9371d116075ee166197fdb3921bfe66`)
  - `docs/CHROMIUM_R5_TEST_HARDENING_PARENT_REVIEW_20261029.md` (`9ab6dc9a142678cf574adc3021fe91b33d8897ba`)

---

## 1. Executive Summary

This report delivers the complete empirical characterization and comprehensive test-safety resolution requested in Parent Reviews:

1. **Resolution of Finding P1 (Comprehensive Test Data Safety & Owned Cleanup)**:
   All fixed profile names and mock registry mutations across `dlls/userenv/tests/userenv.c:test_appcontainer_profile` have been converted to dynamic per-run names (`WexProfile_%lu_%lu`, `WexMock_%lu_%lu`, `WexUnreg_%lu_%lu`, `WexColl_%lu_%lu`), gated by fail-closed non-existence and registry disposition checks (`REG_OPENED_EXISTING_KEY`), guarded against uninitialized paths on missing `LOCALAPPDATA`, and restricted to strictly owned-only cleanup on all exit paths.
2. **Same-PE Empirical Windows 11 vs. Project-Wine Effective Access Comparison**:
   A dedicated Win32 C test probe (`docs/evidence/access_check_probe.c`) was compiled with MinGW-w64 into a standalone PE executable (`access_check_probe.exe`). The exact same PE was executed on both **Native Windows 11 Pro (Build 26100)** and **Project Wine (x86_64, candidate build)**, measuring `AccessCheck` return values (`Ret`), status codes (`Status`), granted access masks (`Granted`), and error codes (`Err` / `GetLastError()`) across discrete DACL configurations.
3. **Preserved Raw Private Evidence & Binary Provenance**:
   Raw execution logs and probe source are archived directly in the repository:
   - Probe Source: [`docs/evidence/access_check_probe.c`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/access_check_probe.c) (hardened with dynamic unique name `WexProbe_%lu_%lu` and pure mathematical SID derivation without creating or deleting profile paths or keys)
   - Executed Probe PE SHA256: `768fe0cde0ff60f0d5060f90beb555f6ff64713c94096693368523be26ee1023`
   - Native Windows 11 Raw Console Output: [`docs/evidence/access_check_native_windows11.txt`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/access_check_native_windows11.txt)
   - Project Wine Raw Console Output: [`docs/evidence/access_check_project_wine.txt`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/access_check_project_wine.txt)
   - Retained Post-`3a4443e6` Unit Test Raw Logs:
     - [`docs/evidence/userenv_test_post_3a4443e6_x86_64.txt`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/userenv_test_post_3a4443e6_x86_64.txt)
     - [`docs/evidence/userenv_test_post_3a4443e6_i386.txt`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/userenv_test_post_3a4443e6_i386.txt)
     - [`docs/evidence/kernelbase_security_test_post_3a4443e6_x86_64.txt`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/kernelbase_security_test_post_3a4443e6_x86_64.txt)
     - [`docs/evidence/kernelbase_security_test_post_3a4443e6_i386.txt`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/kernelbase_security_test_post_3a4443e6_i386.txt)
4. **Candidate Unit Test Results**:
   Rebuilt and verified with **0 failures** across both architectures (738 total test assertions executed: 738 executed with 2 skips and 2 todo marks).

---

## 2. Finding P1 Resolution: Test-Only Data-Safety Hardening

In commits `b222f7f29776` and `3a4443e621c2` (`codex/g4-appcontainer-correctness`), `dlls/userenv/tests/userenv.c` was hardened across every mutation point:

1. **Dynamic Main Profile Naming & Pre-flight Inspection**:
   - Replaced fixed string `WineExplorerTestContainer` with per-run `WexProfile_%lu_%lu`.
   - Removed unconditional pre-flight `pDeleteAppContainerProfile`.
   - Verified `LOCALAPPDATA` availability: skips gracefully if absent (protecting uninitialized buffer paths).
   - Pre-flight `GetFileAttributesW` ensures no pre-existing path collision.
2. **Dynamic Mock Registry Mapping**:
   - Replaced fixed mock SID `S-1-15-2-12345-67890-99999` with dynamically derived SID from `WexMock_%lu_%lu`.
   - Inspected `RegCreateKeyExW` disposition parameter: if `disposition == REG_OPENED_EXISTING_KEY`, the key is immediately closed and skipped without modifying any pre-existing user data.
3. **Dynamic Collision and Unregistered Profile Tests**:
   - `WexUnreg_%lu_%lu` and `WexColl_%lu_%lu` generated dynamically.
   - Pre-flight registry mapping check ensures unreg mapping does not collide.
   - Files created via `CREATE_NEW` instead of `CREATE_ALWAYS`.
   - Boolean ownership tracking flags ensure cleanup is only executed for positively owned artifacts.

---

## 3. Same-PE Empirical Windows 11 vs. Project Wine Effective Access Comparison

### 3.1 Test Methodology & Probe Architecture

To eliminate differences arising from runtime wrappers, language bindings, or token parameter handling, a single Win32 PE binary was compiled and executed:
- **Source**: `docs/evidence/access_check_probe.c`
- **Compiler**: `x86_64-w64-mingw32-gcc -O2`
- **Binary**: `access_check_probe.exe` (PE32+ executable, x86-64)
- **Binary SHA256**: `768fe0cde0ff60f0d5060f90beb555f6ff64713c94096693368523be26ee1023`
- **Data Safety**: Corrected probe uses pure mathematical SID derivation (`DeriveAppContainerSidFromAppContainerName`) with a dynamic unique name (`WexProbe_%lu_%lu`), avoiding any profile creation or deletion and eliminating any mutation of user profile paths or registry keys.
- **Token Setup**:
  - `Normal`: Duplicate of process token (`SecurityImpersonation`, `TOKEN_ALL_ACCESS`).
  - `AppContainer`: Derived via `CreateAppContainerToken(NULL, &sec_caps, &hAcPrimary)` with valid derived package SID `S-1-15-2-971078271-...` and 0 capabilities (`TokenIsAppContainer=1`), duplicated with `TokenImpersonation`.
- **DACL Configurations**:
  - **Case 1**: User-Only DACL (`D:(A;;FR;;;UserSid)`)
  - **Case 2**: User + `ALL APPLICATION PACKAGES` (`D:(A;;FR;;;UserSid)(A;;FR;;;AC)`)
  - **Case 3**: User + Specific Package SID (`D:(A;;FR;;;UserSid)(A;;FR;;;PackageSid)`)
  - All security descriptors set Owner and Group to current caller SID (`O:%sG:%s`).
- **AccessCheck Invocations**:
  Evaluated for both tokens across both `GENERIC_READ` (`FILE_GENERIC_READ`, `0x00120089`) and `MAXIMUM_ALLOWED` (`0x02000000`).

### 3.2 Side-by-Side Empirical Results Matrix

| DACL Case | Token | Desired Access | Native Windows 11 Pro (Build 26100) | Project Wine (x86_64, candidate build) | Semantic Distinction |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Case 1: User-Only DACL** | Normal | `FILE_GENERIC_READ` (`0x00120089`) | `Ret=1 Status=1 Granted=0x00120089 Err=0` | `Ret=1 Status=1 Granted=0x00120089 Err=0` | Identical: Granted |
| **Case 1: User-Only DACL** | **AppContainer** | `FILE_GENERIC_READ` (`0x00120089`) | **`Ret=1 Status=0 Granted=0x00000000 Err=5`** | **`Ret=1 Status=1 Granted=0x00120089 Err=0`** | **DIVERGENCE**: Windows DENIES (`ERROR_ACCESS_DENIED`), Wine GRANTS |
| **Case 1: User-Only DACL** | Normal | `MAXIMUM_ALLOWED` (`0x02000000`) | `Ret=1 Status=1 Granted=0x00160089 Err=0` | `Ret=1 Status=1 Granted=0x001F0089 Err=0` | Both Grant caller permissions |
| **Case 1: User-Only DACL** | **AppContainer** | `MAXIMUM_ALLOWED` (`0x02000000`) | **`Ret=1 Status=0 Granted=0x00000000 Err=5`** | **`Ret=1 Status=1 Granted=0x001F0089 Err=0`** | **DIVERGENCE**: Windows DENIES (`ERROR_ACCESS_DENIED`), Wine GRANTS |
| **Case 2: ALL APP PKGS** | Normal | `FILE_GENERIC_READ` (`0x00120089`) | `Ret=1 Status=1 Granted=0x00120089 Err=0` | `Ret=1 Status=1 Granted=0x00120089 Err=0` | Identical: Granted |
| **Case 2: ALL APP PKGS** | AppContainer | `FILE_GENERIC_READ` (`0x00120089`) | `Ret=1 Status=1 Granted=0x00120089 Err=0` | `Ret=1 Status=1 Granted=0x00120089 Err=0` | Identical: Granted |
| **Case 2: ALL APP PKGS** | Normal | `MAXIMUM_ALLOWED` (`0x02000000`) | `Ret=1 Status=1 Granted=0x00160089 Err=0` | `Ret=1 Status=1 Granted=0x001F0089 Err=0` | Both Grant caller permissions |
| **Case 2: ALL APP PKGS** | AppContainer | `MAXIMUM_ALLOWED` (`0x02000000`) | `Ret=1 Status=1 Granted=0x00120089 Err=0` | `Ret=1 Status=1 Granted=0x001F0089 Err=0` | Both Grant caller permissions |
| **Case 3: Package SID** | Normal | `FILE_GENERIC_READ` (`0x00120089`) | `Ret=1 Status=1 Granted=0x00120089 Err=0` | `Ret=1 Status=1 Granted=0x00120089 Err=0` | Identical: Granted |
| **Case 3: Package SID** | AppContainer | `FILE_GENERIC_READ` (`0x00120089`) | `Ret=1 Status=1 Granted=0x00120089 Err=0` | `Ret=1 Status=1 Granted=0x00120089 Err=0` | Identical: Granted |
| **Case 3: Package SID** | Normal | `MAXIMUM_ALLOWED` (`0x02000000`) | `Ret=1 Status=1 Granted=0x00160089 Err=0` | `Ret=1 Status=1 Granted=0x001F0089 Err=0` | Both Grant caller permissions |
| **Case 3: Package SID** | AppContainer | `MAXIMUM_ALLOWED` (`0x02000000`) | `Ret=1 Status=1 Granted=0x00120089 Err=0` | `Ret=1 Status=1 Granted=0x001F0089 Err=0` | Both Grant caller permissions |

### 3.3 Status vs. Return API Distinction Analysis

The raw output demonstrates the crucial distinction between API execution and security authorization:
1. **API Return (`Ret=1`)**: In all cases on both platforms, `AccessCheck` returns non-zero (`TRUE`), indicating that security descriptors, token impersonation levels, and generic mapping parameters were valid.
2. **Access Status (`Status=0` vs `Status=1`)**:
   - On Native Windows 11, under Case 1 (User-Only DACL), the AppContainer token is evaluated by the NT kernel's Lowbox filter. Because the DACL does not contain `ALL APPLICATION PACKAGES` (`S-1-15-2-1`), `ALL RESTRICTED APPLICATION PACKAGES` (`S-1-15-2-2`), or the specific package SID, `AccessCheck` sets `AccessStatus = FALSE` (0), `GrantedAccess = 0`, and `GetLastError() = ERROR_ACCESS_DENIED` (5).
   - On Wine, `server/token.c:check_token_access` checks `token_sid_present` for each ACE in the DACL. Because `token_sid_present` matches `token->user`, Wine grants access (`Status = 1`, `GrantedAccess = 0x00120089`, `GetLastError() = 0`).
3. **Conclusion**:
   Wine provides lowbox token metadata and process labeling, but **does not enforce native Lowbox DACL boundary isolation**. Successful fixture rendering must not be claimed as equivalent to Windows sandbox isolation.

---

## 4. Architectural Analysis & Proposed Integration Roadmap (Hypothesis)

> [!NOTE]
> **Boundary Observation vs. Provisioning Hypothesis**: The raw `AccessCheck` outputs directly support and verify the reported status difference (`Status=0 / Err=5` on Native Windows 11 vs `Status=1 / Err=0` on Wine). The broader ACL provisioning roadmap outlined below represents an architectural hypothesis that remains pending verification in future master work.

### 4.1 Root Cause of the Wine Boundary Gap

In Windows NT, AppContainer tokens operate under a dual-evaluation constraint:
- Object access requires ordinary user/group permission **AND** an explicit AppContainer grant (`AC`, package SID, or capability SID).
- On Windows, standard OS assets (system files, shared registry roots, standard named pipes) are provisioned with `(A;;...;;;AC)` ACEs.

In Wine:
- Prefix files and registry keys are not provisioned with Windows NT ACLs granting `ALL APPLICATION PACKAGES`.
- If Wine server were naively modified to deny access when Lowbox grants are absent, AppContainer processes (including Chromium's `utility!kNetwork`) would be blocked from basic DLL loading (`kernel32.dll`, `ntdll.dll`) and registry reading, breaking process execution entirely.

### 4.2 Proposed ACL Provisioning Roadmap (Hypothesis Pending Verification)

As authorized by the user for later dedicated work on latest `zcomputerwiz/wine` master:
1. **Prefix Security Templates**: Provision default ACLs granting `ALL APPLICATION PACKAGES` (`S-1-15-2-1`) on system root directories and registry keys.
2. **IPC Handle Inheritance**: Ensure wineserver primitives (pipes, ports, desktop handles) default to permitting lowbox token access where necessary for IPC.
3. **Server-Side Lowbox Intersection**: Implement the dual-check access intersection in `server/token.c:check_token_access`.

---

## 5. Submodule Pin & Cleanliness

- **Superproject Branch**: `codex/chromium-boundary-diagnostic`
- **Wine Submodule Commit**: `3a4443e621c2bd6c1aca1936d47166be92838237`
- **Whitespace Check**: `git diff --check` passes with 0 warnings.

---

## 6. Worker Unit Test Verification Results & Retained Logs

Rebuilt and executed against Wine commit `3a4443e621c2bd6c1aca1936d47166be92838237`:

| Test Binary | Architecture | Tests Executed | Failures | Todo | Flaky | Skipped | Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `userenv_test.exe` | `x86_64` | 295 | **0** | 1 | 0 | 0 | 0 Failures |
| `userenv_test.exe` | `i386` | 295 | **0** | 1 | 0 | 0 | 0 Failures |
| `kernelbase_test.exe security` | `x86_64` | 74 | **0** | 0 | 0 | 1 | 0 Failures |
| `kernelbase_test.exe security` | `i386` | 74 | **0** | 0 | 0 | 1 | 0 Failures |
| **Total Assertions** | | **738** | **0** | **2** | **0** | **2** | **738 executed (0 failures, 2 skips, 2 todo)** |

### Retained Raw Test Output Paths

The exact raw logs from the post-`3a4443e6` test runs are retained directly in the repository:
- `userenv_test.exe` (`x86_64`): [`docs/evidence/userenv_test_post_3a4443e6_x86_64.txt`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/userenv_test_post_3a4443e6_x86_64.txt)
- `userenv_test.exe` (`i386`): [`docs/evidence/userenv_test_post_3a4443e6_i386.txt`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/userenv_test_post_3a4443e6_i386.txt)
- `kernelbase_test.exe security` (`x86_64`): [`docs/evidence/kernelbase_security_test_post_3a4443e6_x86_64.txt`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/kernelbase_security_test_post_3a4443e6_x86_64.txt)
- `kernelbase_test.exe security` (`i386`): [`docs/evidence/kernelbase_security_test_post_3a4443e6_i386.txt`](file:///d:/DeepThought/Worktrees/chromium-token-integration/WineExplorer/docs/evidence/kernelbase_security_test_post_3a4443e6_i386.txt)
- Claude worker artifacts and pre-`3a4443e6` logs: `D:\DeepThought\Projects\OpenClaude\ac_build\v6_logs\`

- **Zero browser runs executed**: Existing r5 candidate evidence (`chromium-browser-verify-r5-20261029`) remains authoritative and unmodified.
