<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1.0 Windows Oracle Fixture Build Check Record

**Date**: 2026-11-07  
**Host Environment**: Windows 11 Pro 26H2 (Build 10.0.26300.9818 AMD64)  
**Status**: Both `x64` and `x86` targets compiled and linked successfully.

---

## 1. Toolset & SDK Versions Used

* **Visual Studio**: Visual Studio Build Tools 2022 (Version `17.14.37614.0`, Toolset `v143`)
* **MSVC Compiler**: `Microsoft (R) C/C++ Optimizing Compiler Version 19.44.35228 for x64` and `Version 19.44.35228 for x86` (`cl.exe`)
* **MSVC Linker**: `Microsoft (R) Incremental Linker Version 14.44.35228.0` (`link.exe`)
* **Windows SDK**: `10.0.26100.0`
  * System include root: `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0` (`shared`, `ucrt`, `um`, `winrt`)
  * C++/WinRT projection headers: `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt`
  * Library root: `C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\{x64,x86}`
* **Developer Environment Setup Script**: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1`
* **VC Toolset Environment Batch Script**: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat`

---

## 2. Exact Commands Executed

All outputs were redirected outside this repository to throwaway directories beneath `D:\DeepThought\ChromiumLab\uwp-u10-build`.

### 2.1 x64 Build Command

Executed in a process-scoped PowerShell session:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force; & "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64; & .\tests\uwp\u1_0\build.ps1 -Architecture x64 -OutDirectory D:\DeepThought\ChromiumLab\uwp-u10-build\x64 -Execute
```

Inner commands executed by `build.ps1`:
1. `call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 10.0.26100.0`
2. `cl /nologo /std:c++17 /EHsc /MT /LD "D:\DeepThought\Projects\WineExplorer\tests\uwp\u1_0\component.cpp" /link /DEF:"D:\DeepThought\Projects\WineExplorer\tests\uwp\u1_0\component.def" /OUT:component.dll runtimeobject.lib ole32.lib uuid.lib`
3. `cl /nologo /std:c++17 /EHsc /MT /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt" "D:\DeepThought\Projects\WineExplorer\tests\uwp\u1_0\probe.cpp" /link /OUT:probe.exe runtimeobject.lib windowsapp.lib ole32.lib advapi32.lib uuid.lib`
4. `cl /nologo /std:c++17 /EHsc /MT "D:\DeepThought\Projects\WineExplorer\tests\uwp\u1_0\launcher.cpp" /link /OUT:launcher.exe ole32.lib uuid.lib`

Result: Exit code `0`. Produced in `D:\DeepThought\ChromiumLab\uwp-u10-build\x64`:
* `component.dll`, `component.exp`, `component.lib`, `component.obj`
* `probe.exe`, `probe.obj`
* `launcher.exe`, `launcher.obj`

### 2.2 x86 Build Command

Executed in a process-scoped PowerShell session:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force; & "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1" -Arch x86 -HostArch amd64; & .\tests\uwp\u1_0\build.ps1 -Architecture x86 -OutDirectory D:\DeepThought\ChromiumLab\uwp-u10-build\x86 -Execute
```

Inner commands executed by `build.ps1`:
1. `call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86 10.0.26100.0`
2. `cl /nologo /std:c++17 /EHsc /MT /LD "D:\DeepThought\Projects\WineExplorer\tests\uwp\u1_0\component.cpp" /link /DEF:"D:\DeepThought\Projects\WineExplorer\tests\uwp\u1_0\component.def" /OUT:component.dll runtimeobject.lib ole32.lib uuid.lib`
3. `cl /nologo /std:c++17 /EHsc /MT /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt" "D:\DeepThought\Projects\WineExplorer\tests\uwp\u1_0\probe.cpp" /link /OUT:probe.exe runtimeobject.lib windowsapp.lib ole32.lib advapi32.lib uuid.lib`
4. `cl /nologo /std:c++17 /EHsc /MT "D:\DeepThought\Projects\WineExplorer\tests\uwp\u1_0\launcher.cpp" /link /OUT:launcher.exe ole32.lib uuid.lib`

Result: Exit code `0`. Produced in `D:\DeepThought\ChromiumLab\uwp-u10-build\x86`:
* `component.dll`, `component.exp`, `component.lib`, `component.obj`
* `probe.exe`, `probe.obj`
* `launcher.exe`, `launcher.obj`

---

## 3. Warnings and Errors Encountered

### 3.1 Initial Script Invocation Error in `build.ps1`
* **Error**: `The filename, directory name, or volume label syntax is incorrect.` / `Build failed: 1`
* **Cause**: `& cmd.exe /d /s /c "`"$line`""` caused PowerShell to escape the nested quotes (`"\"call ...\""`), prompting cmd.exe to fail to locate the command.
* **Fix**: Replaced `& cmd.exe /d /s /c "`"$line`""` with `& cmd.exe /d /c $line`.

### 3.2 Compiler Errors in `component.cpp`
* **Errors**:
  * `component.cpp(64): error C3861: 'WindowsCreateString': identifier not found`
  * `component.cpp(109): error C3861: 'WindowsCreateString': identifier not found`
  * `component.cpp(131): error C3861: 'WindowsGetStringRawBuffer': identifier not found`
  * `component.cpp(138): error C2375: 'DllCanUnloadNow': redefinition; different linkage` (declared in `combaseapi.h` as `STDAPI DllCanUnloadNow(void);` without dllexport).
* **Fix**: Added `#include <winstring.h>` and declared `DllCanUnloadNow` as `extern "C" HRESULT WINAPI DllCanUnloadNow(void)` matching the SDK declaration. Exports are managed via `component.def`.

### 3.3 Compiler Errors in `probe.cpp`
* **Errors**:
  * `probe.cpp(64): error C3861: 'WindowsCreateString': identifier not found`
  * `probe.cpp(75): error C3861: 'WindowsDeleteString': identifier not found`
  * `probe.cpp(102): error C3861: 'WindowsCreateString': identifier not found`
  * `probe.cpp(123): error C3861: 'WindowsGetStringRawBuffer': identifier not found`
  * `probe.cpp(126): error C3861: 'WindowsDeleteString': identifier not found`
  * `probe.cpp(129): error C3861: 'WindowsDeleteString': identifier not found`
  * `probe.cpp(136): error C2872: 'Windows': ambiguous symbol` (`roapi.h` ABI namespace `Windows` vs C++/WinRT namespace `winrt::Windows`).
  * Consequential errors: `C2039: 'StorageFolder': is not a member of 'Windows'`, `C3861: 'GetFolderFromPathAsync': identifier not found`, `C2872: 'Windows': ambiguous symbol`, `C2065: 'Completed': undeclared identifier`.
* **Fix**: Added `#include <winstring.h>` and explicitly qualified the C++/WinRT types as `winrt::Windows::Storage::StorageFolder` and `winrt::Windows::Foundation::AsyncStatus::Completed`.

### 3.4 Linker Warnings
* **Warning** (both `x64` and `x86`):  
  `component.def : warning LNK4104: export of symbol 'DllCanUnloadNow' should be PRIVATE`  
  This is a standard MSVC linker diagnostic for COM/WinRT entry point exports in `.def` files; linking completed successfully with exit code `0`.

---

## 4. What Was Changed

All edits were strictly minimal and retained original GPL-2.0-or-later license headers without copying Microsoft sample code:

1. [`tests/uwp/u1_0/build.ps1`](file:///D:/DeepThought/Projects/WineExplorer/tests/uwp/u1_0/build.ps1):
   - Added optional `[string]$OutDirectory` parameter allowing output redirection outside the repository (defaulting to `$PSScriptRoot\build\$Architecture` when omitted).
   - Replaced `& cmd.exe /d /s /c "`"$line`""` with `& cmd.exe /d /c $line`.
2. [`tests/uwp/u1_0/component.cpp`](file:///D:/DeepThought/Projects/WineExplorer/tests/uwp/u1_0/component.cpp):
   - Added `#include <winstring.h>`.
   - Changed definition of `DllCanUnloadNow` to `extern "C" HRESULT WINAPI DllCanUnloadNow(void)` matching `combaseapi.h`.
   - Cleaned up `DllGetActivationFactory` signature to `extern "C" HRESULT WINAPI DllGetActivationFactory(...)`.
3. [`tests/uwp/u1_0/probe.cpp`](file:///D:/DeepThought/Projects/WineExplorer/tests/uwp/u1_0/probe.cpp):
   - Added `#include <winstring.h>`.
   - Explicitly qualified `winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync` and `winrt::Windows::Foundation::AsyncStatus::Completed`.

---

## 5. Non-Execution & Non-Installation Certification

In compliance with test governance requirements:
* `package.ps1`, `run.ps1`, and `cleanup.ps1` were **NOT** executed.
* No certificates were generated, imported, or trusted in any certificate store.
* No packages were packaged, signed, or installed.
* Developer Mode and all system/registry/policy settings remained completely untouched.
* The built binaries (`probe.exe`, `launcher.exe`, `component.dll`) were **NOT** executed.
* All generated binaries remain isolated in the throwaway directory `D:\DeepThought\ChromiumLab\uwp-u10-build`.
