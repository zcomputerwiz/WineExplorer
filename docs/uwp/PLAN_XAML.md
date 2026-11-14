<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Plan: Windows.UI.Xaml for UWP applications (not started)

Written 2026-11-13 on the owner's instruction: **use Uno and WinUI 2 as implementation guidance, write the plan, do not
start**. Nothing in this plan is built yet. It extends `UWP_COMPATIBILITY_PLAN.md` (the "XAML and conditional private
application trials remain later gates") and uses what U1.0 to U1.4 delivered (package identity, activation, view
lifecycle, CoreWindow with input, DXGI target, package information, framework dependencies, ApplicationData).

## 1. Goal and non-goals

**Goal.** An *unmodified, Store-style XAML UWP package* (compiled XAML, `.xbf` resources, `resources.pri`, framework
dependencies such as `Microsoft.VCLibs` and `Microsoft.UI.Xaml.2.x`) installs with `wexappx`, starts through the
activation manager and shows a working window on Wine. First target application: the Windows Calculator (source:
`microsoft/calculator`, MIT; the installed package has separate terms and is only a local oracle), then a small
open-source XAML application chosen for breadth.

**Non-goals.** WinUI 3 / Windows App SDK (Microsoft.UI.Xaml 3, desktop only); managed (.NET Native / C#) UWP apps in the
first phases; Store services; XAML Islands; the visual designer; a Wine-wide Windows.UI.Composition implementation (only
what XAML needs).

## 2. Where we are

* Wine has `windows.ui.xaml.dll` with only `ColorHelper` (about 190 lines) and three IDLs
  (`windows.ui.xaml.idl`, `.hosting.desktopwindowxamlsource.idl`, `.interop.idl`) covering ten runtime classes; nothing
  else of `Windows.UI.Xaml` exists.
* Done and measured on Windows 11: `CoreApplication.Run` view lifecycle, `CoreWindow`/`CoreDispatcher`, pointer, key and
  character events, `ICoreWindowInterop` and `CreateSwapChainForCoreWindow` (Direct3D 11), package identity and
  information, framework dependencies on the DLL search path, `ApplicationData`, `StorageFolder`, `StorageFile`,
  `FileIO`, `PathIO` and file streams (U1.5, 2026-11-13).
* Missing for XAML beyond XAML itself: MRT/PRI resource loading
  (`ResourceManager`, `ResourceLoader`, `ms-resource:`/`ms-appx:` URIs), XBF2 loading, `Windows.UI.Composition`
  (XAML renders through it), text services (`Windows.UI.Text.Core`), `Windows.UI.ViewManagement` completeness,
  accessibility (UI Automation) providers.

## 3. What a XAML application needs, by layer

1. **Activation and metadata.** `Windows.UI.Xaml.Application` as the base class of `App` (`IApplicationOverrides`,
   `Application.Start(callback)`), `Windows.UI.Xaml.Window`, `Application.LoadComponent(object, Uri)` for each page and
   resource dictionary, class activation of hundreds of runtime classes from `Windows.UI.Xaml.dll`, runtime class names
   and WinRT type information for the application's own classes (`IXamlMetadataProvider`, `IXamlType`).
2. **Property system.** `DependencyObject`, `DependencyProperty` (registration, metadata, default values, coercion
   order, inheritance, attached properties, change callbacks), `SetValue`/`GetValue`, value precedence, `ClearValue`,
   property paths for animation and binding.
3. **Visual tree and layout.** `UIElement`, `FrameworkElement`, `Panel` family (Grid, StackPanel, Canvas,
   RelativePanel, VariableSizedWrapGrid), Measure/Arrange, `LayoutUpdated`, `SizeChanged`, `Visibility`, clipping,
   transforms, hit testing, `Popup` and the popup layer, `ScrollViewer`/`ScrollContentPresenter`, `ItemsControl` and
   virtualizing panels.
4. **Styles, templates, resources.** `ResourceDictionary` (merged, theme dictionaries, `StaticResource`/`ThemeResource`
   lookup order), `Style`, `Setter`, `ControlTemplate`, `DataTemplate`, `VisualStateManager`, `VisualState`,
   `AdaptiveTrigger`, `Storyboard`/animations (including the composition-driven ones), implicit transitions.
5. **Binding.** `Binding`, `x:Bind` support types (`DataTemplate` `Phase`, `BindingExpression`), `INotifyPropertyChanged`
   and `ICustomPropertyProvider` plumbing, value converters, `CollectionViewSource`.
6. **Input and focus.** Routed events (pointer, key, focus, drag, manipulation, gestures), focus manager and tab
   navigation, access keys, `InputScope`, text input (`TextBox`, `RichEditBox`, IME through `CoreTextEditContext`),
   pointer capture, tooltips and context flyouts.
7. **Controls.** The default control set (Button family, TextBlock, TextBox, CheckBox, RadioButton, ToggleSwitch,
   ComboBox, ListView, GridView, FlipView, Pivot, Hub, SplitView, CommandBar, AppBar, MenuFlyout, ContentDialog, Slider,
   ProgressBar/Ring, Image, MediaElement, WebView (later), Frame/Page with navigation) and their default templates
   (`generic.xaml`).
8. **Rendering.** Composition visuals, brushes (solid, image, acrylic fallback, reveal fallback), shapes, text via
   DirectWrite/Direct2D, images (WIC, SVG later), effects and shadows, DPI scaling and rasterization scale, 60 Hz
   frame pacing on `CoreWindow`.
9. **Framework packages.** `Microsoft.UI.Xaml.2.x` (WinUI 2: NavigationView, TreeView, TabView, ColorPicker, ...; native
   C++ on top of the system XAML), `Microsoft.VCLibs.140.00(.UWPDesktop)`, `Microsoft.NET.Native.*` (managed, later).

Layers 1 to 8 are the operating system's `Windows.UI.Xaml.dll` and are not open source. Layer 9 is separate
redistributable packages: WinUI 2 is MIT and can be built from source; VCLibs is Microsoft's and has to come from the
owner's installed Windows or from an equivalent we build (`vcruntime140_app` is the Visual C++ runtime for UWP).

## 4. How Uno and WinUI 2 are used

* **`microsoft-ui-xaml` winui2/main (MIT).** Not a replacement for layers 1 to 8 (it is written *on top of* them). Used for:
  (a) the controls and templates of layer 9, built as the `Microsoft.UI.Xaml.2.x` framework package once the system
  XAML under it works; (b) the exact default templates, visual states and resource keys of the Fluent controls, as a
  specification of visuals and behaviour; (c) its test suite (MUXControlsTestApp) as a corpus of XAML pages and
  interaction tests. WinUI 3 source (`main`) is read for ideas only.
* **Uno Platform (`unoplatform/uno`, copy at `zcomputerwiz/uno-uwp`, Apache-2.0).** A complete managed implementation of
  the `Windows.UI.Xaml` API surface. Used as: (a) a design reference for the dependency property engine, value
  precedence, layout algorithms, visual state handling, binding and template instantiation; (b) a source of behaviour
  tests: its runtime tests (`uno.ui.runtimetests.engine`) and its `SamplesApp` pages, run against the Windows oracle
  first and against our implementation later; (c) a list of what an implementation of the API can skip without
  breaking applications (Uno's "not implemented" attributes show what real apps do not need).
  **Not used as code in Wine's LGPL DLLs.** Apache-2.0 code is kept apart (separate component or reference only);
  anything adapted gets a module-level license decision first (`UWP_COMPATIBILITY_PLAN.md`, license section).
  Uno is also *not* ABI compatible with compiled UWP binaries: applications compiled against `Windows.UI.Xaml` call the
  native ABI (`IInspectable` vtables, WinRT type names, XBF2 resources), which Uno does not provide.
* **Other references already named:** XbfTools (XBF2 format knowledge), PriTools (PRI format), MSIX SDK (package
  parsing), `microsoft/calculator` (target application source).

## 5. Options and recommendation

| Option | What | For | Against |
|---|---|---|---|
| A. Native implementation of `Windows.UI.Xaml` in Wine (C, LGPL) | Our own `windows.ui.xaml.dll` with the real ABI, guided by Uno and WinUI 2, checked against the Windows oracle | Real ABI: unmodified compiled apps and the real `Microsoft.UI.Xaml` framework package run; fits Wine's license and build; no .NET dependency | Very large; every class is hand-written; behaviour must be measured |
| B. Uno hosted behind an ABI adapter | Run Uno (managed, .NET on Wine) and expose its objects through native WinRT wrappers | Reuses a finished framework | Needs .NET on Wine; Apache-2.0 component; wrapper for hundreds of classes anyway (`IInspectable`, type info, XBF2 loading); two object models, two lifetimes; debugging across the boundary |
| C. Native core, WinUI 2 controls from source on top | A is built bottom-up; controls above the property system are the open WinUI 2 sources | Smaller native surface for layer 7 where WinUI 2 provides it | Only applies to `Microsoft.UI.Xaml` controls; system controls (Button, TextBox, ...) are in layer 7 of the OS |

**Recommendation: A with C's use of WinUI 2, Uno as reference and test corpus.** B is only a fallback if A stalls at a
gate (below) and the owner accepts the .NET and license consequences. The decision between A and B follows the
prototype gate X1 (the plan in `UWP_COMPATIBILITY_PLAN.md` asks for the Uno ABI prototype as well; X1 includes it as a
time-boxed experiment so the decision is evidence, not preference).

## 6. Architecture of option A

* `windows.ui.xaml.dll` (existing, extended) exports `DllGetActivationFactory`, with a class table; classes are
  implemented in layers inside the DLL: `dp` (dependency objects and properties), `tree` (visual tree, layout), `style`
  (resources, templates, states), `bind`, `input`, `controls/*`, `render`.
* **Object model.** One C structure per runtime class with the COM interfaces generated from the metadata IDLs, a
  shared `DependencyObject` base with a property store, aggregation for classes the application derives from
  (`Control`, `Page`, `Application` are subclassed by applications: composable activation with an outer
  `IInspectable` is required from the first day).
* **Metadata.** The IDLs for the namespace come from published contracts (WinMD) written into `include/` the way the
  existing ones are; each class gets its activation factory and statics.
* **XBF2.** A loader (reader of the binary node stream into the object model) in its own source file, tested against
  `.xbf` files produced by the XAML compiler on the Windows host; XbfTools is the format reference.
* **Rendering.** Start with a software path through Direct2D/DirectWrite into the swap chain of the `CoreWindow`
  (already working) and keep the visual layer abstract enough to move to a composition tree later; rendering fidelity
  is checked by reading pixels back (screenshots do not capture composed surfaces, found in U1.1b).
* **Input.** Routed events built on the CoreWindow events that exist; focus and keyboard navigation per the oracle.
* **Threading.** One UI thread per view (the dispatcher exists), composition thread later; no work off-thread in the
  first phases.

## 7. Phases and gates

| Phase | Content | Exit gate (all measured against the Windows oracle with the same unchanged binaries) |
|---|---|---|
| X0 | Inventory and harness. Enumerate the API surface from the published WinMD; list which classes the Calculator and the sample apps activate (instrument on Windows); build the oracle apps (C++/WinRT XAML, built with the UWP workload; check it is installed on the host); decide the first IDL set. Reuse the oracle method of U1.1 (`tests/uwp/u1_5`...) | A written inventory with counts, the oracle apps built and run on Windows, expected outputs recorded |
| X1 | Core without controls. `Application`, `Window`, composable activation, `DependencyObject`/`DependencyProperty`, `UIElement`/`FrameworkElement`, `Grid`/`StackPanel`/`Canvas`/`Border`/`TextBlock`/`Rectangle`, `Window.Content`, `LoadComponent` of a trivial XBF2 page. Time-boxed Uno ABI experiment (a wrapped Uno `Button`) for the A/B decision | A one-page XAML app shows text and shapes laid out as on Windows (layout rectangles equal, pixels equal within a tolerance); the A/B decision recorded |
| X2 | Property engine completeness: value precedence, inheritance, attached properties, coercion, change callbacks, resource lookup (`StaticResource`, `ThemeResource`, merged and theme dictionaries), `Style`/`Setter`, `x:Name` fields, events from XAML | A property-engine test page passes with identical results to Windows (Uno's runtime tests used as the case list) |
| X3 | Templates and states: `ControlTemplate`, `ContentPresenter`, `ItemsPresenter`, `DataTemplate`, `VisualStateManager`, `Storyboard` and basic animations, `Button`, `CheckBox`, `RadioButton`, `ToggleSwitch`, `Slider`, `ProgressBar` with the default `generic.xaml` templates (specification: WinUI 2) | Control gallery page matches Windows in layout and in visual state changes under injected pointer and key input |
| X4 | Input and text: routed events, focus, tab navigation, `TextBox` (editing, selection, IME basics), `ScrollViewer`, pointer capture, flyouts and `Popup` | Interaction scripts (inject.exe style) produce the same event sequences and text as on Windows |
| X5 | Binding and collections: `Binding`, `x:Bind` runtime pieces, `INotifyPropertyChanged`, `ItemsControl`, `ListView`/`GridView` with virtualization, `CollectionViewSource`, `Frame`/`Page` navigation | The sample apps' data pages work; list scrolling and selection equal Windows |
| X6 | Framework packages: build `Microsoft.UI.Xaml.2.x` (WinUI 2) from source against our XAML, package it as a framework package (U1.3 mechanism), provide `vcruntime140_app` through the installed VCLibs equivalent; `NavigationView`, `TreeView`, `TabView` pages | An app using the WinUI 2 framework package runs; the `Microsoft.UI.Xaml` control tests selected from its suite pass |
| X7 | The Calculator: build from source (and, as oracle only, the installed Store package on Windows); run on Wine; keyboard and pointer: `7 + 5 =` shows `12`, history and memory, resize, theme, high contrast | The Calculator's scenario script passes; defects found are classified and fixed or listed |

Each phase ends with: oracle expectations (`expected-*.txt`) committed, Wine runs on x64 and x86, a regression pass of the
earlier U1 suites, `docs/STOCK_CHANGES.md` updated, push and merge coordinated with the other sessions.

## 8. Dependencies on other work (to schedule before or inside the phases)

* **Before X0/X1:** MRT/PRI (`ms-appx:`, `ms-resource:`, qualifiers) and the
  `resources.pri` reader, installer signature check (to run Store-signed packages), server-side compatibility gate.
* **Before X3:** Direct2D/DirectWrite and WIC conformance for what rendering uses (measure first; Wine already has
  them, the gaps are found by the oracle pages), DPI and rasterization scale from `CoreWindow`.
* **Before X4:** `Windows.UI.Text.Core` (`CoreTextEditContext`), `InputPane`, UI Automation providers for XAML
  elements (accessibility can trail but the provider interfaces are probed by controls).
* **Suspend and resume** (U1.1c) before any app that relies on `Suspending` (the Calculator does).
* **Compositor.** WineExplorer's DirectComposition and animation work (`docs/DWM_PLAN.md`) is the base for the later
  composition-driven path; XAML itself must not wait for it (X1 to X5 use the software path).

## 9. Licensing and provenance rules for this plan

* Wine DLLs stay LGPL-2.1-or-later with original code; no code is copied from Uno (Apache-2.0) into them.
* WinUI 2 (MIT) code may be built as a *separate* framework package with its notice; adapting it into a Wine DLL needs a
  module-level license note first.
* Behaviour is taken from published contracts (WinMD), documentation and the authorized Windows oracle runs;
  proprietary files and the installed Store packages stay in `../WineExplorer-local` and are never named in the
  repository beyond what the oracle method needs (as in U1.1: results and expectations only).
* The Windows reference machine is a test oracle only.

## 10. Risks and open questions

* **Size.** Layers 1 to 8 are the largest single piece of the Windows UI platform; the phases are ordered so each ends
  with something that runs, and the work can stop at any gate with a usable subset.
* **Hidden dependencies of real apps:** private interfaces used by the framework packages (WinUI 2 uses undocumented
  XAML internals in places), compiled-XAML conventions (`XamlTypeInfo` generated code), telemetry providers, `Windows.UI.Composition`
  APIs used directly by apps. X0 finds these by instrumenting the Calculator on Windows.
* **Managed apps** (C#, .NET Native) are out of scope; they need the .NET Native runtime packages and WinRT projections
  and are a separate decision.
* **Toolchain on the Windows host:** compiling XAML apps needs the VS "UWP development" workload; X0 starts by checking
  it (the oracle apps of U1.1 needed only the C++ build tools).
* **Open questions for the owner:** (1) is the Calculator still the first target, and may the Store package be used on
  the Windows host as an oracle? (2) is a .NET dependency acceptable if A stalls (option B)? (3) does a XAML-capable
  Wine need to ship before the Office and Copilot items, or after?
