<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1.1 view lifecycle probe

`view.cpp` is a packaged full-trust program that calls `CoreApplication.Run` with an `IFrameworkViewSource` of its
own (C++/WinRT, MSVC) and checks, from inside the callbacks, the contract described in
`docs/uwp/DESIGN_LIFECYCLE.md`: callback order and thread, launch activation, window events raised by event
processing, `CoreDispatcher` priorities and `ProcessEvents` options, `RunAsync` from another thread, cancellation,
idle callbacks and window close. `positive` is started by package activation; `unpackaged` is the control.

```
view.exe positive|unpackaged OUTPUT_FILE EXPECTED_PACKAGE_FULL_NAME
```

* `build.ps1 [-Architecture x64|x86] -Execute` builds `build\<arch>\view.exe` (needs `vswhere` on `PATH`:
  `C:\Program Files (x86)\Microsoft Visual Studio\Installer`).
* `AppxManifest.xml` is the package manifest (application `View`, full trust).
* `expected.txt` / `expected-unpackaged.txt`: the PASS lines, in order. `INFO` lines are observations, not compared.
* `run-wine-lifecycle.sh WINE_BUILD U1_0_DIR U1_1_DIR RESULTS_DIR`: fresh prefix per architecture, development
  registration (`../u1_0/register-wine.py`), activation through the U1.0 `launcher.exe`, unregistration.

## Evidence

2026-11-12, owned U1 guest, Wine candidate `u11-1` (`codex/uwp-runtime` plus the U1.1 sources), MSVC 19.44 builds
of 2026-11-12 (`view.exe` SHA-256 x64 `2D5981B5...CCCE6`, x86 `D85F89DA...832C6E`):

* packaged run, x64 and x86: all 35 checks of `expected.txt` pass; `CoreApplication.Run` returns `S_OK`.
* unpackaged control, x64 and x86: `Run` fails with `0x80073D54` (`APPMODEL_ERROR_NO_PACKAGE`), no callback runs.
* recorded sequence (identical on both): `CreateView | Initialize | SetWindow | Load | Run | ViewActivated |
  Kind=Launch | Previous=NotRunning | Arguments=... | Visible=1 | WindowActivated | Visible=0 | Closed |
  RunReturning | Uninitialize`.

## Not done

**No Windows run.** The expected values are the documented contract, not measurements. Running the packaged probe on
Windows 11 needs a signed package and the owner's trust of its throwaway certificate (or Developer Mode), as for
U1.0 (`../u1_0/ORACLE_RESULTS.md`); neither is set up on this machine, and whether a full-trust packaged process may
call `CoreApplication.Run` at all is unknown. Until it is measured, a Wine pass here shows that the implementation
matches the documentation, not that it matches Windows. The assumptions are listed in `DESIGN_LIFECYCLE.md`.
