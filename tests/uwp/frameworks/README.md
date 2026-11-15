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

1. `mrt100_app.dll` delay-loads `GetManagedRuntimeService` from `mrt100.dll`, a Windows system DLL (System32,
   one export) that Wine does not have. Its contract is not documented anywhere public; a stub that fails makes the
   runtime refuse to start (error 1114). Next: observe the function black-box from a test program on Windows, then a
   Wine `mrt100.dll`.
2. With the DLL missing, Wine's `LdrResolveDelayLoadedAPI` passes the failure hook `LOWORD` of the name's RVA as the
   imported name, and the hook crashes (error 998). General Wine bug, given to Session 0 with a patch.
