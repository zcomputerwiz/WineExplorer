<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# The Microsoft Store app on Wine (lab only)

The installed Microsoft Store (`Microsoft.WindowsStore` 22610.1401.10, x64) is the second real UWP target after the Calculator.
The package was copied into the owned lab guest for tests, as the Calculator was (none of its files enter this or any other
repository, CI or a release; only observations are written here). `tests/uwp/lab-helpers/store-run.sh` installs it (fresh prefix,
XamlShim 0.3.3, the framework components, `wexappx install --development`) and launches it; `store-go.sh` and `store-peek.sh`
launch it again in that prefix with debug channels and look at its threads.

## What the package is

* `WinStore.App.exe` and a 107 MB `WinStore.App.dll`: a **Native AOT .NET 10** application (CoreCLR 10.0.15, stack traces
  as `WinStore.App!<BaseAddress>+0x...`), WinUI 2.8 (`Microsoft.UI.Xaml.2.8`) and VCLibs 140 as the only dependencies,
  WebView2 (`Microsoft.Web.WebView2.Core.dll`), SQLite, Win2D (`Microsoft.Graphics.Canvas`).
* Three applications: the Store (`App`), `StoreDesktopExtension` (a full-trust COM server for six classes and a
  full-trust process of the Store) and `StoreCLI`. Extensions: 8 background tasks, app services
  (`StoreDesktopExtension-AppService`, `microsoft-store-appservice`, `microsoft-storeconfig`, `microsoft-store-windowssearch`),
  protocols, publisher cache folders. Capabilities: `runFullTrust`, `packageQuery`, `confirmAppClose` and others.

## Where it stops (2026-11-22)

1. **Installs and starts.** `wexappx` installs it (the dependency check passes with the five framework packages the
   `uwp-frameworks` component provides); the runtime starts, the Store loads `windows.applicationmodel.dll`.
2. **`Windows.ApplicationModel.AppService.AppServiceConnection` did not exist** (`REGDB_E_CLASSNOTREG`, a
   `System.NotSupportedException` that ended the process). The class now exists in `windows.applicationmodel.dll`
   (`appservice.c`, `include/windows.applicationmodel.appservice.idl`, a test in `tests/model.c`): the properties, the events
   and `Close` work, `OpenAsync` answers `AppServiceUnavailable` (no provider is hosted) and `SendMessageAsync` answers a
   response with the status `Failure`.
3. **The main thread then blocks forever** in a wait for an event that nothing sets (the process has two threads, the main
   one and the .NET finalizer; no window, no XAML class is activated, so XamlShim is never reached). The Store sets
   `PackageFamilyName` to its own family and `AppServiceName` to `StoreDesktopExtension-AppService`, adds `RequestReceived`,
   opens, reads the status, and (when the status is Success) adds `ServiceClosed`, then waits. Answering `Success` moves the
   wait one step further and no more. What it waits for is a message or a connection from the provider side: the real
   service has to be hosted.

## Prior art, and what was measured on Windows (2026-11-22)

* **Prior art for app services:** none open. A web search found no Wine, Uno or other open implementation of
  `AppServiceConnection` (Uno's generated class is a not-implemented stub); the only open pieces are Microsoft's API
  documentation and the Windows Community Toolkit's wrapper library (a client of the API). `ValueSet` is built on what Wine
  already has: the `wintypes` `PropertySet` (upstream, over its `multi_threaded_map`), which now also serves `ValueSet`.
* **Black box on the Windows host** (the Store launched from the shell, then stopped): two processes, `WinStore.App.exe`
  (71 threads) and, two seconds after it, `StoreDesktopExtension.exe` (9 threads): the Store starts its full-trust companion itself.
* On Wine the Store opens an app-service connection to the name `StoreDesktopExtension-AppService` (the manifest's app service) before the
  companion is started (seen in the Wine log), so on Windows the open must complete in a way that lets the start go on.

## The real wait (2026-11-22, found with a stack probe)

* **Where.** A probe that prints the code addresses on the main thread's stack (`tests/uwp/lab-helpers/appservice-probe.diff`,
  `WINEAPPSERVICE_STACK`; no debugger can attach: `winedbg` kills the process and an outside tool gets "access denied" on the
  threads of a packaged process) shows the main thread in the program's startup function (it reads the activation URI, the
  schemes `store` and `microsoftstore`, "This property must be accessed after calling 'BuildStartupServices'"), blocked in a
  synchronous wait (`Task.Wait`) on the asynchronous start of the application's services. The stack below it is the async state
  machine of that start, which is where the connection to `StoreDesktopExtension-AppService` is made.
* **What it waits for.** With the connection answered `Success` and a first request sent by the lab patch (`WINEAPPSERVICE_HELLO`:
  an empty `ValueSet` after 1.5 s), the Store answers it, so it is serving requests on that connection: the answer is a
  `ValueSet` with `__status` (an Int32, an error for an empty request). After that one request the Store goes on waiting: what it waits for is **the Desktop Extension
  process**, which is the other end of that remote-call layer (the extension offers methods such as `RunProcessAsync` and
  `LaunchWordFromExecutableAsync`, and the Store logs "The Store has been background activated for an AppServices response from
  the Desktop Extension").
* **The extension is a client of the same service name**: started alone (`applaunch ...!StoreDesktopExtension`), it constructs
  an `AppServiceConnection` for the same family and name, adds `RequestReceived` and opens, exactly as the Store does. So both
  processes run the same pairing code, and on Windows the system connects them: the extension opens the service of the Store's
  package, the system starts the Store in the background for it (`OnBackgroundActivated`, an `AppServiceTriggerDetails` with the
  server end of the connection) and the two exchange `ValueSet`s.
* **Windows**: `WinStore.App.exe` and, two seconds later, `StoreDesktopExtension.exe`.

So the missing piece is exactly app services between two processes of one package, with the system activating the provider
(a background activation of the application, delivered as `BackgroundActivated` with `AppServiceTriggerDetails`) and a transport
for `ValueSet`. Nothing smaller moves the Store.

## Where missing pieces come from

(An inventory of what the Store takes from the system was removed: it was counted from strings of the package's binaries, which the owner's
rule for this work does not allow. The manifest, the public API documentation and runs on Wine are the sources now.)

**Nothing of the `Windows.*` Runtime classes is a download.** The implementations of the `Windows.*` Runtime classes are
components of Windows itself. Microsoft publishes their *metadata and headers* (the Windows SDK, the
`Microsoft.Windows.SDK.Contracts` package) and, in the Windows App SDK, new implementations under `Microsoft.Windows.*` names,
not these. So they have to be written in Wine; only the frameworks and the WebView2 runtime are components.

### WebView2 hosting in XAML (general)

How the Store hosts WebView2 (windowed or by visual hosting) is not established here: the earlier reading of the Store's names was removed
(strings of a shipped binary). What is general and public: the WinUI 2.8 WebView2 control can host the browser by **visual hosting** (the browser
draws into a composition visual that is put into the XAML tree with `ElementCompositionPreview`), which needs (1) `Windows.UI.Composition`
(`Compositor`, `SpriteVisual`, `ElementCompositionPreview`) as a Windows Runtime API, which Wine does not have, and (2) a composition visual backed
by something the browser's output can reach: Wine's `dcomp` is the DirectComposition API, not the Windows.UI.Composition one. The lab's windowed
hosting (Windows 7 version plus SwiftShader) does not cover visual hosting. The WebView2 runtime component is enough as a *runtime*; the host
side is the open track for WebView2 in XAML and for WinUI 3 applications that use the same visual hosting.

### Is WebView2 enough, is WAM?

* **WebView2.** The runtime part is: the component installs the Evergreen runtime, registers it (the discovery keys), and
  windowed hosting works in the lab (Windows 7 version for `msedgewebview2.exe` plus SwiftShader: `docs/WEBVIEW2.md`).
  Not shown: visual hosting into XAML needs `Windows.UI.Composition` and DirectComposition, which Wine still answers `E_NOTIMPL`
  (`DCompositionCreateDevice`); which hosting the Store uses is not established here.
* **WAM.** The API and a broker exist, but only as far as `WAM_BROKER_DESIGN.md` says: a sign-in against the real service was
  never done end to end (only the owner can do it, by hand). Wine also has `AccountsSettingsPane` (now an object model: showing
  it asks the handlers for the commands and completes, no pane is drawn, so the Store's sign-in list is still not on screen) and the
  older `OnlineIdAuthenticator`. Enough for the Store to start and ask; not shown to be enough for it to sign in.

## What a real answer needs

* **Application services as the system does them.** `OpenAsync` finds the provider declared in a catalog record
  (`uap:AppService`), starts it (a background activation of the package's application, or the full-trust companion) and
  connects the two ends; the provider receives `AppServiceTriggerDetails` (name, caller family, its end of the connection)
  in the background-activated event; messages are `ValueSet`s sent both ways (`SendMessageAsync`, `RequestReceived`,
  `SendResponseAsync`, deferrals), a closed end raises `ServiceClosed`. The catalog records do not carry the app service
  declarations yet (`wexappx` keeps only applications, activatable classes and dependencies).
* **`Windows.Foundation.Collections.ValueSet`**: done (2026-11-22, `wintypes`, 13 checks in its test; the kinds of value a
  message may carry are not restricted yet) and a transport for it between processes (not done).
* More classes will show up as runs of the Store on Wine ask for them (an earlier list taken from strings of the package's binaries was removed).
* After that the window itself (WinUI 2.8 on XamlShim, as the Calculator) and the WebView2 content, which needs an Edge
  WebView2 runtime that Wine does not have (the Chromium lab work may supply one).

## Smaller things seen

* `CoCreateInstance` of `{45BA127D-...}` (the application activation manager) from `applaunch.exe` logs "class not
  registered" (a local-server activation of a class registered as an in-process surrogate); harmless so far.
* The signature check says "not checked, the members, content types and block map of the package do not agree" for the zip made
  from the extracted folder (the extraction left out the files Windows keeps away from the folder); with `--development`.
