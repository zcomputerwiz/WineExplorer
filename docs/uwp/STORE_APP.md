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

## What a real answer needs

* **Application services as the system does them.** `OpenAsync` finds the provider declared in a catalog record
  (`uap:AppService`), starts it (a background activation of the package's application, or the full-trust companion) and
  connects the two ends; the provider receives `AppServiceTriggerDetails` (name, caller family, its end of the connection)
  in the background-activated event; messages are `ValueSet`s sent both ways (`SendMessageAsync`, `RequestReceived`,
  `SendResponseAsync`, deferrals), a closed end raises `ServiceClosed`. The catalog records do not carry the app service
  declarations yet (`wexappx` keeps only applications, activatable classes and dependencies).
* **`Windows.Foundation.Collections.ValueSet`** (declared in the IDL, no implementation) and a transport for it between
  processes.
* The names of what the binaries name and Wine lacks (the classes that start with `Windows.` in the Store's binaries and are
  neither in a Wine module nor served by XamlShim are listed by the scratch script used for this note; most are the
  Composition and media types of the projection tables, not calls). The ones that matter first: `ValueSet`,
  `Windows.UI.Notifications.*` (badges, toasts), `Windows.Management.Deployment.*` (`AddPackageOptions`,
  `DeploymentResult`), `Windows.Services.Store.StoreContext`, `Windows.Security.Cryptography.Core.*`,
  `Windows.Web.Http.*`, `Windows.Networking.BackgroundTransfer.*`, `Windows.UI.Popups.*`, `Windows.System.Profile.*`.
* After that the window itself (WinUI 2.8 on XamlShim, as the Calculator) and the WebView2 content, which needs an Edge
  WebView2 runtime that Wine does not have (the Chromium lab work may supply one).

## Smaller things seen

* `CoCreateInstance` of `{45BA127D-...}` (the application activation manager) from `applaunch.exe` logs "class not
  registered" (a local-server activation of a class registered as an in-process surrogate); harmless so far.
* The signature check says "not checked, the members, content types and block map of the package do not agree" for the zip made
  from the extracted folder (the extraction left out the files Windows keeps away from the folder); with `--development`.
