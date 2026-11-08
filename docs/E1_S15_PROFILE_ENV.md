<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S15 profile environment and launch preparation

Survey and implementation plan for Work Package E1-S15 of [E1-S](E1_S_STANDARD_USERS.md),
following [E1_DECISIONS.md](E1_DECISIONS.md), [S10 token](E1_S10_TOKEN.md),
[S12 storage](E1_S12_PROFILE_STORAGE.md), [S13 HKU load](E1_S13_HKU_LOAD.md),
and [S14 leases](E1_S14_LEASES.md).

Wine-only identity boundary: Bob remains the host Unix UID/GID and owns prefix storage;
Alice is the Wine administrator token user. The elevated target process executes under
Bob's host UID/GID in Bob's prefix and session with Alice's administrator token.
Launches requiring a real host UID other than Bob's deny as unsupported.

---

## 1. Codebase Survey: Existing Profile & Environment Derivation

### 1.1 `wine/dlls/userenv` (`userenv_main.c`)

| Function | Lines | Existing implementation and findings |
|---|---|---|
| `CreateEnvironmentBlock` | `237-399` | Opens `HKLM\System\CurrentControlSet\Control\Session Manager\Environment` (`252`), queries `SystemRoot` and `SystemDrive` (`261-279`). Enumerates system `Environment` and `Volatile Environment` subkeys (`284-296`). Reads `HKLM\Software\Microsoft\Windows NT\CurrentVersion\ProfileList\ProfilesDirectory` (`299-302`), setting `ALLUSERSPROFILE` and `ProgramData` (`310-314`). Sets `COMPUTERNAME` (`324-327`) and WoW64 variables (`328`). If `!hToken`: derives `USERPROFILE` as `%ProfilesDirectory%\Default` and sets SID to `".Default"` (`330-344`). If `hToken`: reads `TokenUser` (`352-355`), calls `LookupAccountSidW` (`367-369`), and appends the account name to `ProfilesDirectory` (`buf + len`, `363-365`), setting `USERNAME` and `USERPROFILE` to `C:\users\<account_name>` (`370-371`). Notice: `LookupAccountSidW` populates `domain`, but `USERDOMAIN` is **never set**; `HOMEDRIVE` and `HOMEPATH` are **never set**. Finally opens `HKEY_USERS\<SID>\Environment` and `HKEY_USERS\<SID>\Volatile Environment` and merges variables (`379-395`). |
| `GetUserProfileDirectoryA` | `448-469` | Bounded buffer check; allocates WCHAR buffer, delegates to `GetUserProfileDirectoryW`, converts via `WideCharToMultiByte` (`464-465`). |
| `GetUserProfileDirectoryW` | `471-526` | Queries `TokenUser` from `hToken` (`489-492`). Calls `LookupAccountSidW` to get `userW` (`495-499`). Calls `GetProfilesDirectoryW` to get `dirW` (`"C:\users"`, `501-505`). Constructs `dirW + "\" + userW` (`514-516`), returning `"C:\users\<account_name>"`. Completely unaware of prefix-local managed profile mapping. |
| `GetProfilesDirectoryA/W` | `530-587`, `589-648` | Reads `HKLM\Software\Microsoft\Windows NT\CurrentVersion\ProfileList\ProfilesDirectory` and expands environment strings. Defaults to `%SystemDrive%\users`. |
| `ExpandEnvironmentStringsForUserA/W` | `412-422`, `424-434` | Completely **ignores `hToken`**! Forwards unconditionally to `ExpandEnvironmentStringsA/W` on the calling process's environment block. |
| `LoadUserProfileA/W` | `695-700`, `702-707` | Gated in S13/S14 via `load_managed_profile` (`680-693`): queries `WEX_HKU_QUERY`; for ordinary tokens returns `HKEY_CURRENT_USER`; for managed tokens opens registered proxy root via `WEX_HKU_OPEN`. |
| `UnloadUserProfile` | `721-738` | Gated in S14: checks managed status via `WEX_HKU_QUERY`; for managed tokens calls `WEX_HKU_UNLOAD_S14` with the proxy handle; ordinary tokens return stub `FALSE`. |

### 1.2 `wine/dlls/shell32` and `kernelbase` shell-folder lookups (`shellpath.c`)

| Function / Table | Lines | Existing implementation and findings |
|---|---|---|
| `CSIDL_Data` | `1448-1453` | `CSIDL_PROFILE` (0x28) is mapped to `FOLDERID_Profile`, `CSIDL_Type_User`, but has no `.value` string entry. |
| `_SHGetUserProfilePath` | `2395-2463` | For `hToken != NULL`, reads SID string from token (`_GetUserSidStringFromToken`, `2436`) and selects `hRootKey = HKEY_USERS`. For `hToken == NULL`, selects `hRootKey = HKEY_CURRENT_USER` (`2432`). Calls `_SHGetUserShellFolderPath(hRootKey, userPrefix, szValueName, pszPath)` (`2452`). If registry query fails and `hRootKey != HKEY_LOCAL_MACHINE`, falls back to `_SHGetUserShellFolderPath(HKEY_LOCAL_MACHINE, ...)` (`2454`). If that fails, falls back to `_SHGetDefaultValue(folder, pszPath)` (`2456`). |
| `_SHGetDefaultValue` | `2223-2280` | For `CSIDL_Type_User` where parent is NULL (such as `CSIDL_PROFILE`), returns literal string `L"%USERPROFILE%"` (`2260`). |
| `_SHExpandEnvironmentStrings` | `2564-2678` | Resolves `%ALLUSERSPROFILE%`, `%PUBLIC%`, `%ProgramData%`, `%SystemDrive%`. When `%USERPROFILE%` is encountered (`2646-2655`): opens `ProfileList` in HKLM (`2582`), gets `ProfilesDirectory` (`szProfilesPrefix`, e.g. `"C:\users"`), calls `GetUserNameW(userName, ...)` and appends it (`2652-2654`). Result is always `"C:\users\<GetUserNameW()>"`. Ignores token identity and S12 mapping. |
| `SHGetFolderPathW` / `SHGetFolderPathAndSubDirW` | `2875-2886`, `2940-3060` | Dispatches to `_SHGetUserProfilePath` for `CSIDL_Type_User` (`2995`). Expands `%...%` via `_SHExpandEnvironmentStrings` (`3009`). If `CSIDL_FLAG_CREATE` is passed, creates directories (`3049`). |
| `SHGetKnownFolderPath` | `3567-3617` | Converts `KNOWNFOLDERID` to CSIDL via `csidl_from_id` (`3571`) and delegates to `SHGetFolderPathAndSubDirW` (`3602`). |
| `dlls/kernelbase` | N/A | Does not implement shell folders; shell folder lookups reside in `shell32.dll`. |

### 1.3 Process Environment Block (ntdll, server, wineboot, services, start)

| Component | Lines | Derivation and propagation |
|---|---|---|
| `wine/dlls/ntdll/unix/env.c` | `1205-1300` | `add_registry_environment`: reads `\Registry\Machine\System\CurrentControlSet\Control\Session Manager\Environment` (`1240-1245`), HKCU `Environment` (`1246-1250`), HKCU `Volatile Environment` (`1251-1255`), `ProfileList` for `ProgramData`/`ALLUSERSPROFILE`/`PUBLIC` (`1258-1276`), and `CurrentVersion` for `ProgramFiles`/`CommonFiles` (`1279-1300`). |
| `wine/dlls/ntdll/env.c` | `127-144`, `598-644` | `RtlCreateEnvironment` allocates empty or duplicates `Peb->ProcessParameters->Environment` (`136-138`). `RtlCreateProcessParametersEx` inherits caller environment if `Environment` parameter is NULL (`628`). |
| `wine/server/process.c` | `1273-1289` | `init_process_done`: receives `startup_info_data` and client-passed environment block. Wineserver does not construct environment variables or fork/exec child processes. |
| `wine/programs/wineboot/wineboot.c` | `866-913` | `create_volatile_environment_registry_key`: runs at wineboot for ordinary user; writes into `HKEY_CURRENT_USER\Volatile Environment`: `APPDATA`, `CLIENTNAME`, `USERPROFILE`, `HOMEPATH`, `HOMEDRIVE`, `USERNAME`, `HOMESHARE`, `LOCALAPPDATA`, `USERDOMAIN`, `LOGONSERVER`, `SESSIONNAME`. Does **not** run for Alice's managed profile. |
| `wine/programs/services/services.c` | `1144-1157` | `start_service_process`: calls `CreateEnvironmentBlock(&environment, token, FALSE)` for services. |
| `wine/programs/start/start.c` | `219` | Ordinary executable launcher; inherits parent environment block without synthesizing identity variables. |

### 1.4 How variables are derived today vs. Managed Profile (S12) Requirement

| Variable | Stock Wine derivation today | Managed Profile (S12) requirement |
|---|---|---|
| `USERPROFILE` | `C:\users\<account_name>` from HKLM ProfileList + username (`userenv_main.c:371`, `514`; `shellpath.c:2654`). | Must be Alice's exact S12 prefix-local profile path: `C:\users\.e1s\<SID>\<incarnation>-<name>`. |
| `APPDATA` | `HKCU\Software\...\Explorer\User Shell Folders` or `HKCU\Volatile Environment` populated by wineboot. | Alice's roaming AppData: `C:\users\.e1s\<SID>\<incarnation>-<name>\AppData\Roaming`. |
| `LOCALAPPDATA` | `User Shell Folders` or `Volatile Environment` populated by wineboot. | Alice's local AppData: `C:\users\.e1s\<SID>\<incarnation>-<name>\AppData\Local`. |
| `TEMP` / `TMP` | `HKCU\Environment` or `HKLM\Session Manager\Environment`. | Alice's private temp directory: `C:\users\.e1s\<SID>\<incarnation>-<name>\AppData\Local\Temp`. |
| `HOMEDRIVE` | Only in `Volatile Environment` written by wineboot (`wineboot.c:891`). Missing from `CreateEnvironmentBlock`. | Alice's drive letter component: `"C:"`. |
| `HOMEPATH` | Only in `Volatile Environment` written by wineboot (`wineboot.c:889`). Missing from `CreateEnvironmentBlock`. | Alice's profile directory path without drive: `\users\.e1s\<SID>\<incarnation>-<name>`. |
| `USERNAME` | `LookupAccountSidW` in `userenv_main.c:370` or `GetUserNameW` in `wineboot.c:895`. | Alice's canonical account name (e.g. `"alice"`). |
| `USERDOMAIN` | Only in `Volatile Environment` written by wineboot (`wineboot.c:906`). Missing from `CreateEnvironmentBlock`. | Computer name from `GetComputerNameW` / registry `ActiveComputerName`. |

---

## 2. Architecture & Design for E1-S15

### 2.1 Managed Volatile Environment Publication (`server/registry.c`)
In `wine/server/registry.c:e1s_load_hive`:
When Alice's managed hive is loaded:
1. Under `m->user` (`\Registry\User\<SID>`), create a volatile subkey `Volatile Environment`.
2. Populate the 9 canonical Alice variables:
   - `USERPROFILE` = `m->storage.path.windows`
   - `USERNAME` = `m->account.name`
   - `HOMEDRIVE` = `"C:"` (from `m->storage.path.windows[0..1]`)
   - `HOMEPATH` = `m->storage.path.windows + 2`
   - `USERDOMAIN` = computer name (from `ActiveComputerName` or system default)
   - `APPDATA` = `m->storage.path.windows` + `\AppData\Roaming`
   - `LOCALAPPDATA` = `m->storage.path.windows` + `\AppData\Local`
   - `TEMP` = `m->storage.path.windows` + `\AppData\Local\Temp`
   - `TMP` = `m->storage.path.windows` + `\AppData\Local\Temp`
3. Mark `Volatile Environment` with `KEY_VOLATILE` and **without `KEY_E1S_PERSIST`** so it is never saved to the durable hive on disk.

### 2.2 Client-Side Managed Profile & Shell-Folder Lookups
Gated under `#if defined(WEX_E1S13_HKU_TEST)`:
1. `userenv`:
   - `GetUserProfileDirectoryW`: queries `NtLoadWexProfileEx(WEX_HKU_QUERY, ...)` for `hToken` (or effective token if NULL).
     - If managed and loaded (`managed == 1 && gen > 0`): reads `USERPROFILE` from `Volatile Environment` under Alice's SID/HKCU.
     - If managed but profile not loaded: fails closed (`ERROR_NOT_READY`), never returning Bob's directory.
     - If unmanaged: preserves stock Wine behavior.
   - `CreateEnvironmentBlock`:
     - If token is managed: forces `bInherit = FALSE` (rejects caller environment inheritance). Sets `USERPROFILE` to Alice's S12 path, `USERNAME` to Alice, and populates `APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP`, `HOMEDRIVE`, `HOMEPATH`, `USERDOMAIN` from Alice's profile and `Volatile Environment`.
   - `ExpandEnvironmentStringsForUserW`:
     - If token is managed: expands strings using Alice's managed environment block rather than the calling process's environment.
2. `shell32`:
   - `_SHGetUserProfilePath`:
     - If token is managed and loaded:
       - For `CSIDL_PROFILE`: returns Alice's S12 profile path directly.
       - For other user shell folders: queries Alice's `User Shell Folders`. If missing, fails closed (`E_FAIL`); **never falls back** to HKLM or stock defaults.
   - `_SHExpandEnvironmentStrings`:
     - When expanding `%USERPROFILE%`: if effective token is managed and loaded, expands to Alice's S12 profile path instead of appending `GetUserNameW()`.

### 2.3 Pure Launch Preparation Function (`src/programs/wexelevate/launch_prep.c/.h`)
Located in `src/programs/wexelevate/launch_prep.c` and `.h`:
- **Placement Rationale**:
  1. The elevation broker (`wexelevate`) owns authorization, request validation, target environment construction, and launch staging (per `E1_DECISIONS.md:10` #1).
  2. The wineserver does not manage host Unix process spawning, `fork`/`exec`, or environment blocks.
  3. Placing launch preparation in `src/programs/wexelevate/` minimizes stock Wine modifications, keeps the Elevation broker self-contained, and prepares for S18 integration.
  4. The module is pure, unit-testable C, runnable natively under `scripts/test_launch_prep.sh`.

- **Contract and Validation Rules**:
  1. **Real-UID Rejection**:
     - Hard rejection (`WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED`) if the request specifies or requires a real host UID other than Bob's (`target_uid != bob_uid`).
     - Real/effective/saved host UID/GID remain Bob's.
  2. **Inherited Environment Rejection**:
     - Hard rejection (`WEX_LAUNCH_PREP_ERR_INHERIT_REJECTED`) if `WEXELEVATE_ENV_INHERIT` is requested.
  3. **Protected Variable Overrides**:
     - Hard rejection (`WEX_LAUNCH_PREP_ERR_PROTECTED_OVERRIDE`) if any caller-supplied environment tries to set or override protected variables: `USERNAME`, `USERPROFILE`, `APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP`, `HOMEDRIVE`, `HOMEPATH`, `USERDOMAIN`, `SystemRoot`, `SystemDrive`.
  4. **Sanitized Windows Environment**:
     - Built strictly from a clean allow-list. Contains Alice's protected variables, standard system variables (`SystemRoot`, `SystemDrive`, `ProgramFiles`, `ProgramFiles(x86)`, `CommonProgramFiles`, `CommonProgramFiles(x86)`, `ALLUSERSPROFILE`, `ProgramData`, `PUBLIC`, `COMPUTERNAME`, `COMSPEC`, `PATH`, `PATHEXT`, `PROCESSOR_ARCHITECTURE`).
  5. **Sanitized Unix Environment**:
     - Only allow-listed variables: `HOME` (Bob's home), `USER`, `LOGNAME`, `PATH`, `SHELL`, `TERM`, `LANG`, `LC_*`, `DISPLAY`, `XAUTHORITY`, `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, `XDG_SESSION_*`.
     - Explicitly stripped/rejected: `SSH_AUTH_SOCK`, `LD_*` (`LD_PRELOAD`, `LD_LIBRARY_PATH`), `WINE*` (`WINEPREFIX`, `WINEDEBUG`, `WINELOADER`, `WINESERVER`), `DBUS_SESSION_BUS_ADDRESS` (if foreign), `KRB5CCNAME`, `GPG_AGENT_INFO`.
  6. **Working Directory Check**:
     - Must be descriptor-anchored.
     - Traversing each component from the anchor rejects symlinks (`O_NOFOLLOW` / `lstat` `S_ISLNK`) and `..` path escapes.
     - Must be confirmed to reside inside Alice's profile directory (`profile_dir_fd`) or an allowed system directory (e.g. `drive_c/windows/system32`, `drive_c`).
     - Rejects non-directories, non-existent paths, and symlink traversal.
  7. **No Process Launch**:
     - Pure validation only; does not invoke `exec`, `fork`, `CreateProcess`, or credential launch.

---

## 3. Implementation and Verification Summary

### 3.1 Files Modified and Created
- **Wine Submodule** (`wine/`):
  - `wine/server/registry.c`:
    - Added `e1s_set_str_val` and `e1s_populate_volatile_env`.
    - Populates `Volatile Environment` under `m->user` with Alice's 9 variables (`USERPROFILE`, `USERNAME`, `HOMEDRIVE`, `HOMEPATH`, `USERDOMAIN`, `APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP`) and session values (`CLIENTNAME`, `SESSIONNAME`, `HOMESHARE`).
    - Marked with `KEY_VOLATILE` and without `KEY_E1S_PERSIST`, guaranteeing no persistence to disk in `e1s_write_keys`.
  - `wine/dlls/userenv/userenv_main.c`:
    - Gated under `WEX_E1S13_HKU_TEST`.
    - `GetUserProfileDirectoryW` / `GetUserProfileDirectoryA`: queries `NtLoadWexProfileEx(WEX_HKU_QUERY, ...)`; if managed and loaded, reads `USERPROFILE` from `Volatile Environment`; if managed and unloaded (`gen == 0`), fails closed with `ERROR_NOT_READY`; if unmanaged, preserves stock Wine behavior.
    - `CreateEnvironmentBlock`: if managed, forces `bInherit = FALSE` and injects Alice's S12 variables instead of concatenating `%SystemDrive%\users`.
    - `ExpandEnvironmentStringsForUserW` / `ExpandEnvironmentStringsForUserA`: if managed, expands variables against Alice's managed environment block.
  - `wine/dlls/shell32/shellpath.c`:
    - Gated under `WEX_E1S13_HKU_TEST`.
    - `_SHGetUserProfilePath`: if managed and loaded, returns Alice's S12 path for `CSIDL_PROFILE` (0x28); for other CSIDLs, queries Alice's `User Shell Folders` and fails closed (`E_FAIL`) without falling back to HKLM or stock defaults.
    - `_SHExpandEnvironmentStrings`: when expanding `%USERPROFILE%`, expands to Alice's S12 profile path if effective token is managed.
  - `wine/dlls/advapi32/tests/wex_hku.c`:
    - Added normal-server test assertions for `GetUserProfileDirectoryW`, `CreateEnvironmentBlock`, and `ExpandEnvironmentStringsForUserW` confirming normal tokens behave as stock Wine and never expose `.e1s`.

- **WineExplorer Project**:
  - `src/programs/wexelevate/launch_prep.h`:
    - Parameter, result, and status definitions for `wex_prepare_launch`.
  - `src/programs/wexelevate/launch_prep.c`:
    - Implementation of parameter checks, real-UID denial (`WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED`), caller inherit rejection (`WEX_LAUNCH_PREP_ERR_INHERIT_REJECTED`), protected override rejection (`WEX_LAUNCH_PREP_ERR_PROTECTED_OVERRIDE`), clean Windows allow-list construction, sanitized Unix environment generation, descriptor-anchored CWD verification rejecting symlinks (`WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL`), non-directories (`WEX_LAUNCH_PREP_ERR_CWD_NOT_DIR`), and unauthorized paths (`WEX_LAUNCH_PREP_ERR_CWD_NOT_ALLOWED`).
  - `src/programs/wexelevate/test_launch_prep_main.c`:
    - Standalone unit test suite exercising all cases.
  - `scripts/test_launch_prep.sh`:
    - Unit test harness script compiling production (disabled by default) and lab builds, setting up Bob fixture directories and symlinks, and executing test suites.
  - `scripts/e1s13_hku_checks.h`:
    - Extended with assertions that effective Alice token sees Alice's profile path and 9 variables, and that Bob peer sees Bob's profile path.
  - `scripts/test_e1s10_token.sh`:
    - Added `dlls/shell32/$arch-windows/shellpath.o` and `dlls/shell32/$arch-windows/shell32.dll` to `LAB_PROFILE_OBJECTS` and `LAB_PROFILE_DLLS` for rebuild with `-DWEX_E1S13_HKU_TEST`.
  - `docs/STOCK_CHANGES.md`:
    - Added E1-S15 entry documenting all changes.

### 3.2 Test Execution and Results
- **Native Unit Test**:
  - Script: `scripts/test_launch_prep.sh`
  - Command: `wsl -d wexbuild -u root --exec bash -c 'cd /mnt/d/DeepThought/Projects/WineExplorer && bash scripts/test_launch_prep.sh'`
  - Results:
    ```
    ==> Testing production binary (disabled by default)
    PASS test_disabled
    ==> Testing lab binary (all tests)
    PASS test_clean_win_env
    PASS test_inherit_rejected
    PASS test_protected_override
    PASS test_real_uid_rejected
    PASS test_sanitized_unix_env
    PASS test_cwd_checks
    ==> ALL E1-S15 launch prep tests passed successfully.
    ```
- **Static Checks**:
  - `git diff --check`: 0 trailing whitespace errors.
  - `git -C wine diff --check`: 0 trailing whitespace errors.
  - All new files contain `/* SPDX-License-Identifier: GPL-2.0-or-later */` headers.

