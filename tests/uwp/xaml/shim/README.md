<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Windows.UI.Xaml over Uno: the shim (XAML experiment step 2b, 2026-11-14)

An unmodified compiled XAML application (`tests/uwp/xaml/cx-probe`: C++/CX, `App.xaml`, `MainPage.xaml` compiled to
XBF) runs on Wine x64 with its `Windows.UI.Xaml` classes served by Uno (Skia, Win32 host) inside its own process.

## Pieces

**Where the code lives (owner decision 2026-11-14):** the managed component (WinmdGen and XamlShim) links Uno and is
Apache-2.0 in the owner's Uno fork: github.com/zcomputerwiz/uno-uwp, branch `wex/xamlshim`, folder `wex/` (it was
first written here as GPL and moved there in the same session). WineExplorer keeps the native side (`xamlproxy.c`,
GPL, to become part of Wine's `windows.ui.xaml.dll`), the test applications and the lab scripts. Setup downloads the
component after showing its terms; it is never shipped with WineExplorer.

| Piece | What |
|---|---|
| `xamlproxy.c` | Native `DllGetActivationFactory`. Starts CoreCLR once per process with `hostfxr` (self-contained XamlShim, `host_path` set to the component's own apphost so the application root is the component directory) and forwards the class name to `XamlShim.Activation.GetActivationFactory`. Named `xamlproxy.dll` because file names are case-insensitive and `XamlShim.dll` is the managed assembly. Later: folded into Wine's `windows.ui.xaml.dll` as the fallback for classes it does not implement. |
| uno-uwp `wex/WinmdGen` | Reads `Windows.winmd` (System.Reflection.Metadata) and emits `wex/XamlShim/Abi.g.cs` for the classes in `wex/WinmdGen/classes.txt`: IIDs; one ABI interface per WinRT interface (`[exclusiveto]` and overrides included) whose methods log "not implemented" and return `E_NOTIMPL` unless implemented; vtables of `[UnmanagedCallersOnly]` thunks; callers for native objects; blittable structs; one partial class per runtime class mirroring the class hierarchy, and one per activation factory. The union metadata names some Foundation types by their .NET projections (`System.TimeSpan`, `System.Type` for `TypeName`, `EventRegistrationToken`, `System.Exception` for `HResult`); WinmdGen maps them back. `wex/generate.ps1` runs it. |
| uno-uwp `wex/XamlShim/Ccw.cs` | Native-callable wrappers: one 16-byte block per interface (`[vtable][GCHandle]`), identity, reference count; COM aggregation for composable classes (the application's `App` and `MainPage` are outer objects over our `Application` and `Page`); unwrapping of pointers the application passes back (direct, or through the outer with a private IID). |
| uno-uwp `wex/XamlShim/Wux.cs` | The members the probe uses, over Uno: `Application` (`Start` runs the Uno host on an STA thread, calls the application's callback, then `OnLaunched` on the outer; `LoadComponent`, `Current`, `Exit`), `Window` (`Current`, `Content`, `Activate`), `Frame.Navigate` (the page type through the application's `IXamlMetadataProvider` and `IXamlType.ActivateInstance`), `Page` and `Frame` factories, `FrameworkElement` (`ActualWidth`, `ActualHeight`, `Loaded`), `UIElement.TransformToVisual`, `GeneralTransform.TransformPoint`, `ButtonBase.Click`, `DispatcherTimer`. |
| uno-uwp `wex/XamlShim/XbfLoader.cs` | `LoadComponent`: XbfKit (MIT, NuGet `XbfKit` 0.4.0, github.com/0x5bfa/XbfKit; used as a library, no code copied) turns the XBF back into XAML text with connection IDs; Uno's `XamlReader` builds the content; each connected element goes to the application's `IComponentConnector.Connect` (its `x:Name` fields and `Click` handler). |
| `shimtest.c` | Loads `xamlproxy.dll` and asks for one factory (checks the hosting on Windows). |
| `lab/` | The lab runs: `cxprobe-stage.ps1` (unpacks the probe package, adds the VCLibs DLLs from the SDK's framework package and the `.xbf` files; lab only, nothing of it enters the repository), `shim-publish.ps1`, `xamlproxy.reg` (the eight classes to `C:\xamlshim\xamlproxy.dll`), `cxprobe-run.sh` (fresh prefix, register, launch, wait), `shim-cycle.sh`. |

## Result (Wine 1052fab3, x64, Xvfb)

`expected-wine-uno.txt`; Windows 11 with the system XAML: `../cx-probe/expected-windows.txt`.

| | Windows 11 (system XAML) | Wine + XamlShim over Uno |
|---|---|---|
| start | app constructed, launched, page constructed | the same |
| page | 1200x900 | 1016x606 |
| Title `TextBlock` | 237.949x26.6016 at 24,24 | 248x28 at 24,24 |
| Go `Button` | 76x32 at 24,63 | 84x33 at 24,64 |
| end | `Application.Exit` from a `DispatcherTimer` | the same |

CoreCLR starts in the probe's process in 0.2 to 0.4 s on Wine. Differences left: the window size (UWP's default is
not applied), fonts and control metrics (Uno's Fluent styles, not the UWP defaults).

## Found on the way

* The application's initialization callback creates `App` in a local and releases it: the framework has to hold the
  application object (and the pages in the tree). The experiment keeps one reference on every outer object for good.
* COM aggregation: an interface the inner returns through its non-delegating `QueryInterface` must be `AddRef`'d on
  the outer (its `Release` goes there). Counting it on the inner freed the application's `MainPage` while it was on
  screen.

## Not done (next)

XBF from `resources.pri` (embedded data; Wine's `GetValueAsStreamAsync` for embedded candidates, or a PRI reader in the
component); real lifetimes; the window size and the CoreWindow/CoreApplication activation path; UWP default styles
(WinUI 3 `generic.xaml`, MIT, adapted back); the rest of the members, driven by the generated "not implemented" log;
x86; `DllGetActivationFactory` fallback in Wine's `windows.ui.xaml.dll` and the setup component.
