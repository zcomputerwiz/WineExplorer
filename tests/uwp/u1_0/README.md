<!-- SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors -->
# U1.0 Windows oracle fixture

Static draft: **not built, packaged, installed, launched, or validated**. No
HRESULT in `observations.txt` has been observed. This is a full-trust packaged
desktop console probe, not a UWP lifecycle or AppContainer test.

The host inventory records VS 2022 Build Tools 17.14 with v143 and SDK
10.0.26100.0, including C++/WinRT headers, MakeAppx and SignTool. The scripts
use those paths and the desktop CRT; the UWP workload and NuGet are not required
by design. `build.ps1` prints its exact `vcvarsall` and `cl` commands, uses
`/MT`, and builds x64 or x86. Export naming, link dependencies, manifest schema
and actual compatibility with this toolchain remain unverified.

## Execution protocol

All PowerShell scripts default to printing their actions. `-Execute` is
required for every action. Choose a unique valid package name and a new
`u1oracle-*` directory beneath the current user's temporary directory. Use a
results directory outside that work directory. Run each architecture separately.
Example, first without `-Execute` to review each action:

```powershell
$w = Join-Path $env:TEMP 'u1oracle-example'
$r = 'D:\oracle-results\u1-example'
.\build.ps1 -Architecture x64 -Execute
.\package.ps1 -Name WineExplorer.U1Oracle.Example -Publisher 'CN=WineExplorer U1 Oracle' -Version 1.0.0.0 -Architecture x64 -WorkDirectory $w -Execute
# Arrange trust for $w\oracle.cer externally before the following command.
.\run.ps1 -WorkDirectory $w -ResultsDirectory $r -Execute
```

Run from this directory. The packaging script creates a fresh short-lived
signing certificate in `CurrentUser\My` and exports its public certificate into
the temporary work directory; Windows' key provider holds the private key.
The scripts do not establish trust, change Developer Mode or policies, or
modify a trust store. A user must arrange and later remove any trust separately;
sideloading must already be permitted. The package publisher must match the
certificate subject. The scripts refuse a pre-existing package with the selected
name and a pre-existing work or results directory. Avoid concurrent runs with
the same name; these checks are not an interprocess lock.

`run.ps1` installs each signed variant with `Add-AppxPackage`, obtains its
package full and family names, activates `<family>!Probe` with
`IApplicationActivationManager::ActivateApplication`, waits for the returned
process and reads the probe's explicitly named output file. This route requests
package activation rather than executing the installed EXE. The probe checks its
current and explicit-process identity against the installed full name, and its
token identity against the process identity. The runner removes each identified
package with `Remove-AppxPackage` in a finally block. It then runs the built EXE
directly only for the explicitly unpackaged negative control. The launcher has a
60-second wait and reports failure if it cannot wait or obtain the exit status;
it does not terminate a timed-out process. Activation, output-file access,
process lifetime and cleanup behavior require real verification.

After the run, the runner invokes `cleanup.ps1 -Execute` to remove its own
certificate from `CurrentUser\My` and its temporary package files. If preparing
or trusting fails before the runner starts, run `cleanup.ps1 -WorkDirectory $w
-Execute` manually. Cleanup refuses to proceed while a package of that name
is present. A failed deployment, timeout, interrupted shell or failed removal
may require manual inspection and cleanup. Never remove another package or
certificate. Any separately installed trust remains the user's responsibility.
Certificate-provider private-key deletion is requested with `-DeleteKey` and
has not been verified on this host.

## Checks and evidence

`expected.txt` specifies the ordered positive and negative check lines. The
runner compares `actual.txt` against it and fails on missing, extra or changed
checks; raw output and observed HRESULTs go to the new results directory. Each
negative is a separate package or process: manifest entry omitted, manifest
entry present but DLL omitted, and no package identity. Each records the actual
`RoGetActivationFactory` HRESULT even if it unexpectedly succeeds. Passing
currently means the call failed in the intended identity context; the precise
failure values are **TO BE OBSERVED**, not assumed. Any unexpected success,
including global registration of the class, fails the negative. A failure is
not by itself proof of its cause; inspect the raw evidence and deployment.

The positive checks MTA initialization, identity consistency and requested
package full name, nonempty package path, activation and runtime class name,
and a completed `StorageFolder.GetFolderFromPathAsync` on that path. The
`factory_module` check resolves the module containing the factory's vtable
method and compares its path to `component.dll` under the package path. Together
with the missing-entry control this is designed to identify package-provided
activation without global registration; proxy behavior and path spelling are
unverified and a failure must not be weakened to obtain a pass. The async check
does not cover cancellation, callback ordering or lifecycle.

Keep oracle results outside this repository. Record OS build, SDK and MSVC
version, architecture, package identities and SHA-256, dependency versions,
locale, account, activation route, trust setup, commands, output and exit
status. For Wine also record exact source revisions, build, prefix and gate.
Compare the same package bits and label all unrun and unverified claims.

## MinGW build and Wine run

`build-mingw.sh` cross-compiles `probe.exe` and `component.dll` with the MinGW
compilers (`x86_64-w64-mingw32-g++` and `i686-w64-mingw32-g++`) for both x64
and x86, into `build/mingw-x64/` and `build/mingw-x86/` (gitignored like
`build/`). It uses `-O2 -Wall -municode` with static libstdc++/libgcc so no
MinGW runtime DLL is needed, and defines `INITGUID` so the WinRT IIDs are
defined (MinGW ships no library that provides them; the MSVC build links
`uuid.lib` and is unchanged). The C++/WinRT projection headers and the
`async_folder` check are MSVC-only (`#ifdef _MSC_VER`), because MinGW does not
ship the C++/WinRT headers; the async check is a positive-case check and is not
reached by the no-package Wine cases.

`run-wine.sh <wine build dir> <prefix>` runs only the cases that need no package
installation on Wine — the unpackaged direct run, which also covers the identity
queries of an unpackaged process and the missing-class activation failure — with
that build's `wine` loader in the given prefix, after setting
`HKCU\Software\WineExplorer\Shell\UwpCompatibility` to 1 in that prefix. It
writes `actual-wine-<arch>.txt` per architecture, prints a comparison with the
Windows oracle lines for the same cases (`expected.txt`, `ORACLE_RESULTS.md`
HRESULTs), and marks every package-activation case `NOT-RUN (no package
activation in Wine yet)`. It refuses to run if the prefix is the wexbuild test
fixture or `/etc/wineexplorer` is involved, and it never starts or kills a
wineserver it did not start.
