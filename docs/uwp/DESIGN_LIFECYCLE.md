<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1.1: view lifecycle (CoreApplication, CoreWindow, CoreDispatcher)

First slice of U1.1, on Wine branch `codex/uwp-runtime` (2026-11-12): the lifecycle of a native `IFrameworkView`
application, **without presentation or input**. It builds on the package identity of `DESIGN_PACKAGE_CONTEXT.md`.

The behaviour below was **measured on Windows 11** (build 26300, x64 and x86, three runs each) with a genuine UWP
package (`tests/uwp/u1_1`: AppContainer executable, `EntryPoint="view.App"`, MSVC and C++/WinRT, signed with a
throwaway certificate). The same unchanged binaries pass on Wine. Points marked *(not measured)* are choices.

## What runs

`CoreApplication.Run(IFrameworkViewSource)` in a process that has a package context (`twinapi.appcore`):

* The caller must be in a **multithreaded apartment**: from a single-threaded one `Run` fails with
  `RPC_E_WRONG_THREAD` and nothing is called.
* The view runs on **a thread of its own**, not the caller's, which becomes the thread of its `CoreWindow` and of the
  window's dispatcher (the thread is in a single-threaded apartment on Wine; ASTA is not modelled). Every callback is
  on that thread, in this order:
  `CreateView`, `Initialize(view)`, `SetWindow(window)`, `Load(entry point)`, the launch activation, `Run`,
  `Uninitialize`.
* `Load` receives the **`EntryPoint` of the application in the manifest** (`view.App`). The catalog record carries it
  (`app` line, fourth field) and `TokenWinePackage` returns it.
* The **launch activation** (`CoreApplicationView.Activated`) is raised on the view thread after `Load` and before
  `Run`, not while events are processed: `LaunchActivatedEventArgs` with `Kind` Launch, `Arguments` the activation
  arguments (here the command line after the program name), `TileId` the application id, a `SplashScreen` object.
  `PreviousExecutionState` is not stable on Windows (0 and 3 were seen) and is `NotRunning` on Wine.
* The calling thread waits. When the view is finished the process ends and **`Run` does not return** to the caller;
  `Exiting` is raised on the calling thread, once. A view that fails in one of its steps makes `Run` return that
  error instead on Wine *(not measured)*.
* A process without a package context gets `APPMODEL_ERROR_NO_PACKAGE` on Wine; **Windows ends such a process
  with a fast fail**, so there is no comparable result. A second `Run` gets `E_ILLEGAL_METHOD_CALL` *(not measured)*.

## CoreApplication.Exit

Raises `Exiting` on the thread that called `Run`, then the system closes the window of the view: `Closed` is raised
on the view thread, `ProcessEvents(ProcessUntilQuit)` returns, the view returns from `Run` and is uninitialized, and
the process ends. On Windows the process may end at any point after `Exiting`, so only `Exiting` itself is asserted.

## CoreDispatcher (`windows.ui/dispatcher.c`)

One per window, bound to the window's thread.

* `HasThreadAccess` compares thread ids. `ProcessEvents` from another thread: `RPC_E_WRONG_THREAD`.
* `RunAsync(priority, handler)` may be called from any thread. Items run on the dispatcher thread, the highest
  priority first (High, Normal, Low, Idle), first in first out within a priority, **including items that a running item
  queues** (a High item queued by a Normal item runs before the other Normal items). The returned `IAsyncAction`
  completes after the handler ran; its `Completed` handler runs on the dispatcher thread. `Cancel` before the item
  runs drops it (`Canceled`).
* `RunIdleAsync(handler)`: idle priority; `IsDispatcherIdle` is true when nothing else is queued.
* `ProcessEvents`: `ProcessOneIfPresent` runs **all items that are queued when it is called** and returns (two items
  queued one after the other both ran); `ProcessAllIfPresent` and `ProcessOneAndAllPending` run everything pending
  and return at once when nothing is queued (on Windows `ProcessOneAndAllPending` returned at once here and waited
  for an event in one other run: not asserted); `ProcessUntilQuit` runs until the window is closed. Windows messages
  of the thread are pumped meanwhile, `WM_QUIT` quits.
* After the dispatcher has quit, `RunAsync` returns an action that is already `Canceled` *(not measured)*.

## CoreWindow (`windows.ui/window.c`)

* **`Activate` shows the window** (measured with an unlocked desktop; with the desktop locked the window stays
  invisible, nothing is raised and the application is suspended after about five seconds): `VisibilityChanged(true)`,
  `Activated` (CodeActivated) and `InputEnabled(true)` follow in that order while events are processed, about 80 and
  140 ms after the call; no `SizeChanged`. `Bounds` is the client area, 1200x900 for the default window. The native
  window is created on the view thread by `Activate` or by `ICoreWindowInterop::WindowHandle`.
* **Activation changes**: a click on an inactive window raises `Activated(PointerActivated)` before `PointerPressed`,
  losing the foreground `Activated(Deactivated)`, getting it back without a click `Activated(CodeActivated)`.
* **Mouse** (pointer id 1, device type Mouse, positions in client DIPs): `PointerEntered` and the first `PointerMoved`
  together, `PointerExited` on leaving; left and right buttons give `PointerPressed`/`PointerReleased` pairs
  (`IsLeftButtonPressed` true while the left button is down); wheel notches +-120 in `PointerWheelChanged`.
* **Keyboard**: `KeyDown`, then for a character key `CharacterReceived`, then `KeyUp`; auto-repeat is one `KeyDown`
  per repeat with `WasKeyDown` true and `RepeatCount` 1; `IsExtendedKey` for the arrow keys; no character for
  function and arrow keys. Details: `tests/uwp/u1_1/README.md`.
* **`Close` is refused**: it throws `E_ILLEGAL_METHOD_CALL`. `CoreApplication.Exit` closes the window (private export
  `WineUIExitCoreWindow`).
* All other events can be subscribed to and are not raised. `Bounds` is the screen size on Wine (1200x900 at 0,33 on
  Windows, not modelled).

## Not implemented

* Presentation and input beyond the above: touch and pen, pointer capture, `GetIntermediatePoints`, DPI scaling,
  resizing and the frame title; `CreateSwapChainForCoreWindow` is implemented through `ICoreWindowInterop` but is not
  yet exercised on a Wine with OpenGL or Vulkan.
* Suspend and resume: on Windows an application whose window is not visible gets `Suspending` after about five
  seconds, and a call that waits for events then stalls. `Suspending` and `Resuming` can be subscribed to and are
  never raised (U1.1c). Background activation, `EnablePrelaunch`, restart.
* More than one view, `CreateNewView`, `Views`, application and window `Properties`, `GetIids`.
* AppContainer: the process is not sandboxed; `Run` does not require or create a container. A UWP package differs
  from a full-trust package only by its manifest: both run unsandboxed on Wine.
* Splash screen display and `Dismissed`; the exit code of the ended process (0 on Wine, 1 seen on Windows).
