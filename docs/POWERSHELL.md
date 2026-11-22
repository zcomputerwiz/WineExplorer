# PowerShell 7 on WineExplorer: CIM, `mi.dll` and the computer cmdlets

The official PowerShell 7 (MIT, `PowerShell-7.x-win-x64.zip` from the PowerShell releases page) runs on the WoW64 Wine
build. It is not in the image; it is a download for whoever wants it. What WineExplorer adds so that its CIM and computer
cmdlets work (2026-11-22, tried with 7.7.0, x64):

| Part | Where | What it does |
|---|---|---|
| `mi.dll` | `src/dlls/mi` | The client side of the Management Infrastructure API that `Microsoft.Management.Infrastructure` (the CIM cmdlets) calls. Wine had no `mi.dll`; without it every CIM cmdlet failed in `ApplicationMethods`. It answers over WMI (`wbemprox`): `Get-CimInstance`, `-Query` (WQL), `New-CimSession`, `Invoke-CimMethod`, the class information PowerShell asks for before it invokes a method. |
| `Win32_ComputerSystem` | `wine/dlls/wbemprox` (`builtin.c`, `compsys.c`) | The properties `PartOfDomain`, `Workgroup`, `DNSHostName`, `Domain` (the domain, from `NetGetJoinInformation`), `DomainRole`, the key `Name`; the methods `Rename`, `JoinDomainOrWorkgroup` and `UnjoinDomainOrWorkgroup`, which call `NetRenameMachineInDomain`, `NetJoinDomain` and `NetUnjoinDomain` (so, the join engine, docs/DOMAIN_JOIN.md). The method answers the status of that function, as Windows does. |
| `WexComputer` module | `src/powershell/WexComputer` | `Add-Computer`, `Remove-Computer`, `Test-ComputerSecureChannel` and `Reset-ComputerMachinePassword`: PowerShell 7 left them in Windows PowerShell 5.1 (needs .NET Framework). They call the methods above (and `nltest.exe` for the secure channel). `Rename-Computer` is PowerShell 7's own and works through the same methods. |

## What works, what does not

- Tried in the dev build (`/root/WE-prefetch`, WoW64) with a throwaway prefix: `Get-CimInstance Win32_ComputerSystem`,
  `Win32_OperatingSystem`, a WQL query, `New-CimSession` + `QueryInstances`, `Invoke-CimMethod ... Rename`,
  `Rename-Computer` (returns without error), the four `WexComputer` cmdlets answering the engine's status (50, "request not
  supported", where no engine runs).
- **Real join in the AD lab (2026-11-22, `installer/tests/ad/join-ps-guest.sh`, 29 checks pass):** PowerShell 7.7.0 from
  the release zip (digest checked), the module found without `Import-Module` (it sits in `$PSHOME\Modules`), then
  `Add-Computer` with `-OUPath` (account in the OU, `net ads testjoin`, winbind trust), a second `Add-Computer` refused,
  `Test-ComputerSecureChannel` True, `Reset-ComputerMachinePassword` and the channel still True, `Rename-Computer`
  in the domain (host name and account renamed), the way back, `Remove-Computer`, and the machine left. No password in
  the journal. Run it with `WEX_GUEST=join-ps-guest.sh WEX_LAB_EXTRA=DIR machine.sh` (DIR: the dlls of the build under
  test, the module, `join-ps.ps1`).
- Seen in that run: a running Wine session keeps the computer name it started with (its `ActiveComputerName` is
  made when its server starts), so `Win32_ComputerSystem.Name` and `Rename-Computer`'s "same name" check show the old
  name after a rename in the same session, as Windows does until a restart. The host name, the directory and the
  machine registry (`wex-domain-state.sh` writes `ComputerName` and `ActiveComputerName` after a join, rename or leave)
  have the new name; a session started later sees it. `Win32_ComputerSystem.Domain` is the DNS name of the domain,
  read from `HKLM\Software\WineExplorer\Domain\Name` (the NetBIOS name when that is missing). An image older than
  `mi.dll` needs the dll in each prefix's system32/syswow64 (the lab script does it).
- The 32-bit PowerShell (`PowerShell-7.7.0-win-x86.zip`) does not start in this build: even `1+1` fails loading
  `System.Runtime.dll` ("Module not found") before any CIM code runs. Not looked into; the component is the x64 one.
- Outside a domain, `Rename` goes through the host name service (`wexhost.dll`, `WexHostnameSet`, loaded by name by
  `wbemprox`, as System Properties does), so the host name changes at once; polkit decides, and asks the desktop's
  authentication agent (a dialog) when the caller has no standing permission. Without `wexhost.dll` the stock
  `SetComputerNameEx` is used (effective after a restart). In a domain the engine renames at once. Lab: stage
  `wgrename` of `join-ps-guest.sh` (a polkit rule of the lab stands in for the dialog).
- The join of a workgroup is not offered (`ERROR_NOT_SUPPORTED`), as in System Properties; `-WorkgroupName` says so.
- `-Restart` is accepted and does nothing: no restart is needed.
- Only the entries of the MI tables that PowerShell calls are implemented (`src/dlls/mi/implemented.txt`); the others are
  tracing stubs (`MI_TRACE=<file>` writes what is called). Implemented: enumerate, query, invoke, `Get-CimClass -ClassName`
  (class, properties, methods; no parent class, `wbemprox` has none), `Get-CimInstance -InputObject` (refresh), and the
  writes `New-`, `Set-` and `Remove-CimInstance`, which pass the call to WMI (`PutInstance`, `DeleteInstance`) and report
  what `wbemprox` answers (it is read-only for its classes, so they fail with `FAILED` or `NOT_FOUND`). Not done:
  wildcard `Get-CimClass` (`EnumerateClasses`), associations (`Get-CimAssociatedInstance`), indications
  (`Register-CimIndicationEvent`).
- Only the x64 PowerShell runs (see above). The i386 `mi.dll` builds; its calling convention is unchecked (OMI's
  `MI.h` has `MI_CALL` empty).

## Running it

PowerShell needs a console: with plain pipes `ConsoleHost.Start` fails with a null reference and the command runs
nothing (exit code 0). Run it on a pseudo-terminal, as `script -qec "wine pwsh.exe -File x.ps1" /dev/null` does, or in a
console window of the desktop. `Import-Module <dir>\WexComputer\WexComputer.psd1` loads the four cmdlets.

## Sources and licences

`src/dlls/mi/MI.h` is OMI's (Microsoft, MIT, `LICENSE-omi` beside it); the function tables (`mi_tables.c`) are made from it
by `gen_tables.py`, and `implemented.txt` says which entries are real. The rest of `mi.dll` is GPL-2.0-or-later. The
`wbemprox` changes are LGPL-2.1-or-later like the rest of that module (docs/STOCK_CHANGES.md).
