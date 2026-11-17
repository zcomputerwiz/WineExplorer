<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Store Calculator 11.2607: the Windows Runtime classes it needs (gap list, 2026-11-16)

A static list, made without the package's code: the open-source Calculator (MIT) at the commit of its 11.2607 version
(9f186049, "Increment minor version from 2606 to 2607"; C++ view models in CalcViewModel, a C# UI, C# application view
model) was scanned for the Windows Runtime classes it constructs or calls statics of, against every runtime class of the
Windows SDK 10.0.26100 metadata (`winrt-uses.py`). The strings of the package's own metadata (`CalculatorApp.ViewModel.winmd`,
`GraphControl.winmd`, `TraceLogging.winmd`, read in the lab) name 14 SDK classes, all found by the source scan, so the
source matches the Store build. `gap-list.py` compares the list with the classes Wine registers (its modules'
`classes.idl`) and those XamlShim serves (its generated ABI). Classes only named or reached through other objects are not
counted (their instances come from APIs that are). A static list finds missing classes; a class that exists but has
stub methods (E_NOTIMPL) or behaves differently shows only at run time.

Covered (2026-11-16): 39 classes by Wine (twinapi.appcore 7, windows.globalization 8, wintypes 6, windows.ui 4,
windows.storage 4, windows.applicationmodel 3, windows.web 2, and one each of iertutil, windows.graphics,
windows.networking.connectivity, windows.storage.applicationdata, windows.ui.xaml), 16 by XamlShim.

## Missing in Wine (15, and 2 the .NET Native runtime asks for)

| Class | Use |
|---|---|
| Windows.ApplicationModel.DataTransfer.Clipboard | statics (GetContent, SetContent, SetContentWithOptions) |
| Windows.ApplicationModel.DataTransfer.DataPackage | constructed |
| Windows.ApplicationModel.DataTransfer.HtmlFormatHelper | statics (CreateHtmlFormat) |
| Windows.ApplicationModel.DataTransfer.StandardDataFormats | statics (Text) |
| Windows.ApplicationModel.LimitedAccessFeatures | statics (TryUnlockFeature) |
| Windows.ApplicationModel.UserActivities.UserActivityChannel | statics (GetDefault); served since Wine 4b3ed43b (with UserActivity, its visual elements, attribution, content info and session) |
| Windows.Globalization.DateTimeFormatting.DateTimeFormatter | constructed |
| Windows.Globalization.NumberFormatting.CurrencyFormatter | constructed |
| Windows.System.Launcher | statics (LaunchUriAsync) |
| Windows.System.Threading.ThreadPoolTimer | statics (CreateTimer) |
| Windows.UI.Core.SystemNavigationManager | statics (GetForCurrentView) |
| Windows.UI.StartScreen.JumpList | statics (LoadCurrentAsync); served since Wine 4b3ed43b |
| Windows.UI.StartScreen.JumpListItem | statics (CreateWithArguments); served since Wine 4b3ed43b |
| Windows.UI.Text.FontWeights | statics (Normal, SemiBold, SemiLight, Thin) |
| Windows.UI.ViewManagement.ViewModePreferences | statics (CreateDefault) |
| Windows.Foundation.Diagnostics.AsyncCausalityTracer | the .NET Native runtime (seen at run time) |
| Windows.System.MemoryManager | the .NET Native runtime (seen at run time) |

## Missing in XamlShim (18)

| Class | Use |
|---|---|
| Windows.UI.Xaml.VisualStateManager | statics (GoToState, 35 calls) |
| Windows.UI.Xaml.Media.VisualTreeHelper | statics (GetParent, GetChild, GetChildrenCount) |
| Windows.UI.Xaml.Input.FocusManager | statics (GetFocusedElement, TryFocusAsync, TryMoveFocusAsync) |
| Windows.UI.Xaml.Controls.ToolTipService | statics (SetToolTip) |
| Windows.UI.Xaml.Controls.Canvas | statics (SetLeft, SetTop) |
| Windows.UI.Xaml.Automation.Peers.FrameworkElementAutomationPeer | statics (FromElement) |
| Windows.UI.Xaml.Automation.Peers.TextBlockAutomationPeer | statics (FromElement) |
| Windows.UI.Xaml.Automation.Peers.ButtonAutomationPeer | constructed |
| Windows.UI.Xaml.Controls.RichTextBlock | statics (FontSizeProperty) |
| Windows.UI.Xaml.Documents.TextElement | statics (FontSizeProperty) |
| Windows.UI.Xaml.Controls.ContentDialog | constructed |
| Windows.UI.Xaml.Controls.Flyout | constructed |
| Windows.UI.Xaml.Controls.Primitives.FlyoutShowOptions | constructed |
| Windows.UI.Xaml.Controls.ToolTip | constructed |
| Windows.UI.Xaml.Input.KeyboardAccelerator | constructed |
| Windows.UI.Xaml.Media.DoubleCollection | constructed |
| Windows.UI.Xaml.Media.FontFamily | constructed |
| Windows.UI.Xaml.Setter | constructed |

## Status (2026-11-16)

* Done in XamlShim (1247b2d640, then FontWeights, which Windows serves from Windows.UI.Xaml.dll): the generator forwards
  methods to Uno's methods of the same name (statics, factory constructors), and the 18 classes above plus
  Windows.UI.Text.FontWeights are served.
* Done in Wine: AsyncCausalityTracer (3fdfa06d) and MemoryManager (43ebe826).
* Left in Wine, none reached before the first calculator view: SystemNavigationManager (the Settings page),
  ThreadPoolTimer (the unit converter), ViewModePreferences (always on top), the clipboard classes, DateTimeFormatter and
  CurrencyFormatter (date and currency modes), Launcher, JumpList, JumpListItem, LimitedAccessFeatures,
  UserActivityChannel.
* Seen at run time, outside this list: WinUI 2's Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider (the
  application's XAML metadata asks it for WinUI 2 types; XamlShim does not serve it yet).

## Order of work

1. XamlShim: static methods of statics interfaces forwarded to Uno's static methods of the same name by the generator
   (as properties and attached properties already are), then the 18 classes in `WinmdGen/classes.txt`.
2. Wine, small and certain to be reached: FontWeights, ViewModePreferences, SystemNavigationManager,
   ThreadPoolTimer, AsyncCausalityTracer, MemoryManager.
3. Wine, the clipboard (Clipboard, DataPackage, StandardDataFormats, HtmlFormatHelper), then DateTimeFormatter and
   CurrencyFormatter (the date and currency modes), Launcher, JumpList and JumpListItem, LimitedAccessFeatures,
   UserActivityChannel.

Each Wine class is measured on Windows 11 first and gets a test that passes there and on Wine (x64 and x86).
