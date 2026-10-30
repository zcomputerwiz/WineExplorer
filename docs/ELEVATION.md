# WineExplorer Program Elevation Architecture & Implementation (T3)

## Current independent review status (2026-10-18)

T3 is PARTIAL, not complete. Production consent has no unattended file or
environment approval path. Direct unix_chkpwd handling was removed: authenticating
a Unix password is not administrator authorization or correct Windows identity
mapping. Credential-required/unknown-identity requests fail closed with
ERROR_NOT_SUPPORTED pending the request-bound polkit/backend implementation.
Filtered-administrator consent remains a same-server functional seam, not host
privilege authorization. The dimmer is cosmetic on a shared desktop.

Helper cancellation, unsupported policy, startup failure and timeout remain
distinct errors; no target handle is returned on refusal/error. Mock helper tests
are not genuine authentication tests. See TOKEN_SECURITY_REPAIR_REVIEW.md and
TOKEN_SECURITY_CHECKPOINT_B_WORKORDER.md. The following diagrams and old test
counts describe the prior submission, not completed installed-system acceptance.

## 1. Overview & Architecture

Windows Vista introduced User Account Control (UAC) to enforce the principle of least privilege:
- Administrative users normally run with a **filtered (limited) token** (`TokenElevationTypeLimited`).
- Programs requiring administrative privileges declare `requireAdministrator` or `highestAvailable` in their application manifest (`trustInfo/requestedExecutionLevel`).
- Direct invocations via `CreateProcess` on such executables fail with `ERROR_ELEVATION_REQUIRED` (error code 740).
- Elevation is mediated via `ShellExecuteEx` (verb `runas` or manifest detection), which prompts the user for consent or administrator credentials. Upon approval, the application runs elevated with the full administrative token (`TokenElevationTypeFull`), and `ShellExecuteEx` returns a genuine `hProcess` handle to the caller.

In WineExplorer, this mechanism is implemented through a clean, configurable helper seam:
- **Registry Key:** `HKLM\Software\Wine\Elevation`
- **Value:** `Helper` (`REG_SZ`), pointing to the elevation helper executable (typically `C:\windows\system32\consent.exe`).
- **Compatibility Seam:** When `Helper` is unset or empty, stock Wine behavior is preserved (silent in-process elevation, no prompts). When `Helper` is configured, full Win32 UAC manifest inspection, `CreateProcess` 740 rejection, helper invocation, and process handle return contracts are strictly enforced.

```
                    +-----------------------------+
                    |        Caller Process       |
                    +-----------------------------+
                       /                       \
        CreateProcess()                         ShellExecuteEx("runas" or
              |                                  requireAdministrator PE)
              v                                           |
    +-------------------+                                 v
    | PE Manifest Check |                      +----------------------+
    +-------------------+                      | Query Elevation      |
      |               |                        | Helper from Registry |
      | requireAdmin  | asInvoker              +----------------------+
      v               v                                   |
+-------------+  +-----------+                            v
| Return 740  |  | Normal    |                 +----------------------+
| (ELEVATION_ |  | Execution |                 | Invoke consent.exe   |
|  REQUIRED)  |  +-----------+                 | (/hwnd, /cmd)        |
+-------------+                                +----------------------+
                                                          |
                                           +--------------+--------------+
                                           |                             |
                                      Exit 0                        Exit != 0
                                    (Approved)                     (Declined)
                                           |                             |
                                           v                             v
                              +-------------------------+      +-------------------+
                              | CreateProcessAsUserW    |      | Return FALSE      |
                              | with linked admin token |      | GetLastError() =  |
                              | Return genuine hProcess |      | ERROR_CANCELLED   |
                              +-------------------------+      | (1223)            |
                                                               +-------------------+
```

---

## 2. Core Implementation Details

### A. Manifest Parsing & `CreateProcess` Rejection (`dlls/kernelbase/process.c`)
- Function: `is_elevation_required(HANDLE token, const WCHAR *filename)`
- Inspects `HKLM\Software\Wine\Elevation\Helper`. If unset, returns `FALSE` immediately.
- Inspects the calling token's `TokenElevationType`. An existing `TokenElevationTypeFull` token satisfies all elevation requirements and returns `FALSE`.
- Queries the PE binary manifest via `CreateActCtxW` and `QueryActCtxW(..., RunlevelInformationInActivationContext)`.
- If `RunLevel == ACTCTX_RUN_LEVEL_REQUIRE_ADMIN`: returns `TRUE`.
- If `RunLevel == ACTCTX_RUN_LEVEL_HIGHEST_AVAILABLE` and `elev_type == TokenElevationTypeLimited`: returns `TRUE`.
- If elevation is required, `CreateProcessInternalW` aborts early with `STATUS_ELEVATION_REQUIRED` (`ERROR_ELEVATION_REQUIRED` / 740).

### B. Prevention of Silent In-Process Elevation (`dlls/ntdll/loader.c`)
- In stock Wine, `dlls/ntdll/loader.c` contains `elevate_token()`, which silently swaps the process token to the linked admin token during module loading.
- Added `is_elevation_helper_configured()`. If configured, silent elevation is bypassed, maintaining proper process token elevation boundaries.

### C. Shell Elevation & Helper Seam (`dlls/shell32/shlexec.c`)
- Function: `get_elevation_helper(WCHAR *helper, DWORD size)`
  - Reads `HKLM\Software\Wine\Elevation\Helper`.
- Function: `run_elevation_helper(const WCHAR *helper, HWND hwnd, const WCHAR *cmd, const WCHAR *dir)`
  - Formats command line: `[helper] /hwnd [hwnd] /cmd [cmd]`.
  - Supports quoted helpers with arguments (e.g., test harnesses).
  - Normalizes working directories: passes `NULL` when `dir` is empty (`""`) to prevent Win32 `ERROR_DIRECTORY` (267) errors.
  - Executes helper via `CreateProcessW` and synchronously waits for completion via bounded 30-second wait `WaitForSingleObject(pi.hProcess, 30000)`. On timeout, terminates helper via `TerminateProcess(pi.hProcess, 1223)`.
- In `SHELL_ExecuteW`:
  - When verb is `"runas"`, invokes `run_elevation_helper()`.
  - When direct `CreateProcess` fails with `ERROR_ELEVATION_REQUIRED`, invokes `run_elevation_helper()`.
  - If helper exits 0 (Approved): retrieves linked admin token via `get_admin_token()` and calls `CreateProcessAsUserW(token, ...)`. Returns `retval = 33` and genuine `hProcess` if `SEE_MASK_NOCLOSEPROCESS` is set.
  - If helper exits non-zero (Declined/Cancelled): sets `SetLastError(ERROR_CANCELLED)` (1223), sets `hProcess = NULL`, and returns `SE_ERR_ACCESSDENIED`.

### D. User Account Control Consent Dialog (`consent.exe`)
- Source: `src/programs/consent/` and `wine/programs/consent/`
  - `consent.c`: Native Win32 dialog implementation with Vista UAC shield, gradient banners, Authenticode verification, and authentic PAM shadow credentials check.
  - `consent.rc` & `resource.h`: Dialog templates for Consent (`IDD_CONSENT`) and Credentials (`IDD_CREDENTIALS`).
- UI & Security Features:
  - **4-Quadrant Shield:** Drawn on-the-fly with 24-bit RGB geometry and golden border.
  - **Publisher Verification:** Inspects PE Authenticode digital signature using `WinVerifyTrust(WINTRUST_ACTION_GENERIC_VERIFY_V2)`. Verified signatures show a blue header banner with verified publisher name; unsigned or untrusted binaries show an amber banner with "Unknown Publisher".
  - **Secure Desktop Dimmer:** Spawns a full-screen semi-transparent backdrop overlay (`WS_EX_LAYERED | WS_EX_TOPMOST`, 60% opacity) behind the dialog.
  - **Consent vs Credentials Mode:** Consent mode (Yes/No) for administrators with filtered tokens; Credentials mode (password entry) for standard users.
  - **Credential authorization:** Not implemented; fails closed without collecting a password until a real request-bound administrator authorization/identity mechanism is available.
  - **Removal of Shipping Bypasses:** Former environment switches `WINE_CONSENT_AUTO_APPROVE` and `WINE_CONSENT_AUTO_DENY` are completely absent in the shipping binary.
  - **No shipping test bypass:** The former file fixture is removed entirely. Wine drive mapping is not a trust assertion about host file ownership. Test-only helper binaries stay in disposable test prefixes, not production consent or the ISO.

---

## 3. Process Handle (`hProcess`) Contract

When launching an elevated process via `ShellExecuteEx`:
- Caller specifies `SEE_MASK_NOCLOSEPROCESS`.
- Upon successful elevation, `sei.hProcess` is populated with a genuine Win32 process handle (`PROCESS_QUERY_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE`).
- Callers can wait on `sei.hProcess` via `WaitForSingleObject` or `WaitForMultipleObjects`, query exit codes via `GetExitCodeProcess`, or assign to job objects.
- Upon cancellation or decline, `sei.hProcess` is guaranteed `NULL`, and `GetLastError()` returns `ERROR_CANCELLED` (1223).

---

## 4. Machine Root Permission Migration & Rollback (`scripts/setup-machine-root.sh`)

- **Ownership Model:**
  - Production multi-user systems: owned by system group `wexsys` (`root:wexsys`), isolating administrative assets from interactive sudo accounts.
  - Single-user/desktop setups: fallback administrative group `sudo` (`root:sudo`).
- **Application (`apply`):**
  - Sets mode `2775` on directories, `664` on regular files, `775` on `.exe`/`.dll`.
  - Applies POSIX default ACLs (`g:$MACHINE_GROUP:rwX`) to ensure files created by members of `$MACHINE_GROUP` remain group-writable.
- **Rollback (`rollback`):**
  - Fully strips all POSIX ACLs using `setfacl -R -b` and `setfacl -R -k`.
  - Reverts ownership to `root:root` and restrictive permissions (`755` for directories, `644` for regular files).

---

## 5. Verification & Unit Tests (`dlls/shell32/tests/shlexec.c`)

Comprehensive unit tests were added in `dlls/shell32/tests/shlexec.c` under `test_elevation()`:
1. **Stock Behavior Without Helper:** Direct `CreateProcess` of an executable with `requireAdministrator` manifest succeeds unprompted.
2. **Helper Configured - Direct `CreateProcess`:** Direct `CreateProcess` of `requireAdministrator` fails with `ERROR_ELEVATION_REQUIRED` (740).
3. **Helper Configured - `ShellExecuteEx` Approved:** `ShellExecuteEx` invokes approved helper (exit 0) and spawns child; returns `TRUE` with valid `hProcess`.
4. **Helper Configured - `ShellExecuteEx` "runas" Approved:** `ShellExecuteEx` with verb `"runas"` invokes approved helper and spawns child; returns `TRUE` with valid `hProcess`.
5. **Helper Configured - `ShellExecuteEx` Declined:** `ShellExecuteEx` on `requireAdministrator` invokes declining helper (exit 1223); returns `FALSE` with `GetLastError() == ERROR_CANCELLED` (1223) and `hProcess == NULL`.
6. **Helper Configured - `ShellExecuteEx` "runas" Declined:** `ShellExecuteEx` with verb `"runas"` invokes declining helper; returns `FALSE` with `GetLastError() == ERROR_CANCELLED` (1223) and `hProcess == NULL`.
7. **Rollback:** Deleting `HKLM\Software\Wine\Elevation\Helper` cleanly restores stock unprompted behavior.

### Test Execution Output on Debian WOW64 VM:
```
0020:shlexec: 105 tests executed (0 marked as todo, 0 as flaky, 0 failures), 0 skipped.
```
All elevation test cases passed with 0 failures on both 64-bit and WOW64.

---

## 6. Checkpoint B1: Request-Bound Authorization & Identity Architecture Contract

### A. Four Distinct Identities and Authoritative Acquisition

Any secure elevation mechanism must cleanly distinguish four separate identities rather than conflating them into a single concept of "elevated":

1. **Requesting Windows Identity (Token/SID):**
   - **Definition:** The Win32 process/thread security context requesting elevation.
   - **Authoritative Retrieval:** Obtained directly from wineserver's kernel object model via `GetCurrentThreadEffectiveToken()` / `get_current_process()->token`. The token defines the user SID (`S-1-5-21-...`), integrity level (`SECURITY_MANDATORY_MEDIUM_RID`), and elevation status (`TokenElevationTypeLimited`).
   - **Anti-Spoofing Rule:** Never accept client-supplied SID strings, user names, or integrity declarations in RPC payloads or environment variables.

2. **Requesting Unix Peer Identity:**
   - **Definition:** The host OS user context executing the Wine process.
   - **Authoritative Retrieval:** Obtained authoritatively from the host kernel via D-Bus connection credentials: `GetConnectionUnixUser` (UID), `GetConnectionUnixProcessID` (PID), and kernel start-time from `/proc/[pid]/stat`.
   - **Trust Boundary:** Wine client processes cannot spoof their Unix UID across local D-Bus IPC.

3. **Chosen Administrator Principal:**
   - **Definition:** The administrative user account authorized to approve or execute the elevated action.
   - **Filtered-Token Administrator (Same User):** When the requesting Windows user is already in the administrative group (`S-1-5-32-544` enabled for deny-only in the limited token), the chosen principal is identical to the requesting user. Consent elevation unfilters the token, transitioning from `TokenElevationTypeLimited` to `TokenElevationTypeFull`.
   - **Standard-User (Over-The-Shoulder Elevation):** When the requesting user is a standard user, a separate administrator account must authenticate. The authenticating identity must be authoritatively established by PAM/polkit.

4. **Approved Target & Machine Backend Identity:**
   - **Definition:** The execution context and filesystem identity of the launched child process.
   - **Same-UID Compatibility Boundary:** In the initial functional architecture, filtered and full tokens run under the same Unix UID. This provides correct Win32 token isolation and UAC manifest compliance, but is explicitly documented as a Win32 compatibility boundary rather than host native OS isolation.
   - **Cross-UID / wexsys Writes:** Machine-level assets (`/var/lib/wineexplorer/machine`) are protected by system group `wexsys` (or `sudo`) with setgid directory inheritance and POSIX default ACLs (`g:wexsys:rwX`). Any target executed in this tier must still be represented by a genuine Win32 `hProcess` handle within the calling wineserver instance.

### B. Wexhost Polkit Agent & D-Bus Mechanism Integration

- **D-Bus Action:** Elevation requests use the system action `org.wineexplorer.elevation.runas`.
- **Trusted Authentication Path:** Interactive password authentication is delegated entirely to the `wexhost` polkit agent and PAM subsystem. Direct `unix_chkpwd` invocation and custom password verification routines are prohibited.
- **Authorization Verification:**
  - The elevation daemon validates authorization with polkit via `org.freedesktop.PolicyKit1.Authority.CheckAuthorization`.
  - Polkit rules enforce that only users belonging to authorized administrative groups (`sudo`, `wheel`, or `wexsys`) can grant authorization.
  - An authentication success for an unprivileged user account does NOT grant administrative authorization; polkit checks fail closed.
  - User cancellation (`ERROR_CANCELLED` / 1223), authentication refusal, and timeouts terminate the request immediately without returning a process handle.

### C. Frozen Request Binding & Lifecycle Contract

To prevent time-of-check to time-of-use (TOCTOU) attacks, argument tampering, and request substitution:

1. **Server-Owned Request Record:**
   - Each elevation request is assigned a server-generated UUID / 64-bit monotonic Request ID.
   - The request record immutably binds:
     - `RequestId`: Unique identifier.
     - `Requester`: D-Bus peer ID, Unix UID, PID, and process start-time.
     - `ExecutablePath`: Fully canonicalized DOS and NT target paths.
     - `Argv`: Exact argument vector as parsed by Win32 command-line rules.
     - `WorkingDirectory`: Canonicalized directory path (or NULL).
     - `EnvironmentPolicy`: Sanitized environment block flags.
     - `ElevationType`: Filtered consent (`TokenElevationTypeLimited`) vs standard credentials.
2. **Frozen Presentation:**
   - The dialog presentation displays the exact, immutable binary path and command-line bound in the request record.
   - Any modification or mismatch between displayed metadata and execution parameters results in immediate abortion with `ERROR_INVALID_PARAMETER` / `ERROR_ACCESS_DENIED`.
3. **Single-Use Lifetime:**
   - A Request ID is valid for exactly one approval and execution cycle. Upon completion or error, the Request ID is destroyed.
4. **Cancellation and Timeouts:**
   - Unapproved requests expire after 30 seconds, killing the helper process and returning `ERROR_CANCELLED` (1223).
5. **Concurrent Isolation (A/B Independence):**
   - Concurrent elevation requests operate in completely isolated request records. Approving request A has no effect on concurrent request B.

### D. Standard-User Over-The-Shoulder Identity Trap & Fail-Closed Boundary

**Design Trap:**
Polkit's `Authority.CheckAuthorization` returns an authorization result (`IsAuthorized`, `Challenge`, `Details`), but does NOT return the username or UID of the administrator who authenticated in an over-the-shoulder prompt.

**Security Constraints:**
- An arbitrary standard user MUST NOT be elevated to administrator under their own SID (`S-1-5-21-...-1000`), as their primary token cannot grant administrative rights without violating Windows identity semantics.
- Client-supplied strings (such as a username typed into an unauthenticated dialog field) MUST NOT be trusted to construct an administrator token.
- A successful helper exit code cannot be equated with a valid Windows administrative credential without an authoritative identity channel.

**Current Architectural Seam:**
- **Filtered Administrator Consent:** Designed; not implemented or validated end to end. The requesting user already possesses an administrative linked token, and the helper configuration audit is tested, but there is no polkit mechanism, request record or authentication test yet.
- **Standard-User Elevation:** Until an authoritative D-Bus/PAM identity channel is deployed that returns the verified administrator identity to wineserver for token synthesis, standard-user elevation **fails closed** with `ERROR_NOT_SUPPORTED`. It never falls back to unauthenticated elevation or synthetic admin SIDs.
