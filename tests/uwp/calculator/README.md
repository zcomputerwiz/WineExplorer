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

Earlier next step: dependency-property registration from native code (`DependencyProperty.Register*` with `TypeName` owner and
property types that are not .NET types, `PropertyMetadata` with a native `PropertyChangedCallback`), then the next gap
the Calculator shows. Prior art and the Wine gaps others listed for the Calculator: `tests/uwp/frameworks/README.md`.
