# Edge enterprise MSI analysis

## Flow timeline

| Time | Actor and operation | Evidence |
|---|---|---|
| 16:35:22 | `msiexec` starts the system-level MSI; its embedded `DoInstall` action launches the Omaha stub from `C:\users\debian\AppData\Local\Temp\msiA602.tmp`. | `msi.log:1-82`; `msi.stderr.log:222-225,343-372` |
| 16:35:22 onward | The stub starts the copied updater from `C:\Program Files (x86)\Microsoft\Temp\EUA79B.tmp\MicrosoftEdgeUpdate.exe /silent /install ...`. | `msi.stderr.log:369-389` |
| 16:35:36.600 | The updater performs `/regserver`; the log records `DllEntry` for the installed updater path. | `meu.log:27` |
| 16:35:36.863 | The updater tries to hand off to `C:\Program Files (x86)\Microsoft\EdgeUpdate\MicrosoftEdgeUpdate.exe /handoff ...`; Omaha reports `ShellExecuteEx` failure `0x80004005`, then `Failed to launch installed instance 0x80040708`. | `meu.log:58-60` |
| 16:35:36.892 | A later `/regserver` launch attempt reports `0x80070002` (file not found). | `meu.log:73-77` |
| 16:35:37.791 | Omaha records the install error and WER subcode `0x80040708`; MSI then receives the custom-action failure. | `meu.log:119-120`; `msi.stderr.log:1294-1295` |
| 16:35:38 | MSI rolls back; `DoInstall` ultimately reports failure to `InstallFinalize` and the process exits 91. | `msi.log:120-122,210-253`; `msi.stderr.log:1294-1295` |

The earlier no-network result (`0x80004001` and `RoGetActivationFactory` `0x80040154`) is a separate prerequisite failure. The network-enabled run reached Omaha config HTTP 200 (`meu.log:115,155`), so it is not the first divergence here.

## First divergence

The first actionable divergence is Omaha’s `ShellExecuteEx` failure for the installed updater handoff (`meu.log:58`), not MSI itself. The available trace does not expose the underlying `ShellExecuteEx` return path: `msi.stderr.log` has `+exec,+process`, but no `exec:` records for this call. Therefore the exact failing API is not proven.

What is ruled out by the current trace:

- The Temp Omaha copy is launchable: `CreateProcessInternalW` finds and starts `EUA79B.tmp\MicrosoftEdgeUpdate.exe` (`msi.stderr.log:385-389`).
- Service setup is reached: the installed updater is launched with `/regsvc` (`msi.stderr.log:453-457`) and `/svc` (`msi.stderr.log:782-788`).
- The COM registration helper is launchable: `MicrosoftEdgeUpdateComRegisterShell64.exe` is started repeatedly (`msi.stderr.log:629-633,671-675,714-718`).

The later `0x80070002` is consistent with Omaha probing the per-user path `C:\users\debian\AppData\Local\Microsoft\EdgeUpdate\MicrosoftEdgeUpdate.exe` (`msi.stderr.log:924-925,954-955`), but does not establish that this caused the earlier handoff failure. No Edge CLSID, `LocalService`, registry-view, or service-start error is present in these logs.

## Wine cause (file:function)

The relevant Wine path is `wine/dlls/shell32/shlexec.c:SHELL_execute` → `SHELL_FindExecutable` → `SHELL_ExecuteW` (the file uses debug channel `exec`, line 45). `SHELL_FindExecutable` performs explicit-path existence/search handling and can return `SE_ERR_FNF` before launch (around lines 695 onward). `SHELL_ExecuteW` then calls `CreateProcessAsUserW`; if that fails it maps the underlying result to `ERROR_BAD_FORMAT`, and only invokes the elevation broker when `GetLastError()` is `ERROR_ELEVATION_REQUIRED` (`shlexec.c:524-610`).

`wine/dlls/kernelbase/process.c:CreateProcessInternalW` checks `is_elevation_required()` and returns `STATUS_ELEVATION_REQUIRED` (`process.c:653-665`). `wine/dlls/combase/rpc.c:create_local_service` and `rpc_get_local_class_object` are not implicated by this run: the Edge trace contains no COM activation failure or CLSID error; the updater registration subprocesses are actually created. Likewise, `create_surrogate_server`’s absent-`DllSurrogate` behavior is unrelated unless a future COM trace demonstrates that Omaha uses that path.

## Proposed fix

No source change is justified yet. The minimal safe fix is to make `SHELL_ExecuteW` preserve and report the concrete handoff failure (especially `ERROR_FILE_NOT_FOUND`, `ERROR_ELEVATION_REQUIRED`, or helper/broker failure) instead of collapsing it to Omaha’s generic `0x80004005`; do not change COM registration, service semantics, or WOW64 registry behavior on this evidence alone.

If the focused trace below proves `ERROR_FILE_NOT_FOUND` while the installed file exists, the minimal code fix is in `wine/dlls/shell32/shlexec.c:SHELL_FindExecutable`: keep an explicit existing `.exe` path as the application command and bypass association/search failure. If it proves `ERROR_ELEVATION_REQUIRED`, the fix belongs in the `SHELL_ExecuteW` elevation-helper/broker branch, not MSI or COM. Both are hypotheses pending the trace.

## Next trace if needed

Repeat the same guest-internet MSI run with the single additional channel `+exec` captured separately:

```text
WINEDEBUG=+exec msiexec.exe /i Z:\home\debian\development-validation-20261101\MicrosoftEdgeEnterpriseX64.msi /qn /norestart /l*v Z:\home\debian\edge-msi-next\msi.log 2>Z:\home\debian\edge-msi-next\exec.log
```

Inspect the records immediately before the `meu.log:58` timestamp for `SHELL_FindExecutable`, `CreateProcessAsUserW`, `GetLastError`, `runas`, and the returned `hInstApp`/`hProcess`. This one channel distinguishes missing installed executable from elevation-helper failure without a VM or source edit.

## Follow-up findings (2026-11-05, Claude)

* `+exec` was captured on a second run: there are still no `exec:` records, so the handoff does not reach
  `SHELL_ExecuteW`. A small probe (`ShellExecuteExW` on `%ComSpec%` with no verb, `open` and `runas`, 32 and 64 bit)
  succeeds in the guest runtime, so plain `ShellExecuteEx` and `runas` work there.
* The installed updater files do exist while the install runs: `/regsvc`, `/regserver` and `/svc` are all started from
  `C:\Program Files (x86)\Microsoft\EdgeUpdate\...`; the stub removes the directory again when the install fails
  (it was absent after the run even with `DISABLEROLLBACK=1`).
* `meu.log` line 52: the `/svc` instance logs `is machine: 0` while every other instance logs `is machine: 1`. Omaha
  decides this from the account the process runs as, so the Edge update service was started without the LocalSystem
  token, and the `ShellExecuteEx` handoff fails 26 ms later. This is the same condition as the Chrome updater (services
  running without the SYSTEM token; see `CHROME_INSTALLER_ANALYSIS.md` section 11, Round 7). Treat Edge, the Chrome
  stub and the Chrome MSI as one blocker: service processes must run as `S-1-5-18`.
