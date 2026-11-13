<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1.1: view lifecycle (CoreApplication, CoreWindow, CoreDispatcher)

First slice of U1.1, on Wine branch `codex/uwp-runtime` (2026-11-12): the lifecycle of a native `IFrameworkView`
application, **without presentation or input**. It builds on the package identity of `DESIGN_PACKAGE_CONTEXT.md`.
Evidence is in `tests/uwp/u1_1/README.md`; what is not claimed is listed at the end and is part of the design.

## What runs

`CoreApplication.Run(IFrameworkViewSource)` in a process that has a package context (`twinapi.appcore`):

1. `CreateView` on the source.
2. A `CoreWindow` and its `CoreDispatcher` are created for the **calling thread** (`windows.ui`, private exports
   `WineUICreateCoreWindow` / `WineUIDetachCoreWindow`, found with `GetProcAddress`); a `CoreApplicationView` wraps
   them (`IsMain`, `CoreWindow`, `Dispatcher`, `Activated`).
3. The launch activation is queued on the dispatcher: `LaunchActivatedEventArgs` (`Kind` Launch,
   `PreviousExecutionState` NotRunning, `Arguments` the command line after the program name, `TileId` the
   application id).
4. `Initialize(view)`, `SetWindow(window)`, `Load("")`, `Run()`, then `Uninitialize()`, all on the calling thread.
   `Run` returns the first failure; a view that fails in a step still gets `Uninitialize`.
5. The window is detached: `CoreWindow.GetForCurrentThread` no longer finds it.

A process without a package context gets `HRESULT_FROM_WIN32(APPMODEL_ERROR_NO_PACKAGE)` and no callback runs, so
programs that are not applications behave as before. A second `Run` while one is running gets
`E_ILLEGAL_METHOD_CALL`.

## CoreDispatcher (`windows.ui/dispatcher.c`)

One per window, bound to the window's thread.

* `HasThreadAccess` compares thread ids. `ProcessEvents` from another thread: `RPC_E_WRONG_THREAD`.
* `RunAsync(priority, handler)` may be called from any thread. Items run on the dispatcher thread, the highest
  priority first (High, Normal, Low, Idle), first in first out within a priority. The returned `IAsyncAction`
  completes after the handler ran; its `Completed` handler runs on the dispatcher thread. `Cancel` before the item
  runs drops it (`Canceled`); one that is running finishes.
* `RunIdleAsync(handler)`: idle priority; `IsDispatcherIdle` is true when nothing else is queued.
* `ProcessEvents(option)`: `ProcessOneIfPresent` and `ProcessAllIfPresent` return at once when nothing is queued;
  `ProcessOneAndAllPending` waits for an event when nothing is pending, then runs everything pending;
  `ProcessUntilQuit` runs until the window is closed. Windows messages of the thread are pumped meanwhile (so a
  native window will work later), `WM_QUIT` quits.
* After the dispatcher has quit, `RunAsync` returns an action that is already `Canceled`; what was queued before the
  quit still runs.

## CoreWindow (`windows.ui/window.c`)

`Activate` makes it visible and active; `Close` makes it invisible and closed. Both queue their events to the
dispatcher: `VisibilityChanged`, `Activated` (`CodeActivated`) and `Closed` are **raised while the thread processes
events**, never inside the call. `Close` ends `ProcessUntilQuit` after `Closed` was raised. All other events can be
subscribed to; they are not raised. `Bounds` is the screen size (there is no native window).

## Not implemented

* Presentation and input: no native window, no pointer, keyboard or character events, no `SizeChanged`, no
  `ICoreWindowInterop`, and so no DXGI target (`CreateSwapChainForCoreWindow` is still `E_NOTIMPL`). That is the
  next slice (U1.1b).
* Suspend, resume, background activation, `EnablePrelaunch`, restart (U1.1c). `Suspending` and `Resuming` can be
  subscribed to and are never raised.
* More than one view, `CreateNewView`, `Views`, application and window `Properties`, `GetIids`.
* AppContainer: the process is a full-trust packaged process; `Run` does not require or create a container.
* The entry point string passed to `Load` (empty).
* `CoreApplication.Exit` raises `Exiting` and closes the window of every view; it does not terminate the process.

## Assumptions without a Windows measurement

Everything above follows the documentation of `CoreApplication`, `IFrameworkView`, `CoreDispatcher` and
`CoreWindow`. These points are choices, not observations, and the comparison with a Windows 11 run (not done, see
`tests/uwp/u1_1/README.md`) may change them:

* the callbacks run on the thread that calls `Run` (a UWP process may host the view on another thread);
* the order of `VisibilityChanged`, `Activated` and the launch activation, and that they are raised from
  `ProcessEvents` at normal priority;
* `CoreApplication.Run` in a process without a package: error `APPMODEL_ERROR_NO_PACKAGE`; whether a full-trust
  package may call it at all;
* `ProcessEvents` from the wrong thread (`RPC_E_WRONG_THREAD`), `RunAsync` after quit, `GetResults` of a canceled
  action (`HRESULT_FROM_WIN32(ERROR_CANCELLED)`), `TileId`, `Load`'s entry point string;
* the apartment: the view thread is the caller's apartment (STA in the probe); ASTA semantics are not modelled.
