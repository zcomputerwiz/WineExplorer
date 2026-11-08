<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S16 Coverage and Test Matrix Analysis

## 1. Overview and Scope

Work package E1-S16 (docs/E1_S_STANDARD_USERS.md section 7 row 16) mandates:
> "Build and test HKCU/profile persistence, two administrator identities/logons, simultaneous leases/descendants, handle-held unload, restart/save failure, path/ACL/mode substitution and environment/cwd. Use a trusted lab launch driver to verify Bob UID/raw-syscall limits and same-server handle behavior before broker enablement. Requires S2/S11-S15."

This document analyzes which items of row 16 and of the section 8 matrix (profile and state areas) are **already covered** by existing passing test cases (as of S13/S14/S15, 68 PASS lines in `scripts/test_hku_load.sh` plus native suites `scripts/test_profile_storage.sh` and `scripts/test_launch_prep.sh`), and which items are **missing** and implemented by E1-S16.

---

## 2. Existing Coverage vs Missing Gaps Analysis

### 2.1 Section 7 Row 16 Requirements

| Row 16 Requirement | Existing Passing Coverage (Cited Cases) | Missing / Gap for S16 |
|---|---|---|
| **HKCU/profile persistence** | **Covered**: `unload` (writes `"Persisted"=dword:014a11ce` to HKCU, unloads, verifies disk write, verifies value on reload in `e1s14_lifecycle`). | Cross-identity persistence across two distinct administrators simultaneously. |
| **Two administrator identities / logons** | **Partially covered**: S10 probe covers synthetic token issuance for Alice (`wexadm`) and Carol (`wexcar`). | **NOT covered in profile/hive harness**: In `scripts/test_hku_load.sh` / `scripts/lib/e1s13-fixture.sh`, only Alice (`wexadm`) profile is provisioned and loaded. Two distinct administrator profiles (Alice and Carol) loaded at once with separate hives, separate HKCU views, no cross-visibility, and unloading one while leaving the other loaded is missing. |
| **Simultaneous leases & descendants** | **Partially covered**: `unload` tests single suspended child holding token blocks unload (`STATUS_RESOURCE_IN_USE` `0xc0000708`). | **NOT covered**: Simultaneous child and grandchild processes holding Alice's token concurrently keeping the profile busy; unload must be denied until both descendants exit. |
| **Handle-held unload** | **Partially covered**: `e1s14_lifecycle` creates a temporary key on the broker thread while impersonating Alice and verifies `STATUS_CANNOT_DELETE` (`0xc0000121`). | **NOT covered**: A separate managed client process (or peer) holding an open key handle or descendant key handle under Alice's HKCU blocking broker unload with distinct busy status (`0xc0000121`), then closing it allowing unload. |
| **Restart / save failure** | **Covered**: Checkpoints 1, 2, 3 in `crash-1`, `crash-2`, `crash-3` and three `recovery` cases (exit 86, recovery denial `STATUS_REGISTRY_CORRUPT` `0xc000014c`). | **NOT covered**: Non-crash save failure such as read-only profile directory or save-time I/O failure (fail-closed, lease retained, memory data not lost). |
| **Path / ACL / mode substitution** | **Partially covered**: S12 `scripts/test_profile_storage.sh` covers provisioning-time substitution. In S13 harness, pre-load checks exist for `owner` (UID 0), `absent`, `unprovisioned`, `wrong-incarnation`, `wrong-reference`. | **NOT covered post-load**: Swapping the hive file or profile directory for a symlink, or changing Unix mode (e.g. 0666/0777) or owner from Unix *after load*, verifying that the next save / flush / reload denies fail-closed. |
| **Environment / cwd** | **Covered**: Volatile Environment values checked in `accept` (`USERNAME`, `USERPROFILE`, `HOMEDRIVE`, `HOMEPATH`, `APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP`), `GetUserProfileDirectoryW`, `ExpandEnvironmentStringsForUserW`, `CSIDL_PROFILE`. Pure unit tests in `scripts/test_launch_prep.sh`. | Integration with trusted launch driver verifying environment and cwd behavior under Bob host UID. |
| **Bob UID / raw-syscall limits & same-server handle behavior** | **NOT covered in S13 harness**: S10 had `NOT_COVERED=... host UID measurement`. No test verifies that a process running with Alice's Windows token retains Bob's host UID and cannot use raw Linux syscalls to access Alice host files or host root. | **Missing**: Trusted launch driver probe measuring `getresuid()`/`getresgid()`, testing raw Linux syscall access to Alice-only files vs Bob-accessible files, documenting Wine-only identity boundaries. |

---

### 2.2 Section 8 Test Matrix (Profile & State Areas)

| Section 8 Matrix Area | Existing Passing Coverage | Status & Gap for S16 |
|---|---|---|
| **Host UID, server and handles** | `scripts/e1s10_token_probe.c` tests token groups, privileges, session, and basic handle rights. | **Gap**: Must measure real/effective/saved host UID/GID of an Alice-token process in the prefix; verify raw syscall access limits (cannot access Alice host-only files or switch UID); verify Bob's host UID owns files created via native operations. |
| **Profile location, ownership and ACLs** | `accept` checks `C:\users\.e1s\...` and Volatile Environment; `test_profile_storage.sh` checks Bob 0700/0600 modes; `e1s13_descriptor` checks Windows DACL (Alice owner, SYSTEM, Administrators). | **Covered** for single user. **Gap**: Multi-admin cross-visibility check (Alice cannot see Carol's private hive/keys, Carol cannot see Alice's). |
| **Profile scope and identity changes** | `absent`, `unprovisioned`, `wrong-incarnation`, `wrong-reference`, `owner`, `already-loaded`, `replay`. | **Gap**: Post-load substitution (symlinks, altered modes, altered owner after load) tested against save/flush. |
| **Hive load/save/unload** | `unload` (single child process busy, proxy stale check); `crash-1..3` and `recovery` (fail-closed recovery denial). | **Gap**: Two admins simultaneously loaded (`two-admins`); child + grandchild simultaneous leases (`descendants`); client-held open key handle (`handle-held`); save-failure / read-only directory (`save-fail`). |
| **Environment and launch preparation** | `accept` checks Volatile Environment and userenv; `scripts/test_launch_prep.sh` tests pure launch_prep rules. | **Covered**. |

---

## 3. Prioritized Missing Cases to Implement in E1-S16

To fulfill work package E1-S16 and section 8 requirements, the following six test cases are added to `scripts/lib/e1s13-fixture.sh`, `scripts/e1s13_hku_checks.h`, and `scripts/e1s10_token_probe.c`:

### (a) `two-admins` (Two Administrator Identities & Cross-Visibility)
- **Objective**: Provision and load two distinct S4 administrator accounts (Alice `wexadm` and Carol `wexcar`) simultaneously in the same prefix server.
- **Expected Result**:
  1. Both profiles load successfully with distinct SIDs, distinct storage generations, and distinct proxy roots.
  2. Alice sees only Alice HKCU; Carol sees only Carol HKCU.
  3. Writes to Alice HKCU are invisible in Carol HKCU (no cross-visibility or cross-contamination).
  4. Unload of Alice leaves Carol fully loaded, intact, and functional.
  5. Subsequent unload of Carol succeeds cleanly.
- **Positive Control**: Successful independent registry read/write for both identities while both are loaded.

### (b) `descendants` (Simultaneous Leases & Child/Grandchild Processes)
- **Objective**: A process running with Alice's token creates a child process, which in turn creates a grandchild process.
- **Expected Result**:
  1. Both child and grandchild hold Alice's primary token and are registered with the server's profile process tracking.
  2. While child and grandchild are alive, `WEX_HKU_UNLOAD_S14` denies with `STATUS_RESOURCE_IN_USE` (`0xc0000708`).
  3. Terminating child while grandchild remains alive still denies unload (`0xc0000708`).
  4. Once grandchild terminates and both processes are reaped, unload succeeds with status 0.
- **Positive Control**: S14 single-child unload denial and eventual clean unload.

### (c) `handle-held` (Open Key Handle from Managed Client Process)
- **Objective**: A managed process opens a key handle and descendant key handle under Alice's HKCU and keeps them open.
- **Expected Result**:
  1. Broker attempts `WEX_HKU_UNLOAD_S14` while the client holds the key handle.
  2. Unload is rejected with `STATUS_CANNOT_DELETE` (`0xc0000121`), distinct from process-busy status (`0xc0000708`).
  3. Client closes the key handle.
  4. Unload is retried and succeeds with status 0.
- **Positive Control**: Successful unload after handle closure.

### (d) `save-fail` (Save Failure / Read-Only Directory Fail-Closed)
- **Objective**: Make the profile directory read-only (`chmod 0500`) after load, then trigger a durable save (`RegFlushKey`).
- **Expected Result**:
  1. `wex_profile_save` fails to create `.e1s14-state` (EACCES / `WEX_PROFILE_IO`).
  2. Server fails closed: returns `STATUS_REGISTRY_IO_FAILED` (`0xc00002ac`).
  3. The loaded hive remains in memory, the lease is retained (phase `E1S_POISONED`), and the existing `user.reg` file is NOT lost, truncated, or corrupted.
  4. Permissions restored (`chmod 0700`); cleanup proceeds safely.
- **Positive Control**: Baseline `unload` and `RegFlushKey` passing under normal permissions.

### (e) `post-subst` (Post-Load Path/ACL/Mode Substitution)
- **Objective**: After Alice's profile is loaded into the server, alter the filesystem state from Unix (e.g. replace `user.reg` with a symlink, or change mode to 0666, or replace the directory with a symlink).
- **Expected Result**:
  1. Subsequent flush / save detects that the inode, mode, or link identity changed via `hive_unchanged()` / `verify_existing()`.
  2. Save is refused with `STATUS_FILE_INVALID` (`0xc0000098`) or `STATUS_ACCESS_DENIED` (`0xc0000022`).
  3. Interrupted / corrupt state is rejected on any subsequent reload.
- **Positive Control**: S12 pre-load mode/owner/symlink rejection.

### (f) `host-uid` (Trusted Lab Launch Driver / Bob UID and Raw-Syscall Limits)
- **Objective**: A process launched with Alice's Windows token runs on Linux under Bob's host credentials.
- **Expected Result**:
  1. Windows token query verifies Alice SID (`TokenUser`), High integrity, and Administrators group enabled.
  2. Linux host identity query (`getresuid`, `getresgid`, `/proc/self/status`) confirms real, effective, and saved UIDs are Bob's host UID (`62001`), NOT Alice's host UID and NOT root (`0`).
  3. Linux raw file access confirms: Bob can read/write Bob-owned files; attempts to open an Alice-only host file (mode 0600 owned by `wexadm`) fail with Linux `EACCES` (`13` / Win32 `ERROR_ACCESS_DENIED`).
  4. Documented boundary: Wine token elevation grants Windows administrative rights in this prefix, but confers ZERO host privilege escalation or host identity switching.
- **Positive Control**: Successful access to Bob-owned prefix files under the same process.
