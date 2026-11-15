<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# UWP application frameworks

The `uwp-frameworks` component of `scripts/wex-components.py` installs Microsoft's framework packages for every user:
the five that the Windows Calculator 11.2607 declares (`Microsoft.UI.Xaml.2.8` 8.2501.31001.0,
`Microsoft.NET.Native.Framework.2.2` 2.2.29512.0, `Microsoft.NET.Native.Runtime.2.2` 2.2.28604.0,
`Microsoft.VCLibs.140.00` 14.0.33519.0, `Microsoft.VCLibs.140.00.UWPDesktop` 14.0.33728.0, x64).

* **Sources.** Microsoft's own versioned downloads, each container and each package in it pinned by size and SHA-256:
  nuget.org (`Microsoft.UI.Xaml` 2.8.7, `runtime.win10-x64.Microsoft.Net.Native.SharedLibrary` 2.2.8-rel-29512-01,
  `runtime.win10-x64.Microsoft.Net.Native.Compiler` 2.2.12-rel-33220-00) and Visual Studio 2022's payload catalog
  (`Microsoft.VisualCpp.CRT.Appx.Msi` and `...Appx.Desktop.140.Msi`, 14.44.35208, `cab1.cab`). The Desktop VCLibs behind
  Microsoft's aka.ms link is 14.0.33321.0, below the Calculator's minimum, so the Visual Studio one is used. About 238
  MiB are downloaded for about 20 MiB of packages. Cabinets are opened with `cabextract` (a dependency of the
  WineExplorer Wine package already).
* **Install.** Each package's identity (name, version, architecture, publisher, `Framework`) is checked against its
  pin; its files go to `C:\Program Files\WindowsApps\<PackageFullName>` and a record to the machine's catalog,
  `C:\ProgramData\WineExplorer\appx-catalog` (the wexappx format, `framework 1`). Wine's kernelbase (eba27020) reads
  dependencies there after the user's own catalog; a machine record counts only as a framework. Receipt, full
  verification, upgrade (versions no longer pinned are removed), rollback (a failure leaves nothing and no receipt) and
  uninstall as for `uwp-xaml`. Tests: `scripts/test_component_uwp_frameworks.py` (synthetic packages, a zip and a
  cabinet container, run as root).
* **Terms** (owner, 2026-11-14): setup shows three Microsoft licence documents, one after the other under their
  titles, each pinned by its bytes (`terms_documents`) and the whole text by its hash: the Windows UI Library licence
  (`license.txt` inside the pinned WinUI 2.8.7 package, read with HTTP range requests, about 70 KiB of the 20 MiB), the
  .NET Native for UWP licence (`releases/UWP/LICENSE.TXT` of Microsoft's `dotnet` repository), and the Visual C++
  runtime licence (Microsoft's `Visual-C-Runtime-2015-2022-License` document, also shown by `vcrun2022`; VCLibs is that
  runtime for UWP). The Visual Studio licence page was not usable: it loads its text with JavaScript.

## Lab test

`run-wine-frameworks.sh WINE_BUILD U1_0_DIR FRAMEWORKS_DIR COMPONENTS_PY DOWNLOADS RESULTS` (x64): `fwprobe.exe`
(`fwprobe.c`, MinGW: `x86_64-w64-mingw32-gcc -municode`) in a package (`AppxManifest.xml`) that depends on the five
frameworks with the Calculator's minimum versions. The package is refused while they are missing; `lab-frameworks.py`
runs the component's install steps against the prefix; the package installs, is activated through the U1.0 launcher,
and its checks are compared with `expected.txt` (what Windows does; not measured there yet, the package would have to
be signed); then everything is removed.

2026-11-14, Wine eba27020 (owned U1 guest): install, resolution and removal pass. The program resolves all five
dependencies at their `WindowsApps` paths and loads `Microsoft.UI.Xaml.dll`, `vcruntime140_app.dll` and `mfc140u.dll`
from their frameworks. `mrt100_app.dll` and `SharedLibrary.dll` (.NET Native) do not initialise, two FAIL lines:

1. `mrt100_app.dll` delay-loads `GetManagedRuntimeService(REFGUID, void **)` from `mrt100.dll`, a Windows system DLL
   that Wine does not have. Microsoft's .NET Native documentation calls it optional (performance only); the runtime
   passes `ResolveDelayLoadedAPI` its own failure hook and, as system hook, a thunk to `kernelbase!DelayLoadFailureHook`,
   and falls back to built-in code when the delay load fails the Windows way.
2. Wine failed that three ways, each checked on Windows 11 (x64 and x86) with our own tests: the failure hook got
   `LOWORD` of the name's RVA instead of the name; a dll hook returning NULL ended the resolution instead of asking the
   system hook (kernel32 `test_ResolveDelayLoadedAPI` addition); and `DelayLoadFailureHook` raised `EXCEPTION_WINE_STUB`
   where Windows raises `ERROR_DELAY_LOAD_FAILED` (0x505, non-continuable, parameters the DLL and function names; a
   probe of ours calling it under `__try`). General Wine bugs, Session 0's B14.

With the three fixes in the lab build (uncommitted here; B14 lands them on Wine `development`), all 10 checks pass: the
.NET Native runtime initialises from its framework packages with no `mrt100.dll`.

## Prior art (2026-11-14)

* `designgears/wine-uwp` (no licence; says some findings came from disassembly): its findings name the same delay-load
  gap and list the Calculator's next Wine gaps (`RtlQueryPackageClaims`, `CheckTokenMembershipEx`, the
  `GetCurrentPackage*` family, `AppPolicyGetWindowingModel`, token security attributes `WIN://SYSAPPID`,
  `GetCurrentPackageInfo2`, `CoGetSystemSecurityPermissions`). Used only as claims to verify with our own Windows
  tests; none of its code or disassembly-derived detail is used. One claim was wrong in detail: the exception's first
  parameter is the DLL name, not a `DelayLoadInfo`.
* `xodus` forks (bartman081523): claim the Calculator running on patched Proton with a native XAML; the Xodus
  maintainers objected to AI-generated and reverse-engineered code there. Not used.
* `ZhymabekRoman/wine-ultimate`: Wine Staging patches for WinUI 3, Windows App SDK, DirectComposition and WebView2;
  not reviewed yet.
