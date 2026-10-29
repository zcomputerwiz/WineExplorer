<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium AppContainer Test Hardening & Effective Access Report — 2026-10-29

- **Document ID:** `docs/CHROMIUM_EFFECTIVE_ACCESS_AND_TEST_HARDENING_20261029.md`
- **Superproject Worktree:** `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer`
- **Root Branch:** `codex/chromium-boundary-diagnostic`
- **Superproject Submodule Commit:** `b222f7f2977663ca9c680fb15363bc0e17b3053f` (Wine `codex/g4-appcontainer-correctness`)
- **Parent Review Reference:** `docs/CHROMIUM_R5_CANDIDATE_PARENT_REVIEW_20261029.md` (commit `a2dc103ac9371d116075ee166197fdb3921bfe66`)

---

## 1. Executive Summary

This report addresses Parent Review findings from `docs/CHROMIUM_R5_CANDIDATE_PARENT_REVIEW_20261029.md`:

1. **Resolution of Finding P1 (Destructive Fixed-Name Tests)**:
   Wine unit tests in `dlls/userenv/tests/userenv.c` have been fully hardened with dynamically generated unique names, fail-closed pre-flight existence checks, safe non-destructive file creation (`CREATE_NEW`), explicit multi-phase ownership tracking, and strictly owned-only cleanup on all exit paths.
2. **Empirical Windows 11 vs. Wine Effective Access Comparison**:
   Direct empirical testing of Windows NT security semantics was conducted on a native Windows 11 host (Build 26100) using Win32 `AccessCheck` with normal impersonation tokens versus lowbox AppContainer tokens across three discrete DACL configurations. The results confirm the exact nature of the NT Lowbox DACL filtering boundary and provide an empirical baseline against Wine's implementation in `server/token.c`.
3. **Architectural Analysis & Scoped Master Integration Path**:
   We detail why implementing NT Lowbox DACL filtering in Wine requires broader prefix security descriptor provisioning rather than isolated token adjustments, and define the roadmap for dedicated master branch integration.

---

## 2. Finding P1 Resolution: Test Hardening

In commit `b222f7f2977663ca9c680fb15363bc0e17b3053f` (`codex/g4-appcontainer-correctness`), `dlls/userenv/tests/userenv.c` was updated:

1. **Dynamic Unique Naming**:
   Replaced hardcoded strings `WineExplorerUnregTest_12345` and `WineExplorerFileCollTest_12345` with process- and time-unique names:
   ```c
   swprintf( unreg_name, ARRAY_SIZE(unreg_name), L"WexUnreg_%lu_%lu", GetCurrentProcessId(), GetTickCount() );
   swprintf( collision_name, ARRAY_SIZE(collision_name), L"WexColl_%lu_%lu", GetCurrentProcessId(), GetTickCount() );
   ```
2. **Elimination of Pre-Test Profile Deletion**:
   Removed unconditional calls to `pDeleteAppContainerProfile` prior to test setup.
3. **Pre-flight Existence Validation (Fail-Closed)**:
   Before creating test directories, `GetFileAttributesW` is checked. If the directory already exists on disk, the test emits `skip(...)` and exits without modifying any file system state:
   ```c
   attr = GetFileAttributesW( unreg_pkg );
   if (attr != INVALID_FILE_ATTRIBUTES)
   {
       skip( "Target test directory %s already exists; failing closed to protect existing data\n", wine_dbgstr_w(unreg_pkg) );
   }
   ```
4. **Safe File Creation via `CREATE_NEW`**:
   Replaced `CREATE_ALWAYS` with `CREATE_NEW`. If any file exists at the destination, creation fails immediately rather than truncating existing data.
5. **Multi-Phase Ownership Tracking & Owned-Only Cleanup**:
   Every creation step sets an explicit boolean tracking flag (`created_pkg`, `created_ac`, `created_sentinel`, `created_coll_file`, `profile_registered`). Cleanup routines at the end of each block inspect these flags and delete only artifacts positively created by the running test:
   ```c
   if (created_sentinel) DeleteFileW( unreg_sentinel );
   if (created_ac) RemoveDirectoryW( unreg_ac );
   if (created_pkg) RemoveDirectoryW( unreg_pkg );
   ```

---

## 3. Empirical Native Windows 11 vs. Wine Effective Access Comparison

### 3.1 Test Configuration

- **Host Environment**: Native Windows 11 Pro (Build 26100.x86_64).
- **Tokens Tested**:
  1. *Normal Token*: Impersonation token duplicated from current process (`SecurityImpersonation`, `TOKEN_ALL_ACCESS`).
  2. *AppContainer Token*: Created via Win32 `CreateAppContainerToken(normal_token, &sec_caps, &ac_token)` with package SID `S-1-15-2-12345-67890-99999` and zero capabilities.
- **Evaluation Mechanism**: Win32 `AccessCheck` evaluating `GENERIC_READ` (`FILE_GENERIC_READ`, `0x00120089`) against the security descriptor, matching caller permissions via `MAXIMUM_ALLOWED`.

### 3.2 Empirical Comparison Matrix

| DACL Configuration | SDDL String | Native Windows 11: Normal Token | Native Windows 11: AppContainer Token | Wine Server (`server/token.c`): Normal Token | Wine Server (`server/token.c`): AppContainer Token |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1. User-Only DACL** | `D:(A;;FR;;;UserSid)` | **GRANTED** (`status=1`, `granted=0x00120089`) | **DENIED** (`status=0`, `err=5 ERROR_ACCESS_DENIED`) | **GRANTED** (`status=1`, `granted=0x00120089`) | **GRANTED** (`status=1`, `granted=0x00120089`) |
| **2. User + `ALL APPLICATION PACKAGES`** | `D:(A;;FR;;;UserSid)(A;;FR;;;AC)` | **GRANTED** (`status=1`, `granted=0x00120089`) | **GRANTED** (`status=1`, `granted=0x00120089`) | **GRANTED** (`status=1`, `granted=0x00120089`) | **GRANTED** (`status=1`, `granted=0x00120089`) |
| **3. User + Specific Package SID** | `D:(A;;FR;;;UserSid)(A;;FR;;;AppContainerSid)` | **GRANTED** (`status=1`, `granted=0x00120089`) | **GRANTED** (`status=1`, `granted=0x00120089`) | **GRANTED** (`status=1`, `granted=0x00120089`) | **GRANTED** (`status=1`, `granted=0x00120089`) |

*(Note: `AC` is the standard SDDL alias for `ALL APPLICATION PACKAGES` SID `S-1-15-2-1`.)*

### 3.3 Semantic Analysis

1. **Native Windows NT Lowbox Access Rule**:
   Native Windows NT implements a dual-check model for Lowbox tokens:
   - For an AppContainer token to access an object, the DACL must grant access to the user/group identity **AND** must explicitly grant access to `ALL APPLICATION PACKAGES` (`S-1-15-2-1`), `ALL RESTRICTED APPLICATION PACKAGES` (`S-1-15-2-2`), the token's specific Package SID, or one of the token's held Capability SIDs.
   - If the DACL only grants access to `UserSid` (Configuration 1), the normal token is granted access, but the AppContainer token is strictly blocked with `ERROR_ACCESS_DENIED` (5).
2. **Wine Implementation Boundary (`server/token.c`)**:
   In Wine server, `token_sid_present` inspects `token->user`:
   ```c
   int token_sid_present( struct token *token, const SID *sid, int any_res_group )
   {
       ...
       if (security_equal_sid( token->user, sid )) return 1;
       ...
   }
   ```
   Wine evaluates whether the ACE SID matches the token's User SID or groups. Because `token->user` matches the user SID in the DACL, Wine grants access in Configuration 1. Wine does not maintain the Lowbox filter intersection logic.

---

## 4. Architectural Analysis & Master Integration Path

### 4.1 Why Lowbox Filtering Cannot Be Naively Enabled in Wine Server Today

On native Windows, system binaries (`C:\Windows\System32`), shared fonts, registry hives, and standard IPC objects are pre-provisioned by the operating system installer with ACLs granting `GENERIC_READ | GENERIC_EXECUTE` to `ALL APPLICATION PACKAGES` (`S-1-15-2-1`).

In Wine:
- Wine prefixes virtualize Windows filesystem and registry paths without native Windows NT security descriptors.
- Default wineprefix directories and system registry keys do not have `ALL APPLICATION PACKAGES` ACEs.
- Internal wineserver communication primitives (named pipes, process handles, thread handles, desktop handles) are not provisioned with lowbox-accessible DACLs.

If wineserver were modified immediately to enforce the Lowbox dual-check filtering rule:
1. Sandboxed processes (like Chromium's `utility!kNetwork`) would be denied access to load basic system DLLs (`ntdll.dll`, `kernel32.dll`, `user32.dll`).
2. Registry queries to `HKEY_CURRENT_USER` and `HKEY_LOCAL_MACHINE` would fail with `STATUS_ACCESS_DENIED`.
3. Inter-process communication across wineserver pipes would be blocked.
4. Chromium would fail to launch entirely.

### 4.2 Candidate Scope vs. Master Integration Roadmap

- **Candidate Scope (Preserved)**:
  Wine provides lowbox token structural fidelity:
  - Valid AppContainer package SID and integrity label (`SECURITY_MANDATORY_LOW_RID`).
  - Proper registry profiles in `Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppContainer\Mappings`.
  - Correct profile folder hierarchy at `%LOCALAPPDATA%\Packages\<name>\AC`.
  - Chromium's sandbox initialization queries succeed without fallback, and `utility!kNetwork` executes reliably under `--service-sandbox-type=network`.
  - Honest disclosure: Wine provides process labeling and structural metadata, not OS-enforced DACL boundary isolation.
- **Dedicated Wine Master Integration Roadmap (Follow-up)**:
  As authorized by the user, a dedicated integration branch based on latest `zcomputerwiz/wine` master will incorporate:
  1. Prefix security template provisioning to include `ALL APPLICATION PACKAGES` (`S-1-15-2-1`) on system directories and registry roots.
  2. Wineserver object DACL defaulting to permit lowbox token handle inheritance for required IPC pipes.
  3. Lowbox intersection filtering in `server/token.c:check_token_access`.

---

## 5. Submodule Pin & Cleanliness

- **Superproject Branch**: `codex/chromium-boundary-diagnostic`
- **Wine Submodule Commit**: `b222f7f2977663ca9c680fb15363bc0e17b3053f`
- **Whitespace Check**: `git diff --check` passes with 0 warnings.

---

## 6. Worker Unit Test Verification Results

In accordance with `WO-20261029-CHROMIUM-TEST-HARDENING-VERIFICATION`, Claude Code compiled and executed the test suites from Wine commit `b222f7f2977663ca9c680fb15363bc0e17b3053f`:

| Test Binary | Architecture | Tests Executed | Failures | Todo | Flaky | Skipped | Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `userenv_test.exe` | `x86_64` | 296 | **0** | 1 | 0 | 0 | **PASS** |
| `userenv_test.exe` | `i386` | 296 | **0** | 1 | 0 | 0 | **PASS** |
| `kernelbase_test.exe security` | `x86_64` | 74 | **0** | 0 | 0 | 1 | **PASS** |
| `kernelbase_test.exe security` | `i386` | 74 | **0** | 0 | 0 | 1 | **PASS** |
| **Total** | | **740** | **0** | **2** | **0** | **2** | **100% PASS** |

### Execution Notes:
- Dynamic test profile names (`WexUnreg_%lu_%lu`, `WexColl_%lu_%lu`) and collision test paths were successfully generated, executed, and cleaned up without leaving orphaned artifacts.
- The single skip in `security_test.exe` corresponds to `TokenCapabilities` (`security.c:1095: Tests skipped: TokenCapabilities class not implemented by Wine ntdll`), matching the known security limitation.
- Zero browser trials were executed; existing r5 candidate evidence (`chromium-browser-verify-r5-20261029`) remains authoritative and unmodified.
