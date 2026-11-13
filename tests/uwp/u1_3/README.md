<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1.3 package dependencies

A framework package (`WineExplorer.U1Framework`, a DLL `u1fw.dll`, `<Framework>true</Framework>`) and a package that
depends on it (`WineExplorer.U1Dependent`, a full-trust program `depapp.exe`, `PackageDependency` with a minimum
version and the publisher). The program loads `u1fw.dll` by name, calls it, and asks for its package graph with
`GetCurrentPackageInfo(PACKAGE_FILTER_DIRECT)`.

* `build.ps1 [-Architecture x64|x86] -Execute` builds `build\<arch>\u1fw.dll` and `depapp.exe`.
* `package.ps1 -WorkDirectory <new u1oracle-*> -CertificateThumbprint <thumb> -Execute` builds and signs
  `framework.appx` and `app.appx`; `run.ps1 -WorkDirectory ... -ResultsDirectory ... -Execute` installs, activates through
  the U1.0 launcher, compares with `expected.txt` and removes them (the certificate has to be trusted by the owner).
* `run-wine-deps.sh WINE_BUILD U1_0_DIR U1_3_DIR RESULTS_DIR`: the same on Wine with `wexappx`, fresh prefix per
  architecture, plus the refusals.

## Evidence

2026-11-13, Windows 11 build 26300 (x64): the dependent package is refused without its framework (0x80073CF3,
"Package failed updates, dependency or conflict validation"); with it, `IsFramework` is true for the framework, the
dependent package lists it as a dependency, the program loads `u1fw.dll` by name from
`C:\Program Files\WindowsApps\<framework full name>` and the 8 checks pass. Windows lets the framework be removed
while the dependent package is installed (not modelled: Wine does the same).

Wine (`codex/uwp-runtime`, owned U1 guest, fresh prefix per architecture, x64 and x86): the same 8 checks pass; the
dependent package is refused without the framework and with a framework below `MinVersion`; both packages uninstall and
leave nothing. How: `wexappx` resolves each `PackageDependency` against the catalog (name, publisher, architecture,
minimum version, highest wins) and writes `dependency` lines into the record; the activation manager puts the folders
of the direct dependencies (`GetPackageInfo(PACKAGE_FILTER_DIRECT)`) in front of `PATH` of the started process.
