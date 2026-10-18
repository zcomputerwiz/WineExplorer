# WineExplorer Program Elevation Architecture & Implementation (T3)

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
  - Executes helper via `CreateProcessW` and synchronously waits for completion via `WaitForSingleObject(pi.hProcess, INFINITE)`.
- In `SHELL_ExecuteW`:
  - When verb is `"runas"`, invokes `run_elevation_helper()`.
  - When direct `CreateProcess` fails with `ERROR_ELEVATION_REQUIRED`, invokes `run_elevation_helper()`.
  - If helper exits 0 (Approved): retrieves linked admin token via `get_admin_token()` and calls `CreateProcessAsUserW(token, ...)`. Returns `retval = 33` and genuine `hProcess` if `SEE_MASK_NOCLOSEPROCESS` is set.
  - If helper exits non-zero (Declined/Cancelled): sets `SetLastError(ERROR_CANCELLED)` (1223), sets `hProcess = NULL`, and returns `SE_ERR_ACCESSDENIED`.

### D. User Account Control Consent Dialog (`consent.exe`)
- Source: `src/programs/consent/` and `wine/programs/consent/`
  - `consent.c`: Native Win32 dialog implementation with Vista UAC shield, gradient banners, Authenticode verification, and automated test bypass switches.
  - `consent.rc` & `resource.h`: Dialog templates for Consent (`IDD_CONSENT`) and Credentials (`IDD_CREDENTIALS`).
- UI & Security Features:
  - **4-Quadrant Shield:** Drawn on-the-fly with 24-bit RGB geometry and golden border.
  - **Publisher Verification:** Inspects PE Authenticode digital signature using `WinVerifyTrust(WINTRUST_ACTION_GENERIC_VERIFY_V2)`. Verified signatures show a blue header banner with verified publisher name; unsigned or untrusted binaries show an amber banner with "Unknown Publisher".
  - **Secure Desktop Dimmer:** Spawns a full-screen semi-transparent backdrop overlay (`WS_EX_LAYERED | WS_EX_TOPMOST`, 60% opacity) behind the dialog.
  - **Consent vs Credentials Mode:** Consent mode (Yes/No) for administrators with filtered tokens; Credentials mode (password entry) for standard users.
  - **Automated Test Hooks:** Supports `WINE_CONSENT_AUTO_APPROVE=1` (exits 0) and `WINE_CONSENT_AUTO_DENY=1` (exits 1223) for automated testing.

---

## 3. Process Handle (`hProcess`) Contract

When launching an elevated process via `ShellExecuteEx`:
- Caller specifies `SEE_MASK_NOCLOSEPROCESS`.
- Upon successful elevation, `sei.hProcess` is populated with a genuine Win32 process handle (`PROCESS_QUERY_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE`).
- Callers can wait on `sei.hProcess` via `WaitForSingleObject` or `WaitForMultipleObjects`, query exit codes via `GetExitCodeProcess`, or assign to job objects.
- Upon cancellation or decline, `sei.hProcess` is guaranteed `NULL`, and `GetLastError()` returns `ERROR_CANCELLED` (1223).

---

## 4. Verification & Unit Tests (`dlls/shell32/tests/shlexec.c`)

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
