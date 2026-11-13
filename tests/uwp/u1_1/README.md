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

## U1.1b presentation and input probe (Windows, started)

Application `ViewPresent` (mode `present`), run by `run-present.ps1` (it takes a screenshot at READY and moves and
types only into a window it has checked; it needs an unlocked desktop that nobody uses for about 40 seconds).

2026-11-12, x64, unlocked desktop (output in `..\WineExplorer-local`, not copied):

* `window.Activate()` **does** show the window when the desktop is unlocked: `Visible=1` after about 80 ms, then
  `WindowActivated` (CodeActivated) and `InputEnabled=1` at about 140 ms. No `SizeChanged`. `Bounds` stay 1200x900 and
  the window opens in a cascading position (the origin moved between runs). The earlier "Activate raises nothing, the
  window stays invisible" measurements (the `window_stays_invisible_after_activate` and `activate_raises_no_window_events`
  checks) were made with a locked desktop and have to be reread: **with the desktop locked** `Visible` stays 0 and
  `Suspending` is raised after about 5 s (confirmed again by this probe's first run).
* `D3D11CreateDevice` and `IDXGIFactory2::CreateSwapChainForCoreWindow` succeed: 1200x900, B8G8R8A8 (87), 2 buffers,
  flip-sequential, `DXGI_SCALING_NONE`; five `Present(1, 0)` calls return `S_OK` at about 110 ms intervals.
* Not measured yet: pointer, keyboard and character events. The frame opens behind the foreground window (the
  activation is not allowed to take the foreground), the injected click did not reach it, so the runner refuses to
  inject. Next: bring the frame to the front (or have the owner click its taskbar button) before injecting.

## Not asserted (observed, varies or races on Windows)

`PreviousExecutionState` (0 and 3 seen), anything after `CoreApplication.Exit` or after `Run` returns (the process may
end at any point), whether `ProcessOneAndAllPending` waits with nothing queued, the exit code of the ended process,
`Bounds` (1200x900 at 0,33 on Windows). Not done: presentation and input, suspend and resume (U1.1b, U1.1c).
