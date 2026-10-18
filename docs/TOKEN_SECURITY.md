<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Token Security, Elevation, and Chromium Compatibility Architecture (T0)

Date: 2026-10-17. Implementation branch: `gemini/token-security`.  
Base commit: `1fa63233` (master) | Submodule `wine`: `ed5fe967` | `wexcomp`: `acc46fe6`.  
Companion work order: [`docs/TOKEN_SECURITY_WORKORDER.md`](TOKEN_SECURITY_WORKORDER.md)  
Research sources & licenses: [`docs/TOKEN_SECURITY_REFERENCES.md`](TOKEN_SECURITY_REFERENCES.md)

---

## 1. Executive Summary & Objective

This document establishes the architecture checkpoint (**Milestone T0**) for implementing a unified Windows NT security and token framework in WineExplorer.

The objective is to provide genuine Windows NT token semantics that simultaneously power:
1. **User Account Control (UAC) / Application Elevation:** Running standard applications with filtered (limited) tokens and elevating installers and administrative tools to full tokens upon user authorization.
2. **Chromium Sandbox & Worker Isolation:** Enabling modern multi-process browsers (Chromium, Microsoft Edge, WebView2, Electron) to create and lower restricted child tokens without requiring `--no-sandbox` or hardcoded diagnostic bypasses.

**Scope & Posture:**
* **Functionality & Compatibility First:** The initial milestone establishes correct Win32/NT behavioral contracts, object access checks, token queries, and IPC.
* **Security Boundaries:** Wine runs on Linux as the host user. Hardening against hostile native shellcode (Linux user namespaces, `seccomp-bpf`, `landlock`, memory cgroups) is documented as a follow-on phase (T5), not an initial delivery blocker.
* **Authority:** Wine's `wineserver` is the central authority for token objects and handle access checks. ReactOS provides structural NT reference; Samba provides Windows-to-Unix translation design guidance (design only, no code import).

---

## 2. Per-API Inventory & Gap Analysis

The following inventory audits the baseline state in Wine 11.19 (`ed5fe967`) across token, security descriptor, process mitigation, and sandbox APIs.

| API / Entry Point | Wine 11.19 Status | Wine Semantics | Unix / Host Enforcement | Gap / Required Work |
| :--- | :--- | :--- | :--- | :--- |
| `NtCreateLowBoxToken` | **Success-only Stub** | `misc.c` returns `*token_handle = NULL`, `STATUS_SUCCESS`. Fails Chromium `token->is_valid()`. | None | Must allocate a valid token object with AppContainer SID and capability list. |
| `NtFilterToken` | **Partial** | `server/token.c` handles `privileges` removal and `disable_sids` (deny-only). Completely ignores `flags` and `restrict_sids`. | None | Add `restricted_sids` list to `struct token` in wineserver; implement `DISABLE_MAX_PRIVILEGE`. |
| `NtDuplicateToken` | **Implemented** | Duplicates groups and privileges. Sets impersonation level and primary flag. | None | Must also copy `restricted_sids`, `integrity_level`, and `app_container_sid`. |
| `NtOpenProcessToken[Ex]` | **Implemented** | Returns handle to process primary token via wineserver. | None | Correct. Ensure proper access mask checks (`TOKEN_QUERY`, `TOKEN_DUPLICATE`). |
| `NtOpenThreadToken[Ex]` | **Implemented** | Returns impersonation token if set, or falls back/fails per `OpenAsSelf`. | None | Verify fallback semantics when impersonation token is dropped. |
| `NtQueryInformationToken` | **Partial** | Covers `TokenUser`, `TokenGroups`, `TokenPrivileges`, `TokenOwner`, `TokenPrimaryGroup`, `TokenDefaultDacl`, `TokenSessionId`, `TokenElevationType`, `TokenElevation`. | None | Missing/Stubbed: `TokenIntegrityLevel` (hardcoded high), `TokenIsAppContainer` (returns 0), `TokenAppContainerSid` (returns NULL), `TokenRestrictedSids` (unimplemented). |
| `NtSetInformationToken` | **Partial** | Implements `TokenDefaultDacl`, partial `TokenSessionId`. Stubs `TokenIntegrityLevel`. | None | Implement `TokenIntegrityLevel` round-trip and label updates. |
| `AccessCheck` / `NtAccessCheck` | **Partial** | Evaluates object owner and DACL in `server/token.c:token_access_check`. | None | **Critical Gap:** Completely ignores Mandatory Integrity Labels (`SYSTEM_MANDATORY_LABEL_ACE_TYPE`) and Restricting SIDs! |
| `UpdateProcThreadAttribute` | **Partial** | Validates length for `HANDLE_LIST`, `CHILD_PROCESS_POLICY`, `MITIGATION_POLICY`, `JOB_LIST`. | None | **Critical Gap:** Missing `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` (returns `ERROR_NOT_SUPPORTED`). |
| `SetProcessMitigationPolicy` | **Stub** | `kernelbase/process.c` logs FIXME and returns `TRUE`. | None | Return correct queryable mitigation state for `ProcessSystemCallDisablePolicy`. |
| `GetProcessMitigationPolicy` | **Stub** | Returns `TRUE` without setting mitigation buffer contents. | None | Populate mitigation bitmasks to satisfy Chromium verification checks. |
| `CreateJobObjectW` / Limits | **Partial** | `server/job.c` implements basic process association. | None | Ensure `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` and breakaway flags satisfy sandbox coordinator. |

---

## 3. Identity & Token Data Model

### 3.1 Linux UID to Windows SID Mapping
Wine currently maps `uid == getuid()` to a single hardcoded `local_user_sid` (`S-1-5-21-...-1000`) and all other UIDs to `anonymous_logon_sid` (`S-1-5-7`).

To support multi-user sessions, elevation, and service isolation, we define stable SID generation:

1. **Machine Authority:**
   * Machine SID: `S-1-5-21-<sub1>-<sub2>-<sub3>` generated deterministically from `/etc/machine-id` or stored in `system.reg`.
2. **Standard User:**
   * Linux normal user (UID $\ge$ 1000): Maps to `S-1-5-21-<machine>-<1000 + (UID - 1000) * 2>`.
   * Groups: Primary group mapped to Domain Users (`S-1-5-21-<machine>-513`).
3. **Administrator Account:**
   * Linux `sudo` / `wheel` group members:
     * **Filtered Token (Limited / Medium Integrity):**
       * User SID: Administrator's user SID.
       * Groups: `BUILTIN\Users` (`S-1-5-32-545`) enabled; `BUILTIN\Administrators` (`S-1-5-32-544`) marked `SE_GROUP_USE_FOR_DENY_ONLY`.
       * Integrity: `SECURITY_MANDATORY_MEDIUM_RID` (`S-1-16-8192`).
       * Elevation Type: `TokenElevationTypeLimited`.
       * Privileges: Basic standard user privileges only (`SeChangeNotifyPrivilege`).
     * **Full Token (Elevated / High Integrity):**
       * User SID: Administrator's user SID.
       * Groups: `BUILTIN\Administrators` marked `SE_GROUP_ENABLED | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_OWNER`.
       * Integrity: `SECURITY_MANDATORY_HIGH_RID` (`S-1-16-12288`).
       * Elevation Type: `TokenElevationTypeFull`.
       * Privileges: Full administrative privileges enabled/default (`SeSecurityPrivilege`, `SeTakeOwnershipPrivilege`, `SeBackupPrivilege`, `SeRestorePrivilege`, `SeDebugPrivilege`, `SeLoadDriverPrivilege`, etc.).
4. **Service & Machine Root Account (`wexsys`):**
   * **Crucial Rule:** Windows `NT AUTHORITY\SYSTEM` (`S-1-5-18`) must **never** map to Linux `root` (UID 0).
   * Mapped to dedicated unprivileged system daemon user `wexsys` (or host daemon group), with System Integrity (`S-1-16-16384`).
5. **Restricted & AppContainer Tokens:**
   * Restricting SIDs: List of SIDs (e.g. `S-1-2-0` Local, `S-1-5-12` Restricted Code).
   * AppContainer Package SID: `S-1-15-2-...` with Low Integrity (`S-1-16-4096`).

```
+-------------------------------------------------------------------------------+
|                             Windows Access Token                              |
+-------------------------------------------------------------------------------+
| User SID:       S-1-5-21-...-1000 (Local User)                                |
| Integrity:      S-1-16-8192 (Medium) or S-1-16-4096 (Low/AppContainer)        |
| Elevation Type: Limited (Filtered) vs Full (Elevated)                         |
+-------------------------------------------------------------------------------+
| Groups:         - Everyone (S-1-1-0)                                          |
|                 - BUILTIN\Users (S-1-5-32-545) [Enabled]                      |
|                 - BUILTIN\Administrators (S-1-5-32-544) [Deny-Only or Enabled]|
+-------------------------------------------------------------------------------+
| Privileges:     - SeChangeNotifyPrivilege [Enabled]                           |
|                 - (Admin privs removed or enabled based on Elevation Type)    |
+-------------------------------------------------------------------------------+
| Restricting:    [Optional list of SIDs constraining all access checks]       |
+-------------------------------------------------------------------------------+
```

---

## 4. Server-Side Token & Object Access Architecture

### 4.1 Server `struct token` Extensions (`server/token.c`)
The current server token structure must be extended to retain the complete Windows NT security context:

```c
struct token
{
    struct object  obj;
    struct luid    token_id;
    struct luid    modified_id;
    struct list    privileges;          /* luid_attr entries */
    struct list    groups;              /* sid_and_attributes */
    struct sid    *user;
    struct sid    *owner;
    struct sid    *primary_group;
    unsigned int   primary;
    unsigned int   session_id;
    struct acl    *default_dacl;
    int            impersonation_level;
    int            elevation;

    /* --- New Fields for Token Security & Sandbox --- */
    struct sid     integrity_level;     /* Mandatory Integrity SID (S-1-16-*) */
    unsigned int   mandatory_policy;    /* SYSTEM_MANDATORY_LABEL_NO_WRITE_UP etc. */
    struct list    restricted_sids;     /* Restricting SIDs list */
    struct sid    *app_container;       /* AppContainer Package SID (if applicable) */
    struct list    capabilities;        /* Capability SIDs (S-1-15-3-*) */
};
```

### 4.2 Two-Tier Access Check Algorithm (`token_access_check`)
In native Windows NT, when a token performs an access check against a Security Descriptor:

1. **Step 1: Mandatory Integrity Control (MIC) Check:**
   * Inspect the object SACL for `SYSTEM_MANDATORY_LABEL_ACE_TYPE`.
   * If present, compare `token->integrity_level` RID with label RID:
     * If `token_rid < label_rid` and label has `SYSTEM_MANDATORY_LABEL_NO_WRITE_UP`, mask out write permissions (`GENERIC_WRITE`, `WRITE_DAC`, `WRITE_OWNER`, `DELETE`).
     * If `token_rid < label_rid` and label has `SYSTEM_MANDATORY_LABEL_NO_READ_UP`, mask out read permissions.
2. **Step 2: Discretionary Access Control (DACL) Check:**
   * Evaluate user and enabled groups against DACL ACEs (`ACCESS_DENIED_ACE_TYPE`, `ACCESS_ALLOWED_ACE_TYPE`).
   * Groups with `SE_GROUP_USE_FOR_DENY_ONLY` participate **only** in deny checks; they never grant rights.
3. **Step 3: Restricting SIDs Intersection Check:**
   * If `list_empty(&token->restricted_sids)` is false:
     * Perform an independent access check using **only** the restricting SIDs against the DACL.
     * `granted_access = dacl_granted_access & restricting_granted_access`.
     * Access is granted **only if both** passes permit the requested access.

---

## 5. Elevation Launch & Return Contract

### 5.1 The Cross-Wineserver Handle Challenge
When an application requests elevation (e.g., installer executing `runas` via `ShellExecuteEx`):
* If the elevated process runs under another Linux UID (e.g. `wexsys`), it operates in a separate `wineserver` instance.
* In Wine's architecture, a numeric Win32 `HANDLE` is an index into a specific `wineserver`'s process handle table. One `wineserver` cannot return an integer handle that refers to an object in another `wineserver`.
* However, callers of `ShellExecuteEx` (with `SEE_MASK_NOCLOSEPROCESS`) expect a valid `hProcess` handle that supports:
  * `WaitForSingleObject(info.hProcess, INFINITE)`
  * `GetExitCodeProcess(info.hProcess, &code)`
  * `CloseHandle(info.hProcess)`

### 5.2 Architectural Seam: Dual-Path Resolution
We resolve this by defining two clean, explicit paths:

```
Path A: Single-Wineserver User Elevation (Recommended & Default for T3)
+---------------------------------------------------------------------------+
| User Process (Filtered Token) ---> ShellExecuteEx("runas", "setup.exe")   |
|                                            |                              |
|                              wexhost Polkit Auth Dialog                   |
|                                            |                              |
| Parent wineserver creates Child Process with Full Admin Token (Elevated)  |
|                                            |                              |
| Returns genuine Win32 hProcess handle directly to caller                  |
+---------------------------------------------------------------------------+

Path B: Machine-Tier Backend Daemon (wexsys / Session 0)
+---------------------------------------------------------------------------+
| Service Manager / wexhost spawns daemon under dedicated UID 'wexsys'       |
| Process proxy object in user wineserver holds IPC pipe to daemon           |
| Translates wait/exit signals into proxy process state                     |
+---------------------------------------------------------------------------+
```

* **Contract for Path A (Standard Elevation):**
  * Caller: Standard or administrator user.
  * Manifests: `requireAdministrator`, `highestAvailable`, or verb `"runas"`.
  * Consent: Prompt triggered via D-Bus to `wexhost` (polkit listener).
  * On Authorization: `wineserver` allocates the child process using `token_create_admin(..., TokenElevationTypeFull)`.
  * Return: Transparent, genuine `hProcess` returned to `SHELLEXECUTEINFO.hProcess`.
  * Rollback/Refusal: On user cancel or wrong password, returns `ERROR_CANCELLED` (`1223`). No process spawned.

---

## 6. Pinned Chromium Test Baseline & Reproduction Report

### 6.1 Baseline Configuration
* **Runtime Version:** Microsoft Edge WebView2 Fixed Version Runtime `155.0.4283.62` x86 (PE32 Intel i386).
* **Provenance:** Official Microsoft CDN package `Microsoft.WebView2.FixedVersionRuntime.155.0.4283.62.x86.cab` (SHA256: `18f8e0787e9e80209da740a12e2c454e60128cf7e85743b71004bb18579dfd94`).
* **Test Environment:** `WineExplorer-VM` (Debian 13 Trixie, Linux 6.12, Wine 11.19 WoW64).
* **Isolation:** Clean test prefix `~/edge-test/pfx/` and user profile `/tmp/edge-data/`.

### 6.2 Unmodified Baseline Failure Analysis
When Edge 32-bit was executed in unmodified Wine 11.19 without sandbox or composition flags:
1. **Crash 1 (DirectComposition):** Chromium calls `LoadLibraryW(L"dcomp.dll")` and invokes `DCompositionCreateDevice3`. Wine's `dcomp.dll` returns `E_NOTIMPL`. In `ui/gl/direct_composition_support.cc`, this unhandled failure triggers `MiniDumpWriteDump` and aborts the broker process.
2. **Crash 2 (Token Isolation):** When launching worker processes (renderer, GPU, storage), Chromium calls `NtCreateLowBoxToken`. Wine's stub returns `*token_handle = NULL`. In `sandbox/win/src/restricted_token.cc`, `token->is_valid()` fails, causing immediate child process termination.
3. **Crash 3 (Win32k Lockdown):** Chromium calls `SetProcessMitigationPolicy` and attempts to restrict GDI/User syscalls. Under Wine, child processes still need `win32u.dll` to query displays and window stations; mismatched mitigation expectations cause renderer initialization aborts.

### 6.3 Validated Milestone Baseline Run
With mitigations applied in the compatibility lab:
* DirectComposition bypassed via `WINEDLLOVERRIDES="dcomp=d"`.
* Multi-process Mojo IPC active across broker, storage service, GPU, and renderer.
* Graphics rasterized via Google SwiftShader CPU Vulkan SIMD (`vk_swiftshader.dll`).
* **Visual Acceptance:** Rendered both complex CSS gradients (`edge_app_long.png`) and live network content with multilingual typography from `http://example.com` (`edge_clean.png`) inside WineExplorer's desktop.

---

## 7. Implementation Plan & Review Gates

```mermaid
flowchart TD
    T0["T0: Reproduction & Architecture Checkpoint (docs/TOKEN_SECURITY.md)"] --> T1["T1: Token Representation, Filtering & Effective Identity"]
    T1 --> T2["T2: Server Access Checks (MIC & Restricting SIDs)"]
    T2 --> T3["T3: Program Elevation & UAC Consent Seam"]
    T3 --> T4["T4: Chromium Functional Compatibility (Unmodified Sandbox)"]
    T4 --> T5["T5: Deferred Hardening Design (Linux Isolation Documentation)"]
```

### Review Gate 1: Milestones T0 – T2 (COMPLETE)
1. **T1 (Token Representation):**
   * Added `integrity_rid`, `restricted_sids`, and `app_container` to `struct token`.
   * Extended `server/protocol.def` for `filter_token` with `disable_sids_size` and VARARG `restrict_sids`.
   * Implemented `DISABLE_MAX_PRIVILEGE` and `LUA_TOKEN` in wineserver `filter_token`.
   * Implemented `create_lowbox_token` in wineserver returning a valid handle, Low integrity (`0x1000`), AppContainer SID, and capabilities.
   * Implemented `set_token_integrity_level` in wineserver.
   * Implemented `NtQueryInformationToken` / `NtSetInformationToken` in `ntdll` and WOW64 thunks for `TokenIntegrityLevel`, `TokenAppContainerSid`, `TokenIsAppContainer`, `TokenIsRestricted`, `TokenHasRestrictions`, and `TokenRestrictedSids`.
2. **T2 (Access Checks):**
   * Implemented Mandatory Integrity Check (`SYSTEM_MANDATORY_LABEL_NO_WRITE_UP`, `NO_READ_UP`, `NO_EXECUTE_UP`) in `token_access_check` and `check_object_access`.
   * Implemented Restricting SIDs intersection check in `token_access_check`.
   * Added comprehensive unit test coverage in `dlls/advapi32/tests/security.c`:
     * `test_token_integrity_level_roundtrip`: Verifies Medium $\to$ Low $\to$ Untrusted (`0`) round trips.
     * `test_restricted_token_sids`: Verifies `TokenIsRestricted`, `TokenHasRestrictions`, and `TokenRestrictedSids` query buffers and group count.
     * `test_lowbox_token`: Verifies `NtCreateLowBoxToken`, `TokenIsAppContainer` == 1, `TokenAppContainerSid`, and Low integrity RID.
     * `test_mandatory_integrity_access_check`: Verifies that a Low-integrity impersonation token is granted `KEY_READ` but DENIED `KEY_WRITE` on a Medium-labeled object via `NO_WRITE_UP`.
     * `test_restricted_token_access_check`: Verifies that restricting SIDs intersection denies access when the DACL does not match restricting SIDs, and grants access when restricting SIDs match.
   * **Verification Results:**
     * x86_64 PE test suite: **3,643 tests executed, 0 failures**.
     * i386 (WOW64) PE test suite: **3,643 tests executed, 0 failures**.

### Review Gate 2: Milestone T3 (In Progress)
* Manifest-driven elevation, `runas` integration with `wexhost` polkit consent dialog, full/limited token pairing, and `hProcess` lifecycle verification.

### Review Gate 3: Milestone T4
* Unmodified Chromium/Edge launch without `--no-sandbox` or `--single-process`, verifying that child renderers initialize their LowBox tokens and communicate over Mojo IPC.
