<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# The Windows Calculator on Wine (lab only)

The installed Microsoft Store Calculator (11.2607.0.0, x64) is the first real UWP target. The owner allowed copying the
package into the owned lab VM for tests (2026-11-14); none of its files enter this or any other repository, CI or
release. Only observations are recorded here.

## How it runs (lab)

`calc-run.sh` (lab helper, kept with the lab scripts): a fresh prefix; the `uwp-xaml` component (XamlShim 0.2.0) and
the `uwp-frameworks` component through their own install code; the package zipped and installed with
`wexappx install --development` (its five framework dependencies resolve in the machine catalog); activation through the
U1.0 launcher with `XAMLSHIM_TRACE=1`; screenshots of the Xvfb screen. Wine `codex/uwp-runtime` plus the B14 loader fixes
(uncommitted in the lab until Session 0 lands them).

## Status, 2026-11-14

Works, unmodified package: install and dependency resolution; the .NET Native runtime (`mrt100_app.dll`,
`SharedLibrary.dll`) initialises without `mrt100.dll`; `CoreApplication.Run` with our view source; XamlShim serves
`Windows.UI.Xaml.Application`, composes the app's `Calculator.App` (outer object, initialization callback).

Stops at: the initialization callback. The app's own `CalcViewModel.dll` (activated in-package) fails in `DllMain`:
its C++/CX static initialisers register dependency properties, which needs `Windows.UI.Xaml.PropertyMetadata`; the
factory is missing, a `Platform::Exception` (C++ exception `e06d7363`) ends the initialisation. `OnLaunched` then
returns `E_NOINTERFACE`. Nothing is drawn yet. Also seen, not fatal so far: `Windows.System.MemoryManager` (Wine).

## Size of what is left (lower bound)

Type names in the binaries' strings (UTF-16 and ASCII; names only, no code is looked at): the Calculator's
`CalculatorApp.dll`, `CalcViewModel.dll` and `GraphControl.dll` name 82 `Windows.UI.Xaml` types; WinUI 2.8's
`Microsoft.UI.Xaml.dll` names 165 `Windows.UI.Xaml`/`Windows.UI.Composition` types (16 of them Composition); 193 distinct
together. XamlShim 0.2.0 serves about 30. The Calculator's include: `DependencyObject`/`DependencyProperty`/
`PropertyMetadata`/`PropertyPath`, `Style`/`Setter`/`ResourceDictionary`, `Binding`/`XamlBindingHelper`/
`XamlMarkupHelper`, `VisualStateManager`/`StateTriggerBase`, `Flyout`/`FlyoutBase`/`Popup`/`ContentDialog`/`ToolTip`,
`AutoSuggestBox`, `RichEditBox`/`RichTextBlock`, `Slider`, `ToggleSwitch`, `RadioButton`, `Viewbox`, `Canvas`,
`UserControl`, `ContentPresenter`, `KeyboardAccelerator`, `FocusManager`, `VisualTreeHelper`, `ThemeShadow`,
`ElementCompositionPreview`. WinUI 2 itself is native code over the system XAML and Composition, the hardest part.

### Later the same day (uno-uwp 475245e3, lab build of XamlShim)

Dependency properties from native code work (the probe's checks match Windows): `CalcViewModel.dll` loads and registers
its properties (`LocalizationService.FontType`/`FontSize` attached, `NarratorNotifier.Announcement`), the application's
initialisation completes (`Application.HighContrastAdjustment`, which Uno lacks, is stored on the shim). `OnLaunched`
then fails in the application's own code: "Unable to cast object of type
`Windows.ApplicationModel.Activation.LaunchActivatedEventArgs`". The open-source Calculator (`microsoft/calculator`, MIT)
reads `args.User.NonRoamableId` (`IActivatedEventArgsWithUser`, `Windows.System.User`), `args.PrelaunchActivated`
(`IPrelaunchActivatedEventArgs`), `Arguments`, `Kind`, `SplashScreen`, then `ApplicationView` and the jump list. Wine's
launch arguments (`twinapi.appcore`) implement `IActivatedEventArgs` and `ILaunchActivatedEventArgs` only, and Wine has
no `Windows.System.User`. Next: measure those interfaces on Windows with the probe, then implement them in Wine.

### 2026-11-15 (Wine e863e73a, uno-uwp SizeHelper)

The launch arguments now have the interfaces Windows gives them (with the user, prelaunch, view, view switcher,
tile info), `Windows.System.User` exists, `ApplicationView.GetForCurrentView` gives a real view (`SetPreferredMinSize`,
`TryResizeView`, the launch preferences), `SizeHelper` is served; the probe's 21 launch lines are the same on Wine as on
Windows 11. The Calculator passes those calls and stops at `Application.Resources` (null): its `App.xaml` resources
(WinUI 2's `XamlControlsResources`, theme dictionaries, its own converters and controls) were never loaded. Its
`App()` calls `InitializeComponent()`, but no `LoadComponent` reaches XamlShim; how the compiled `App.xaml` reaches the
XAML layer is the next thing to find. Loading it needs the application's own (native) types inside Uno's XAML reader.
Also asked for and missing: `Windows.System.User` statics, `Windows.Foundation.Diagnostics.AsyncCausalityTracer`,
`Windows.System.MemoryManager`.

### 2026-11-15 (Wine development 1fab56f6, uno-uwp a36195de35)

As on Windows (probe: Resources fail with E_UNEXPECTED in `App()`, hold `App.xaml`'s resources at `OnLaunched`), the
framework itself loads `App.xbf` from `resources.pri` into `Application.Resources` between construction and
`OnLaunched`; XamlShim now does so. Of the Calculator's: the merged `XamlControlsResources` and 55 plain entries load;
its three theme dictionaries ("Implicit content is not supported on ApplicationTheme") and button styles fail in Uno's
XAML reader. Reading `Resources["AppMinWindowWidth"]` failed on Wine's `PropertyValue.GetRuntimeClassName` stub (Wine
22af9251 fixes it); `Windows.Globalization.Fonts.LanguageFontGroup` was missing (Wine f50b362a adds it);
`Window.Dispatcher` was null (XamlShim a36195de35). The Calculator now navigates to `MainPage` and loads its XBF;
Uno's XAML reader stops at the application's own control `{using:CalculatorApp}TitleBar`. Next: native application
types (their `IXamlMetadataProvider`) inside Uno's XAML reader, then the theme dictionaries.

### 2026-11-15, later (Wine development aaa610f4, uno-uwp 42a7a44426)

The application's own controls and markup extensions now work inside Uno's XAML reader (stand-in .NET types the
application's controls compose over; members forwarded to its `IXamlMember`). `TitleBar` is constructed, loads its
XAML and connects its elements, after Wine gained the view title bars (f140b080) and `Windows.UI.Colors` (aaa610f4)
and XamlShim `Window.SetTitleBar`. `ResourceString` extensions get their names. Open:
- WinUI 2: `MainPage` fails at `Connect 16 NavView` (E_NOINTERFACE): the application expects WinUI 2's native
  `Microsoft.UI.Xaml.Controls.NavigationView` (from the `Microsoft.UI.Xaml.2.8` framework); XamlShim gives Uno's.
- Community Toolkit `SettingsExpander`: its content property (`Items`, a collection) and members whose values are
  framework objects (`HeaderIcon` = `FontIcon`) are not forwarded yet; default styles from the toolkit's
  `Themes/Generic.xaml` are not applied.
- `App.xaml` theme dictionaries and styles that Uno's reader rejects.

### 2026-11-15, evening (Wine development 0a4975cf, uno-uwp 87e605b2dd)

WinUI 2 is mapped onto Uno: XamlShim's WinUI 2 interfaces are generated from metadata built from WinUI 2's
MIT-licensed IDL (`wex/winui2/build-metadata.ps1`, no Microsoft binary), and a Uno control that WinUI 2 also has
(`NavigationView`) gets the WinUI 2 shim. `NavView` and `Popup` connect; Wine gained
`ApplicationView.GetApplicationViewIdForWindow` (2337013f) and `UserActivityRequestManager` (0a4975cf), both
measured on Windows with the XAML probe. `MainPage` is now constructed, the Frame navigates to it and becomes
`Window.Content`. Then:
- `OnLaunched` ends with KeyNotFoundException ("key") thrown in the application's code (not a failed
  `Application.Resources` lookup).
- `Loaded` handlers fail: after `Popup.IsOpen` (E_NOTIMPL) and after `NavigationView.MenuItemsSource` (null).
- Nothing is drawn yet (blank screenshot).
- `App.xaml`: the three theme dictionaries ("Implicit content is not supported" in Uno's reader), the button styles
  (`CalcButtonStyle`, `NumericButtonStyle*`, `OperatorButtonStyle`, ...) and one merged dictionary do not load.
- `Settings`: the toolkit `SettingsExpander`'s content (`Items`) and icon members are not forwarded.

Later (uno-uwp d5ae3be9a2): `App.xaml` now loads as a whole (92 entries, 3 theme dictionaries, 2 merged). XamlShim
resolves conditional namespaces, takes `<StaticResource>` aliases and enum values out and adds them after loading,
qualifies application types in `TargetType`, gives attached setter properties back their owner (XbfKit drops it),
leaves out setters of properties Uno lacks (`RichEditBox.ContentLinkForegroundColor`), and finds the application's
own dependency properties for style setters (`IXamlType.RunInitializer` first). Still open: the KeyNotFoundException
at the end of `OnLaunched`, the two failing `Loaded` handlers, `Settings`, and nothing drawn yet.

### 2026-11-15, night (Wine development f1347be7, uno-uwp 90eb538e82)

`OnLaunched` completes and the window is shown (320x500, the minimum size the application sets; the CoreWindow
frame holds Uno's window). The KeyNotFoundException was Wine's settings `Lookup` of a missing key (E_BOUNDS where
Windows gives null; Wine a2356079). XamlShim now connects a page's root and its x:Bind binding connector
(`IComponentConnector2`), loads a page's own `<Page.Resources>`, forwards the application's converters and commands,
and serves statics' property getters (`Popup.IsOpenProperty`). The visual tree has the main page, the TitleBar and
Uno's NavigationView template. Next: `NavigationView.MenuItemsSource` is the application's own observable vector of
categories, which needs a .NET list over the native `IVector` (with `VectorChanged`) and item templates whose x:Bind
bindings reach native items; the content area (the Standard calculator) is navigated to from there. Then `Settings`
(toolkit `SettingsExpander` content and icons).

Later (Wine development 19df3184, uno-uwp 50bbf05770): the application's collections are .NET lists for Uno
(`IBindableVector`/`IVector<Object>`, VectorChanged relayed), root element attributes are applied (`TitleBar`
`Height="32"`), and Wine has `Language.IsWellFormed`/`CurrentInputMethodLanguageTag` (19df3184). The main page's
bindings complete and the Calculator creates its first view, which stops at
`Windows.Globalization.NumberFormatting.DecimalFormatter` (not in Wine; no prior art upstream) and
`Windows.UI.Xaml.Controls.DataTemplateSelector` (not yet served by XamlShim). Also seen: the application asks 74 times
for WinUI 2's `Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider`; one Loaded handler still throws
NotImplementedException.

### 2026-11-16 (Wine development 0845ed03, uno-uwp 19d5ccc234 and later)

The view model is made: Wine has `IApplicationView4` (the application asks `IsViewModeSupported(CompactOverlay)`
when it sets its mode, before it makes the view model), and XamlShim serves `PropertyChangedEventArgs` (the C++/CX
view model makes one for every property it changes). With `SizeChangedEventArgs`, `Style` and twelve more classes
generated (the shim now logs each framework class served by its base class's shim), `OnLaunched`, `OnNavigatedTo`
and every event handler return S_OK.

It draws. The X screenshot of the screen is black inside the window frame (the screen does not show Uno's window
surface), but the window's pixels read back through GDI (`XamlShim` writes `xamlshim-window.bmp` 5 s after the
window is placed, with `XAMLSHIM_TRACE=1`; the quick loop fetches it as `calc-window.png`) show the title "Calculator",
the navigation button, "Standard", the keep-on-top and history buttons and the accent-coloured "=" button.

Later (uno-uwp 22fab8e31b): the Standard calculator is drawn with its display ("0") and the whole keypad: digits,
operators and the icon glyphs of the package's own font. What it took in XamlShim: `Style.BasedOn` completed from
the XAML (Uno's reader left 31 of the application's 42 unresolved, so the buttons kept no setters of
`CalcButtonStyle` and were 0 pixels high); `ms-appx:///` values of the application mapped to its package's files
(Uno looks in its own folder); every scalar `PropertyValue` (the digit buttons' content is a Char16);
`ContentControl.Content` taking values; the application's `OnApplyTemplate` overrides called (the display control
finds its text block there); bindings by `ElementName` to the compiled XAML's root followed from the root's property.
(Both were fixed later, see "Layout fixed".)

Later the same day (Wine development f0f972f8, uno-uwp see its log): **7 + 5 = 12 by mouse**. The clicks (a Wine program,
`lab-helpers/calc-input.c`, SendInput on the lab's display) reach Uno and its buttons; the display shows 7, 7, 5, 12.
What it took: Uno's window was never a child of the CoreWindow (`SetParent` failed with 5023: Wine's server needs the
same DPI awareness and the CoreWindow's thread was system aware, where Windows' is per-monitor, measured with the
XamlProbe; Wine 41c7bf93), so pointer positions were the screen's; the child window is held to the CoreWindow's client
area; bindings (`{Binding Path}`) to the application's native objects (the view model) now read through the application's
XAML metadata (`DynamicObject`, `INotifyPropertyChanged` forwarded), so each button's `ButtonPressed` command is
bound; and the IInspectable pointer of a shim object is usable as its default interface (.NET Native called a class's
member through the pointer it had met as `Object`: a null vtable slot). Wine f0f972f8: `GetIids` of the dispatcher.
**7 + 5 Enter by keyboard gives 12 as well** (uno-uwp, no Wine change: Wine's CoreWindow already raises KeyDown, KeyUp and
CharacterReceived from its window messages, but the keys go to Uno's window, the focused one). XamlShim sends the key
messages of Uno's window to the CoreWindow first and keeps a key the application marked handled from Uno's tree;
Uno takes the character TranslateMessage posted from the queue itself, so it is sent along with its key; the stand-ins of
application types get static `Get`/`Set` methods for the members their XAML metadata says are attachable (Uno's reader
ignored `KeyboardShortcutManager.Character` and `.VirtualKey` without them: no key was registered); an `IReference<T>` of an
application type (`ResourceVirtualKey` gives a `MyVirtualKey`) is kept as the application's object.
**Layout fixed** (uno-uwp fcd89337db): the decimal point and the clipped keypad text were one bug. The first full layout threw
in NavigationView ("Null encountered as data template": the application's `NavViewMenuItemTemplateSelector` had no templates,
because Uno's reader left its `{StaticResource}` attributes null, as it does `Style.BasedOn`, and its `SelectTemplateCore
was never called), so every element kept the measure of its first pass: text boxes 10 px high, glyphs cut at the bottom.
The fix: the selectors' `SelectTemplateCore` is forwarded to the application; their template references are filled once the
dictionary is built; one .NET wrapper per application object (Uno compares by reference, and NavigationView's SelectedItem
looped to a stack overflow with a new wrapper on every read); `DataTemplate` is a shim class. `XAMLSHIM_LATE_TREE=<s>` dumps the
tree again later (with `INPUT`); the dump shows metrics, available size and `STALE` for elements arranged larger than measured.
Open: the expression line above the display. Its template elements are now connected (GetBindingConnector, Connect,
`DataTemplate.SetExtensionInstance` remembered) and the application's DataContextChanged handler of the x:Bind runs, but it
hits a null reference inside the application (an access violation at one address in CalculatorApp.dll, 18 times) before it
sets any text; not yet explained.

Earlier next step: dependency-property registration from native code (`DependencyProperty.Register*` with `TypeName` owner and
property types that are not .NET types, `PropertyMetadata` with a native `PropertyChangedCallback`), then the next gap
the Calculator shows. Prior art and the Wine gaps others listed for the Calculator: `tests/uwp/frameworks/README.md`.
