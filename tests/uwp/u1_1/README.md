<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1.1 UWP view lifecycle probe

`view.cpp` is a native UWP application (C++/WinRT, MSVC, `/APPCONTAINER`, `EntryPoint="view.App"`) that calls
`CoreApplication.Run` with an `IFrameworkViewSource` of its own and checks, from inside the callbacks, what
`docs/uwp/DESIGN_LIFECYCLE.md` describes. The **same unchanged binaries** run on Windows 11 and on Wine.

A UWP launch passes no command line: the activated application id selects the mode and the output goes to
`%LOCALAPPDATA%\view-MODE.txt` (the package's own folder inside the AppContainer on Windows).

| Application id | mode | |
|---|---|---|
| `View` | `positive` | multithreaded apartment, ends with `CoreApplication.Exit` |
| `ViewReturn` | `return` | multithreaded apartment, returns from `Run` |
| `ViewSta` | `sta` | single-threaded apartment: `Run` fails |

`view.exe unpackaged OUT X` is a control for Wine only (Windows ends such a process with a fast fail).

* `build.ps1 [-Architecture x64|x86] -Execute` builds `build\<arch>\view.exe` (`vswhere` must be on `PATH`:
  `C:\Program Files (x86)\Microsoft Visual Studio\Installer`).
* `package.ps1` builds and signs one package with a two-day certificate (a new one, or `-CertificateThumbprint` of
  one it made before); `run.ps1` installs it, activates the three applications through the U1.0 `launcher.exe`,
  compares the output with `expected-MODE.txt` (PASS and FAIL lines, sorted) and removes the package. Trusting the
  certificate is a separate step the owner takes: **machine** TrustedPeople, an elevated `Import-Certificate`.
* `run-wine-lifecycle.sh WINE_BUILD U1_0_DIR U1_1_DIR RESULTS_DIR`: fresh prefix per architecture, development
  registration (`../u1_0/register-wine.py`), the same activations, the same comparison.
* `expected-*.txt` are what Windows 11 produced.

## Evidence

2026-11-12. Windows 11 Pro build 26300 (x64 host, x86 package under WoW64), MSVC 19.44, SDK 10.0.26100.0, signed with
a throwaway certificate trusted by the owner in TrustedPeople for the run (to be removed afterwards).

* **Windows**: three runs each on x64 and x86: `positive` 34 checks, `return` 32, `sta` 3 all match. Findings are in
  `DESIGN_LIFECYCLE.md`; the full TRACE and INFO output of the exploratory runs is in
  `..\WineExplorer-local` *(not copied here)*.
* **Wine** (candidate `u11-2` of `codex/uwp-runtime`, owned U1 guest, fresh prefix per architecture): `positive` 34,
  `return` 32, `sta` 3 and the unpackaged control all pass on x64 and x86 with the same `view.exe` bits.
  The U1.0 fixture (all checks, 14 server refusals), the 61 + 59 install checks (including installing the UWP package
  with `wexappx` and activating it), windows.ui (12, 82), twinapi (73), windows.storage (599) and kernel32 version
  (541) pass on both architectures; `rpcrt4_test server` has only its two known failures.

## Not asserted (observed, varies or races on Windows)

`PreviousExecutionState` (0 and 3 seen), anything after `CoreApplication.Exit` or after `Run` returns (the process may
end at any point), whether `ProcessOneAndAllPending` waits with nothing queued, the exit code of the ended process,
`Bounds` (1200x900 at 0,33 on Windows). Not done: presentation and input, suspend and resume (U1.1b, U1.1c).
