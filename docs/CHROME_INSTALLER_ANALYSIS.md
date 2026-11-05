<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Google Chrome Enterprise MSI Installer Analysis

## Executive Summary

On 2026-11-02, an attempt to execute the Google Chrome enterprise MSI (`googlechromestandaloneenterprise64.msi`) under Wine inside the QEMU guest test environment failed during MSI execution:
`err:msi:execute_script ... DoInstall returned 1627`
followed by COM registration errors for CLSIDs `{6d40bdad-bf13-535b-bc3b-eeee65b1d745}` and `{8018f647-bf07-55bb-82be-a2d7049f7ce4}` ("not registered"), which caused Wine's surrogate executable `dllhost.exe` to spawn and terminate with `Can't create instance`.

This document synthesizes:
1. All recovered logs, traces, scripts, and embedded binary artifacts associated with the 2026-11-02 trial.
2. The exact identity of the failing CLSIDs within modern Chromium Updater (Omaha 4) and the installer flow leading to the failure.
3. A ranked analysis of architectural root causes within Wine (`wine/dlls/combase`, `wine/dlls/ole32`, `wine/dlls/rpcrt4`, `wine/programs/services`, `wine/dlls/advapi32`, `wine/programs/dllhost`, `wine/dlls/msi`, and `wine/server/registry.c`).
4. A diagnostic plan with concrete `WINEDEBUG` channels, guest registry inspection commands, minimal code fixes by file and function, and end-to-end acceptance gates.

All statements in this document are explicitly marked as either `[from logs]` (directly verified from repository files, extracted MSI tables, PE structures, or execution logs) or `[from knowledge, unverified]` (derived from Chromium/Omaha architecture or Windows internals knowledge, requiring guest-side runtime trace verification).

---

## 1. Inventory of Logs, Traces, and Artifacts

### 1.1 Repository and Lab References
- `docs/ROADMAP.md` (lines 262–270) `[from logs]`:
  Records the 2026-11-02 run in the combined WoW64 build inside the QEMU guest with `ChromiumLab/mk-gchrome.py`. The MSI starts, its `DoInstall` action runs Google's embedded installer, which fails:
  `err:msi:execute_script ... DoInstall returned 1627` after trying to create COM classes `{6d40bdad-bf13-535b-bc3b-eeee65b1d745}` and `{8018f647-bf07-55bb-82be-a2d7049f7ce4}` ("not registered"); `dllhost.exe` surrogate then exits with "Can't create instance".
- `D:/DeepThought/ChromiumLab/chrome-dl/googlechromestandaloneenterprise64.msi` `[from logs]`:
  The MSI package used for the trial (170,426,368 bytes, modified 2026-11-02 08:33:38).
  Product Code: `{50354927-E098-35AA-A0B1-3226D8CB43B6}`.
  Product Version: `156.0.8078.80`.
- `D:/DeepThought/ChromiumLab/mk-gchrome.py` `[from logs]`:
  Guest harness script generator (created 2026-11-02 08:35:46) that produces `validate-gchrome.py`. Invokes:
  `msiexec.exe /i <path>\googlechromestandaloneenterprise64.msi /qn /l*v <path>\msi.log`
  with a 900-second timeout, verifying subsequent presence of `drive_c/Program Files/Google/Chrome/Application/chrome.exe`.
- `D:/DeepThought/ChromiumLab/gchrome-run.sh` `[from logs]`:
  Host/guest execution script (modified 2026-11-02 08:48:21) running inside `/home/debian/development-validation-20261101` via `systemd-run` and `scripts/run-isolated-graphics.py --socket-root /var/tmp/edge-sock --private-loopback -- python3 validate-gchrome.py gchrome-1 $PWD/memory-runtime2/build/wow64 > gchrome-1.log 2>&1`.
- `D:/DeepThought/ChromiumLab/boot-gc1.log` `[from logs]`:
  Timestamped 2026-11-02 08:33:53 (0 bytes).
- Git Commit `4ce71520aa` `[from logs]`:
  Recorded on 2026-11-02 08:42:27 -0600 by zcomputerwiz & Claude Sonnet 5.5, committing the failure notes and roadmap item for stock browser installers into `docs/ROADMAP.md` and `docs/STOCK_CHANGES.md`.

### 1.2 MSI Database Structure & Custom Actions
Static inspection of `googlechromestandaloneenterprise64.msi` `[from logs]` reveals:
- **CustomAction Table**:
  - `DoInstall`: Type `3074` (`0x0C02`), Source `GoogleChromeInstaller`, Target `[InstallCommand]`.
    - Flag breakdown:
      - `0x0002` = `msidbCustomActionTypeBinaryData` (executable stored in Binary table).
      - `0x0400` = `msidbCustomActionTypeInScript` (deferred execution in execution script).
      - `0x0800` = `msidbCustomActionTypeNoImpersonate` (runs without user impersonation, i.e., elevated in system security context).
  - `BuildInstallCommand`: Target formula:
    `--silent --install="[ProductTag]" --installsource=enterprisemsi[OptOmahaArgs] --appargs="appguid={8A69D345-D564-463c-AFF1-A69D9E530F96}&installerdata=%7B%22distribution%22%3A%7B%22msi%22%3Atrue%2C%22system_level%22%3Atrue%2C%22verbose_logging%22%3Atrue%2C%22msi_product_id%22%3A%2250354927-E098-35AA-A0B1-3226D8CB43B6%22%2C%22allow_downgrade%22%3A[AllowDowngradeSubstitution]%7D%7D"`
- **Binary Table**:
  - `GoogleChromeInstaller`: Extracted size 168,473,016 bytes; SHA256 `d3d58b004932b2f4d1fbfd4b1638d56649779ea20a406489c83ff26ca8cbeb24`.
  - Machine Architecture: `0x14c` (32-bit x86 PE executable).
  - Embedded 7-Zip payload: Starts at offset `0x48c294` (`4,768,404` bytes), containing the updater and browser binaries.

---

## 2. CLSID Identification and Installer Flow

### 2.1 CLSID Disassembly & Mapping
Static reverse engineering of `GoogleChromeInstaller.exe` and cross-referencing with Chromium's `chrome/updater` source tree `[from logs]` confirms the exact role of each identifier:

| Identifier | Symbolic Name in Source | Role & Interface | Counterpart User Class |
|---|---|---|---|
| `{6d40bdad-bf13-535b-bc3b-eeee65b1d745}` | `UpdateServiceInternalClassSystem` (`InternalSystem`) | Internal IPC server interface (`IUpdaterInternalSystem`, IID `{acab122b-29c0-56a9-8145-afa2f82a547c}`). Used by the metainstaller to instruct the background updater service to install/register applications (`RunInstaller2`, `Install2`, `RegisterApp2`). | `InternalUser` (`{6e1e5c99-bc66-5838-926c-b47fc5a1b44a}`) |
| `{8018f647-bf07-55bb-82be-a2d7049f7ce4}` | `UpdateServiceClassSystem` (`ActiveSystem`) | Public on-demand COM server interface (`IUpdaterSystem`, `IUpdater2System`, and legacy `GoogleUpdate3Web` / `IAppBundleWeb`). Exposes update checks, policies, and process launching. | `ActiveUser` (`{547e9aef-8043-5d26-879f-01e7664192dc}`) |

Source provenance identified in PE strings `[from logs]`:
- `..\..\chrome\updater\app\server\win\com_classes.cc`
- `..\..\chrome\updater\ipc\proxy_impl_base_win.h`
- `..\..\chrome\updater\app\server\win\com_classes_legacy.cc`
- `..\..\chrome\updater\win\task_scheduler.cc`
- `..\..\chrome\installer\util\install_service_work_item_impl.cc`

### 2.2 Google's Installer Sequence Preceding `CoCreateInstance`
In Chromium Updater / modern Omaha 4 architecture `[from knowledge, unverified]` verified against disasm of `GoogleChromeInstaller.exe` `[from logs]`:

1. **Invocation**: MSI `DoInstall` runs `GoogleChromeInstaller.exe --silent --install=...`.
2. **IPC Discovery Probe**: Before doing a full re-installation of Google Update, `UpdateServiceInternalProxy` (`proxy_impl_base_win.h`) probes whether an active system updater instance already exists to service the install request.
3. **Failing Call**: At PE virtual address `0x71c89c` and `0x71ca22` `[from logs]`:
   ```c
   CoCreateInstance(
       CLSID_UpdateServiceInternalClassSystem, /* {6d40bdad-bf13-535b-bc3b-eeee65b1d745} */
       NULL,
       CLSCTX_LOCAL_SERVER,                   /* 0x4 */
       &IID_IUnknown,
       (void **)&punk
   );
   ```
4. **Registration Sequence in Normal Windows**:
   - If the updater is not yet installed:
     Windows returns `REGDB_E_CLASSNOTREG` (`0x80040154`). The installer then extracts `updater.7z`, creates the Windows Services (`GoogleUpdaterService` / `GoogleUpdaterInternalService`), schedules tasks via Task Scheduler 2.0 (`ITaskService`), writes the `HKLM\Software\Classes\CLSID` and `AppID` keys, and calls `CoCreateInstance` again.
   - If the updater is installed:
     The `AppID` contains `LocalService = "GoogleUpdaterInternalService..."`. Windows Service Control Manager (SCM) starts the service (if not already running), the service registers its class objects via `CoRegisterClassObject(..., CLSCTX_LOCAL_SERVER, ...)`, and COM returns the proxy interface.

---

## 3. Root Cause Analysis in Wine

Investigation of Wine sources (`wine/dlls/combase/rpc.c`, `wine/dlls/combase/combase.c`, `wine/programs/dllhost/dllhost.c`, `wine/dlls/msi/custom.c`, `wine/programs/services/services.c`, and `wine/server/registry.c`) highlights six contributing mechanisms, ranked by likelihood:

### Rank 1: Erroneous Surrogate Fallback in `combase/rpc.c` (`create_surrogate_server`)
- **Likelihood**: 95% (Directly explains the log signature).
- **Evidence `[from logs]`**:
  1. The log states: `dllhost.exe surrogate then exits with 'Can't create instance'`.
  2. `wine/programs/dllhost/dllhost.c` line 352 prints: `ERR("Can't create instance of %s\n", wine_dbgstr_guid(&clsid))` when `ISurrogate_LoadDllServer` fails to obtain an `InprocServer32` factory via `CoGetClassObject(clsid, CLSCTX_INPROC_SERVER, ...)`.
  3. In `wine/dlls/combase/rpc.c` lines 648–652:
     ```c
     if (tries == 1)
     {
         if ((hr = create_local_service(rclsid)) &&
             (hr = create_server(rclsid, &process)) &&
             (hr = create_surrogate_server(rclsid, &process)))
             return hr;
     }
     ```
  4. In `wine/dlls/combase/rpc.c` lines 577–589 (`create_surrogate_server`):
     ```c
     ret = RegQueryValueExW(key, L"DllSurrogate", NULL, NULL, (LPBYTE)command, &size);
     RegCloseKey(key);
     if (ret || !size || !command[0])
     {
         TRACE("No value for DllSurrogate key\n");
         ...
         wcscat(command, L"\\dllhost.exe");
     }
     ```
     **The Bug**: If `RegQueryValueExW` fails because `DllSurrogate` **does not exist at all** in the registry (`ret == ERROR_FILE_NOT_FOUND`), Wine evaluates `ret || !size || !command[0]` as **TRUE**, erroneously assumes a default system surrogate is requested, and executes `dllhost.exe /PROCESSID:{rclsid}`.
     In Windows `[from knowledge, unverified]`, `dllhost.exe` is ONLY launched if the `DllSurrogate` value name is present in the AppID key. If `DllSurrogate` is absent, surrogate activation fails immediately without spawning `dllhost.exe`.
  5. Furthermore, `create_local_service` fails (falling through to `create_surrogate_server`) because:
     - Wine queries `L"ServiceParams"` (`rpc.c` line 454) instead of `L"ServiceParameters"` (`GoogleChromeInstaller.exe` offset `0x403f68`).
     - If the Windows service has not been installed yet or cannot be started via SCM, `create_local_service` returns `REGDB_E_CLASSNOTREG` instead of aborting cleanly.

### Rank 2: Service-Hosted COM Server Activation (`AppID\LocalService`) in Wine SCM
- **Likelihood**: 85%.
- **Evidence `[from logs]`**:
  `GoogleChromeInstaller.exe` configures its COM servers as Windows Services using `LocalService` under the AppID key (`0x403f4e` in `.rdata`).
  In `wine/dlls/combase/rpc.c`, `create_local_service` calls `start_local_service()`, which invokes `OpenServiceW(handle, name, SERVICE_START)` and `StartServiceW()`.
  If Google's metainstaller has not yet created the service, `OpenServiceW` fails with `ERROR_SERVICE_DOES_NOT_EXIST`. Wine maps this to `REGDB_E_CLASSNOTREG`, but then mistakenly cascades into `create_server` and `create_surrogate_server` instead of returning the class-not-registered error to the client.

### Rank 3: WOW64 32-bit vs. 64-bit Registry View Mismatch for CLSIDs
- **Likelihood**: 70%.
- **Evidence `[from logs]`**:
  `GoogleChromeInstaller.exe` is a 32-bit binary (`0x14c`), while the Wine prefix is 64-bit (`memory-runtime2/build/wow64`).
  In `wine/dlls/combase/combase.c` lines 270–282:
  ```c
  const BOOL is_win64 = sizeof(void*) > sizeof(int);
  const BOOL force_wow32 = is_win64 && (access & KEY_WOW64_32KEY);
  ```
  In a 32-bit process, `is_win64` is FALSE, so `force_wow32` is FALSE, causing `get_classes_root_hkey` to open `\Registry\Machine\Software\Classes` directly.
  In `wine/server/registry.c` line 2695, `Software\Classes` is flagged as `KEY_WOWSHARE`. If 32-bit installer writes register keys into `Software\Classes\CLSID` but 64-bit Wine lookup expects them in `Software\Classes\Wow6432Node\CLSID` (or vice versa), `open_key_for_clsid` fails with `REGDB_E_CLASSNOTREG`.

### Rank 4: Task Scheduler 2.0 API (`taskschd.dll`) Registration Failure
- **Likelihood**: 60%.
- **Evidence `[from logs]`**:
  `GoogleChromeInstaller.exe` imports `taskschd.dll` and references `RegisterTaskDefinition` to schedule `GoogleUpdateTaskMachineCore` and `GoogleUpdateTaskMachineUA`.
  In stock Wine `[from knowledge, unverified]`, `taskschd.dll` contains stub methods that return `E_NOTIMPL` or do not persist task XML definitions. If Omaha treats scheduled task registration failure as fatal during its setup transaction, it aborts and rolls back before reaching the final browser deployment.

### Rank 5: MSI Custom Action Security Context (`msidbCustomActionTypeNoImpersonate`)
- **Likelihood**: 50%.
- **Evidence `[from logs]`**:
  MSI CustomAction `DoInstall` is Type `3074`.
  In `wine/dlls/msi/custom.c` line 1572:
  `if (type & msidbCustomActionTypeNoImpersonate) WARN("msidbCustomActionTypeNoImpersonate not handled\n");`
  Wine executes the custom action under the current interactive user process token rather than synthesizing a LocalSystem (`S-1-5-18`) elevated environment.
  In WineExplorer, elevation enforcement (`elevation_enforced()` and `sysrun_active` in `wine/server/registry.c` line 984) restricts non-SYSTEM tokens from writing into certain machine registry subtrees or managing SCM services.

### Rank 6: Elevation Moniker / COM+ Catalog
- **Likelihood**: 25%.
- **Evidence `[from logs]`**:
  While `GoogleChromeInstaller.exe` contains strings for `IProcessLauncher`, disasm of the failing call at `0x71c89c` shows direct `CoCreateInstance(..., CLSCTX_LOCAL_SERVER)` rather than `CoGetObject("Elevation:Administrator!new:...")`. Thus, elevation moniker parsing is not the immediate failure trigger.

---

## 4. Concrete Diagnostic Plan

### 4.1 Diagnostic WINEDEBUG Channels
Run the MSI installation with the following debug channels to capture the exact registry queries, SCM service actions, and COM activation attempts:
```bash
WINEDEBUG=+ole,+combase,+rpc,+reg,+service,+msi,+dllhost
```

### 4.2 Exact Inspection Commands in QEMU Guest
Commands executed in the guest environment (`development-validation-20261101`) to inspect prefix state before and after failure:

1. **Check CLSID registration in both 64-bit and 32-bit registry views**:
   ```cmd
   wine reg query "HKLM\Software\Classes\CLSID\{6d40bdad-bf13-535b-bc3b-eeee65b1d745}" /s
   wine reg query "HKLM\Software\Classes\CLSID\{8018f647-bf07-55bb-82be-a2d7049f7ce4}" /s
   wine reg query "HKLM\Software\Classes\Wow6432Node\CLSID\{6d40bdad-bf13-535b-bc3b-eeee65b1d745}" /s
   wine reg query "HKLM\Software\Classes\Wow6432Node\CLSID\{8018f647-bf07-55bb-82be-a2d7049f7ce4}" /s
   ```
   *Confirmation*: If keys exist only under `Wow6432Node`, confirms WOW64 redirection mismatch (Hypothesis 3). If keys exist with `AppID`, confirms Hypothesis 1 & 2.

2. **Check AppID values for `LocalService` vs `DllSurrogate`**:
   ```cmd
   wine reg query "HKLM\Software\Classes\AppID\{6d40bdad-bf13-535b-bc3b-eeee65b1d745}" /s
   wine reg query "HKLM\Software\Classes\AppID\{8018f647-bf07-55bb-82be-a2d7049f7ce4}" /s
   ```
   *Confirmation*: Confirms whether `LocalService` is registered and whether `DllSurrogate` is absent (proving Wine's surrogate launcher was falsely triggered).

3. **Check Service Control Manager state for Google Update services**:
   ```cmd
   wine reg query "HKLM\System\CurrentControlSet\Services" /k /f "Google"
   wine sc query gupdate
   wine sc query gupdatem
   ```
   *Confirmation*: Confirms whether Google's installer created the Windows Services in SCM or if `services.exe` rejected the service creation.

### 4.3 Minimal Wine Source Fixes by File and Function

#### Fix 1: Guard `create_surrogate_server` against Missing `DllSurrogate`
- **File**: `wine/dlls/combase/rpc.c`
- **Function**: `create_surrogate_server(REFCLSID rclsid, HANDLE *process)`
- **Change**: Ensure `dllhost.exe` is launched **only if** `RegQueryValueExW(key, L"DllSurrogate", ...)` succeeds (`ret == ERROR_SUCCESS`). If `ret != ERROR_SUCCESS`, return `REGDB_E_CLASSNOTREG` immediately without spawning `dllhost.exe`.
```c
/* Only activate surrogate if DllSurrogate value is explicitly present */
ret = RegQueryValueExW(key, L"DllSurrogate", NULL, NULL, (LPBYTE)command, &size);
RegCloseKey(key);
if (ret != ERROR_SUCCESS)
{
    TRACE("DllSurrogate value not present in AppID key; not a surrogate server\n");
    return REGDB_E_CLASSNOTREG;
}
if (!size || !command[0])
{
    /* Empty DllSurrogate value indicates default system surrogate dllhost.exe */
    ...
}
```

#### Fix 2: Support `ServiceParameters` in `create_local_service`
- **File**: `wine/dlls/combase/rpc.c`
- **Function**: `create_local_service(REFCLSID rclsid)`
- **Change**: Check for `L"ServiceParameters"` in addition to `L"ServiceParams"`.
```c
r = RegQueryValueExW(hkey, L"ServiceParameters", NULL, &type, NULL, &sz);
if (r != ERROR_SUCCESS)
    r = RegQueryValueExW(hkey, L"ServiceParams", NULL, &type, NULL, &sz);
```

#### Fix 3: Prevent False Cascade from `create_local_service` to Surrogate
- **File**: `wine/dlls/combase/rpc.c`
- **Function**: `rpc_get_local_class_object`
- **Change**: If `open_appidkey_from_clsid` finds a `LocalService` value, do not fall through to `create_server` or `create_surrogate_server` if `start_local_service` fails.

### 4.4 Verification & Acceptance Criteria
Validation in QEMU guest (`validate-gchrome.py`):
1. **Exit Code**: `msiexec.exe /i googlechromestandaloneenterprise64.msi /qn` exits with code `0`.
2. **Payload Existence**: `drive_c/Program Files/Google/Chrome/Application/chrome.exe` is present and non-empty.
3. **Uninstall Registry**: `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\{50354927-E098-35AA-A0B1-3226D8CB43B6}` or `Google Chrome` exists with valid `DisplayName` and `UninstallString`.
4. **Execution & Rendering**: Launching `chrome.exe` produces an active browser window in `wexcomp`, captures valid client pixels in `run-isolated-graphics.py`, and records clean shutdown without zombie processes.

---

## 5. Runtime Findings (2026-11-05 trace)

Replayed the failing install in the QEMU guest (Debian WSL distro, VirGL) with full COM/registry/service
tracing, then dumped the registry in both WOW64 views. Artifacts: `D:/DeepThought/ChromiumLab/msi-trace/`
(`trace-msi-trace.log` 2.4 MB full trace, `trace-system.reg` 3.8 MB registry, `trace-msi.log` MSI log).

### 5.1 Method

- Booted the guest (`WEX_G4_RUN_NAME=chrome-msi-trace-20261105`), reused the 2026-11-02 runtime
  (`/home/debian/development-validation-20261101/memory-runtime2/build/wow64`) and MSI
  (`/home/debian/development-validation-20261101/googlechromestandaloneenterprise64.msi`).
- Fresh prefix, `wineboot --init`, then
  `WINEDEBUG=+timestamp,+pid,+ole,+combase,+rpc,+reg,+service,+msi,+process msiexec /i <msi> /qn /l*v <msi.log>`.
- After the failure: `wine reg query` for both CLSIDs/AppIDs in both views, `wine sc query` for the updater
  service, and a full `system.reg` dump.

### 5.2 Reproduction

The failure reproduced exactly:

```
23848:572.000:012c:0130:err:msi:execute_script Execution of script 0 halted; action L"DoInstall" returned 1627
23868:572.000:012c:0130:err:msi:ITERATE_Actions Execution halted, action L"InstallFinalize" returned 1627
```

`msiexec` exit code **91**. The MSI's own log (`trace-msi.log`) shows the install was rolled back
(`RemoveFiles`, `RemoveFolders`, `CreateFolders`, `InstallFiles`, `WriteRegistryValues`, `ProcessComponents`
all run as rollback actions after the failure).

### 5.3 COM activation sequence for `{6d40bdad}` (InternalSystem)

The installer's COM client (PID `0190`) activates `{6d40bdad-bf13-535b-bc3b-eeee65b1d745}` six times
(~250 ms apart, ts 569.380 → 570.654). First attempt, full registry trace:

```
569.380:0190:01c8:trace:ole:create_local_service Attempting to start Local service for {6d40bdad-bf13-535b-bc3b-eeee65b1d745}
569.380:0190:01c8:trace:reg:NtOpenKeyEx (0xdc,L"CLSID\{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}",20019,0x10051f4c8)
569.380:0190:01c8:trace:reg:NtOpenKeyEx <- 0x184
569.380:0190:01c8:trace:reg:RegQueryValueExW (00000184,L"AppId",00000000,0159F594,0159F59A,0159F58C=78)
569.380:0190:01c8:trace:reg:NtOpenKeyEx (0xdc,L"AppId\{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}",20019,0x10051f4c8)
569.380:0190:01c8:trace:reg:NtOpenKeyEx <- 0x184
569.380:0190:01c8:trace:reg:RegQueryValueExW (00000184,L"LocalService",00000000,0159F6A4,0159F6B0,0159F6A8=78)
569.380:0190:01c8:trace:reg:NtQueryValueKey (0x184,L"LocalService",2,0x159f544,90)
569.380:0190:01c8:warn:ole:create_local_service No LocalService value
569.380:0190:01c8:trace:ole:create_server Attempting to start server for {6d40bdad-bf13-535b-bc3b-eeee65b1d745}
569.380:0190:01c8:trace:reg:NtOpenKeyEx (0xdc,L"CLSID\{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}",20019,0x10051f4c8)
569.380:0190:01c8:trace:reg:NtOpenKeyEx <- 0x184
569.380:0190:01c8:trace:reg:NtOpenKeyEx (0x184,L"LocalServer32",20019,0x10051f4c8)
569.380:0190:01c8:trace:reg:NtOpenKeyEx <- (nil)
569.380:0190:01c8:trace:reg:NtCreateKey ((nil),L"\\Registry\\Machine\\Software\\Classes",<null>,0,2000100,0x10051f4a8)
569.380:0190:01c8:trace:reg:NtCreateKey <- 0x184
569.380:0190:01c8:trace:ole:create_classes_root_hkey L"\\Registry\\Machine\\Software\\Classes" -> 00000184
569.380:0190:01c8:trace:reg:NtOpenKeyEx (0x184,L"CLSID\{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}",20119,0x10051f4c8)
569.380:0190:01c8:trace:reg:NtOpenKeyEx <- (nil)
569.380:0190:01c8:err:ole:create_server class {6d40bdad-bf13-535b-bc3b-eeee65b1d745} not registered
569.380:0190:01c8:trace:ole:create_surrogate_server Attempting to start surrogate server for {6d40bdad-bf13-535b-bc3b-eeee65b1d745}
569.380:0190:01c8:trace:reg:NtOpenKeyEx (0xdc,L"CLSID\{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}",20019,0x10051f4c8)
569.380:0190:01c8:trace:reg:NtOpenKeyEx <- 0x18c
569.380:0190:01c8:trace:reg:RegQueryValueExW (0000018C,L"AppId",00000000,0159F26C,0159F272,0159F264=78)
569.380:0190:01c8:trace:reg:NtOpenKeyEx (0xdc,L"AppId\{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}",20019,0x10051f4c8)
569.380:0190:01c8:trace:reg:NtOpenKeyEx <- 0x18c
569.380:0190:01c8:trace:reg:RegQueryValueExW (0000018C,L"DllSurrogate",00000000,00000000,0159F3D4,0159F378=522)
569.380:0190:01c8:trace:reg:NtQueryValueKey (0x18c,L"DllSurrogate",2,0x159f21c,256)
569.381:0190:01c8:trace:ole:create_surrogate_server No value for DllSurrogate key
569.381:0190:01c8:trace:ole:create_surrogate_server Activating surrogate local server L"C:\windows\system32\dllhost.exe /PROCESSID:{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}"
569.381:0190:01c8:trace:process:CreateProcessInternalW app (null) cmdline L"C:\windows\system32\dllhost.exe /PROCESSID:{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}"
```

Then the spawned `dllhost` (PID `024c`) fails:

```
569.421:024c:0250:warn:ole:com_get_class_object class {6d40bdad-bf13-535b-bc3b-eeee65b1d745} not registered as in-proc server
569.421:024c:0250:err:dllhost:wWinMain Can't create instance of {6d40bdad-bf13-535b-bc3b-eeee65b1d745}
569.421:0190:01c8:err:ole:com_get_class_object no class object {6d40bdad-bf13-535b-bc3b-eeee65b1d745} could be created for context 0x4
```

Key observations:

1. **`create_local_service` opens the AppID key via HKCR (`0xdc`) and finds no `LocalService` value**
   (`No LocalService value`, line 16734) — even though the installer wrote `LocalService` to the AppID key
   3.2 s earlier (see §5.5).
2. **`create_server` finds no `LocalServer32`** (the `NtOpenKeyEx (0x184,L"LocalServer32",...)` returns
   `(nil)`), and the WOW64 fallback re-open of the CLSID key in the opposite view also returns `(nil)`, so it
   reports `class ... not registered`.
3. **`create_surrogate_server` finds no `DllSurrogate` value** (`No value for DllSurrogate key`, line 16758)
   **yet still launches `dllhost.exe /PROCESSID:{6D40BDAD-...}`** (line 16759). This is the surrogate-fallback
   bug (Hypothesis 1): Windows only starts a surrogate when the AppID has a `DllSurrogate` value (empty value
   = default `dllhost`); an *absent* value means "not a surrogate server".
4. The `dllhost` process then fails with `Can't create instance` because the class is not actually registered
   as an in-proc server in that view.

### 5.4 COM activation sequence for `{8018f647}` (ActiveSystem)

`{8018f647-bf07-55bb-82be-a2d7049f7ce4}` is activated six times (ts 565.753 → 570.892). Each attempt:

```
565.753:0190:01c8:trace:ole:create_local_service Attempting to start Local service for {8018f647-bf07-55bb-82be-a2d7049f7ce4}
565.753:0190:01c8:trace:ole:create_server Attempting to start server for {8018f647-bf07-55bb-82be-a2d7049f7ce4}
565.753:0190:01c8:err:ole:create_server class {8018f647-bf07-55bb-82be-a2d7049f7ce4} not registered
565.753:0190:01c8:trace:ole:create_surrogate_server Attempting to start surrogate server for {8018f647-bf07-55bb-82be-a2d7049f7ce4}
565.753:0190:01c8:err:ole:com_get_class_object no class object {8018f647-bf07-55bb-82be-a2d7049f7ce4} could be created for context 0x4
```

Unlike `{6d40bdad}`, **no `dllhost` is launched** for `{8018f647}`: its CLSID key does not exist at all
(0 occurrences of `8018F647` in `system.reg`), so `create_surrogate_server` fails before reaching the
`DllSurrogate` lookup. The class was never registered by the installer.

### 5.5 Registry write timeline (installer, PID `0190`)

The installer writes the COM registration via **direct `HKLM` (`0x24`) `Software\Classes` access** at
ts 566.187, and creates the updater service at ts 566.184–185:

```
13736:566.184:0190:01c8:trace:service:OpenServiceW 00F84160 L"GoogleUpdaterInternalService157.0.8089.0" 0x10003
13815:566.185:0190:01c8:trace:service:CreateServiceW 00F84160 L"GoogleUpdaterInternalService157.0.8089.0" L"Google Updater Internal Service (GoogleUpdaterInternalService157.0.8089.0)"
14141:566.187:0190:01c8:trace:reg:NtCreateKey (0x24,L"Software\Classes\CLSID\{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}",(null),0,20019,0x10051f4a8)
14154:566.187:0190:01c8:trace:reg:NtCreateKey (0x24,L"Software\Classes\AppID\{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}",(null),0,20019,0x10051f4a8)
14160:566.187:0190:01c8:trace:reg:NtSetValueKey (0x17c,L"LocalService",1,0x3749c580,82)
14165:566.187:0190:01c8:trace:reg:NtSetValueKey (0x17c,L"ServiceParameters",1,0x37460de0,28)
```

Notes:

- The service `GoogleUpdaterInternalService157.0.8089.0` is **created but never started** (no
  `StartServiceW` for it; the only `StartServiceW` calls are for `Schedule`).
- The AppID value name written is **`ServiceParameters`** (not `ServiceParams`), confirming Hypothesis 2.
- The `LocalService` write (handle `0x17c`) goes to a key opened via **`HKLM` (`0x24`)**, while the COM
  activation reads the AppID key via **`HKCR` (`0xdc`)** — these resolve to different WOW64 views (see §5.6).

### 5.6 Final registry state — WOW64 view mismatch (root cause)

From the full `system.reg` dump after the failure:

```
[Software\Classes\AppID\{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}] 1793859102        <- 64-bit view (no Wow6432Node)
#time=1dd6cec86c3b4dc
"LocalService"="GoogleUpdaterInternalService157.0.8089.0"
"ServiceParameters"="--com-service"

[Software\Classes\Wow6432Node\CLSID\{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}] 1793859102   <- 32-bit view
#time=1dd6cec86c3aac8
"AppID"="{6D40BDAD-BF13-535B-BC3B-EEEE65B1D745}"
```

- The **AppID key** (with `LocalService` + `ServiceParameters`) is in the **64-bit view**
  (`Software\Classes\AppID\{...}`, no `Wow6432Node`).
- The **CLSID key** (with the `AppID` value) is in the **32-bit view**
  (`Software\Classes\Wow6432Node\CLSID\{...}`).
- `{8018f647}`: **0 occurrences** in `system.reg` — never registered.

Both keys were created by the *same* direct `HKLM\Software\Classes` access (same PID, same ts, §5.5), yet they
landed in **different WOW64 views**. The 32-bit COM client then reads via `HKCR`:

- `HKCR\CLSID\{6D40BDAD}` → resolves to the **32-bit** view → CLSID key **found** (has `AppID`).
- `HKCR\AppId\{6D40BDAD}` → the 32-bit view has no AppID key with `LocalService`; the value the installer
  wrote is in the **64-bit** view, which the 32-bit `HKCR` read does not see → **`No LocalService value`**.

So the write (direct `HKLM`, one view) and the read (`HKCR`, the other view) resolve to **different keys**.
This is the root cause of the 1627: the COM activation cannot see the `LocalService` registration the
installer just wrote.

### 5.7 Hypothesis verdicts

| # | Hypothesis | Verdict | Evidence |
|---|------------|---------|----------|
| 1 | Surrogate fallback when `DllSurrogate` absent | **CONFIRMED (real bug, not the root cause)** | `No value for DllSurrogate key` (line 16758) immediately followed by `Activating surrogate local server L"C:\windows\system32\dllhost.exe /PROCESSID:{6D40BDAD-...}"` (line 16759). Wine launches `dllhost` even though the AppID has no `DllSurrogate` value. |
| 2 | `ServiceParams` vs `ServiceParameters` | **CONFIRMED** | Installer writes `NtSetValueKey (0x17c,L"ServiceParameters",...)` (line 14165); `create_local_service` only queries `L"ServiceParams"`. |
| 3 | Local-service start failures | **PARTIALLY CONFIRMED** | `GoogleUpdaterInternalService157.0.8089.0` is created (`CreateServiceW`, line 13815) but never started (no `StartServiceW` for it). However, the activation never reaches `start_local_service` because `LocalService` is not seen (§5.6). |
| 4 | WOW64 registry view mismatch | **CONFIRMED (ROOT CAUSE)** | AppID key in 64-bit view, CLSID key in 32-bit view (§5.6); write via `HKLM` (`0x24`), read via `HKCR` (`0xdc`) resolve to different keys; `No LocalService value` (line 16734) despite the write at line 14160. |
| 5 | `taskschd` | **Not the direct cause** | `StartServiceW Schedule` occurs (ts 566.195, 567.258) but is unrelated to the COM activation failure. |
| 6 | MSI `NoImpersonate` | **Not the direct cause** | No evidence in the trace; the failure is in COM activation, not MSI privilege handling. |

### 5.8 Root cause

**WOW64 registry view mismatch.** The installer writes the COM registration (CLSID, AppID, `LocalService`,
`ServiceParameters`) via direct `HKLM\Software\Classes` access. In Wine, that access resolves the CLSID key to
the 32-bit view and the AppID key to the 64-bit view (inconsistent redirection). The 32-bit COM client then
reads via `HKCR`, which resolves the CLSID key to the 32-bit view (found) but does **not** see the `LocalService`
value the installer wrote to the 64-bit-view AppID key. `create_local_service` therefore reports
`No LocalService value`, the cascade falls through to `create_server` (no `LocalServer32`) and then
`create_surrogate_server` (no `DllSurrogate`, but Wine still launches `dllhost`, which fails with
`Can't create instance`). The COM activation fails, the installer's custom action returns 1627, and the MSI
rolls back.

### 5.9 Proposed Wine fix (NOT applied — for review)

The smallest change that addresses the root cause is a **WOW64 fallback in `create_local_service`**: when the
AppID key is found but has no `LocalService` value, retry the AppID key in the opposite WOW64 view (mirroring
the fallback `create_surrogate_server` already has). Two smaller, independent fixes are also recommended.

#### Fix 1 (primary): WOW64 fallback for the AppID key in `create_local_service`

- **File**: `wine/dlls/combase/rpc.c`
- **Function**: `create_local_service(REFCLSID rclsid)` (line 424)
- **Change**: After `open_appidkey_from_clsid` succeeds but `RegQueryValueExW(hkey, L"LocalService", ...)`
  fails, close the key and retry `open_appidkey_from_clsid` with the opposite WOW64 view, then re-query
  `LocalService`. Only report `No LocalService value` if both views lack it.

```c
    int arch = (sizeof(void *) > sizeof(int)) ? 64 : 32;
    REGSAM opposite = (arch == 64) ? KEY_WOW64_32KEY : KEY_WOW64_64KEY;
    BOOL is_wow64 = FALSE;

    hr = open_appidkey_from_clsid(rclsid, KEY_READ, &hkey);
    if (FAILED(hr))
        return hr;

    sz = sizeof buf;
    r = RegQueryValueExW(hkey, L"LocalService", NULL, &type, (LPBYTE)buf, &sz);
    if (r != ERROR_SUCCESS || type != REG_SZ)
    {
        /* AppID key found but no LocalService: try the opposite WOW64 view,
         * in case the registration was written there. */
        RegCloseKey(hkey);
        if (arch == 64 || (IsWow64Process(GetCurrentProcess(), &is_wow64) && is_wow64))
        {
            hr = open_appidkey_from_clsid(rclsid, opposite | KEY_READ, &hkey);
            if (SUCCEEDED(hr))
            {
                sz = sizeof buf;
                r = RegQueryValueExW(hkey, L"LocalService", NULL, &type, (LPBYTE)buf, &sz);
            }
        }
        if (r != ERROR_SUCCESS || type != REG_SZ)
        {
            WARN("No LocalService value\n");
            RegCloseKey(hkey);
            return REGDB_E_CLASSNOTREG;
        }
    }
    /* ... existing ServiceParams read + start_local_service ... */
```

This makes the 32-bit COM client see the `LocalService` value the installer wrote to the 64-bit-view AppID key,
so `start_local_service` is invoked and the activation can succeed.

#### Fix 2: Support `ServiceParameters` in `create_local_service`

- **File**: `wine/dlls/combase/rpc.c`
- **Function**: `create_local_service(REFCLSID rclsid)`
- **Change**: Query `L"ServiceParameters"` first, falling back to `L"ServiceParams"`.

```c
    r = RegQueryValueExW(hkey, L"ServiceParameters", NULL, &type, NULL, &sz);
    if (r != ERROR_SUCCESS)
        r = RegQueryValueExW(hkey, L"ServiceParams", NULL, &type, NULL, &sz);
```

#### Fix 3: Do not launch `dllhost` when `DllSurrogate` is absent

- **File**: `wine/dlls/combase/rpc.c`
- **Function**: `create_surrogate_server(REFCLSID rclsid, HANDLE *process)` (line 546)
- **Change**: When `RegQueryValueExW(key, L"DllSurrogate", ...)` returns `ERROR_FILE_NOT_FOUND` (value absent),
  return `REGDB_E_CLASSNOTREG` instead of building and launching the default `dllhost.exe`. Only an *empty*
  `DllSurrogate` value should select the default `dllhost`.

```c
    ret = RegQueryValueExW(key, L"DllSurrogate", NULL, NULL, (LPBYTE)command, &size);
    RegCloseKey(key);
    if (ret == ERROR_FILE_NOT_FOUND)
    {
        TRACE("No DllSurrogate value; not a surrogate server\n");
        return REGDB_E_CLASSNOTREG;
    }
    if (!size || !command[0])
    {
        /* Empty DllSurrogate value: use the default system surrogate. */
        TRACE("Empty DllSurrogate value; using default dllhost\n");
        if ((sizeof(void *) == 8 || is_wow64) && opposite == KEY_WOW64_32KEY)
            GetSystemWow64DirectoryW(command, MAX_PATH - ARRAY_SIZE(L"\\dllhost.exe"));
        else
            GetSystemDirectoryW(command, MAX_PATH - ARRAY_SIZE(L"\\dllhost.exe"));
        wcscat(command, L"\\dllhost.exe");
    }
```

**Note on the deeper issue.** Fix 1 papers over the inconsistent WOW64 redirection (the CLSID and AppID keys
landing in different views despite the same `HKLM\Software\Classes` write). The proper fix is in the registry
redirection logic so that direct `HKLM\Software\Classes` access and `HKCR` access resolve to the same view for a
given process bitness. That is a larger change in `wine/server/registry.c` / the `HKCR` merge code and is out of
scope for the minimal fix; Fix 1 is the smallest change that unblocks the Chrome install.

### 5.10 Commands used

```bash
# Boot (Git Bash; MSYS_NO_PATHCONV=1 required)
export MSYS_NO_PATHCONV=1
wsl -d Debian -u root --exec unshare --mount --propagation private \
  env WEX_G4_RUN_NAME=chrome-msi-trace-20261105 \
  python3 -B /mnt/d/DeepThought/ChromiumLab/launch-16g.py

# SSH wrapper into the guest (port 2235, user debian)
wsl -d Debian --exec bash /mnt/d/DeepThought/ChromiumLab/g.sh '<cmd>'

# Trace run (script: D:/DeepThought/ChromiumLab/msi-trace/run-trace.sh, run detached with nohup)
#   - fresh prefix, wineboot --init
#   - WINEDEBUG=+timestamp,+pid,+ole,+combase,+rpc,+reg,+service,+msi,+process \
#       msiexec /i /home/debian/development-validation-20261101/googlechromestandaloneenterprise64.msi /qn /l*v msi.log
#   - wine reg query for both CLSIDs/AppIDs in both views
#   - wine sc query for the updater service
#   - full system.reg dump

# Pull artifacts to D:/DeepThought/ChromiumLab/msi-trace/
#   trace-msi-trace.log, trace-msi.log, trace-system.reg, trace-run-stdout.log, trace-wineboot.log
```

The VM was shut down at its 900 s deadline; no `qemu-system` processes remain.
