<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Plan: Windows.UI.Xaml for UWP applications (started 2026-11-14: X0)

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
* **Decided by the owner (2026-11-14):** (1) the Calculator is the first target, and the installed Store Calculator may
  be run on the Windows host as a read-only oracle (observed only; none of its files enter the repository); (2) native
  only (option A): no .NET dependency; if A stalls, report and decide again (option B is not pre-approved); (3) XAML
  starts now; the Copilot and Office items run in a separate session of the owner's.
* **Found 2026-11-14:** the current Calculator (11.2607) has a C# UI compiled with .NET Native and depends on the framework
  packages `Microsoft.NET.Native.Framework.2.2`, `Microsoft.NET.Native.Runtime.2.2`, `Microsoft.UI.Xaml.2.8`,
  `Microsoft.VCLibs.140.00(.UWPDesktop)`, and on WebView2. .NET Native is machine code and needs no .NET runtime in Wine;
  its runtime DLLs come in those framework packages. **Owner (2026-11-14):** these Microsoft redistributables are
  installed through the existing setup components workflow (`scripts/wex-components.py`, Windows Components in setup),
  not shipped: a "UWP application frameworks" component downloads the pinned framework packages, shows their terms,
  verifies the hashes and registers them with `wexappx`. The UWP support in Wine itself is built in; the component only
  supplies the Microsoft frameworks. This keeps decision (2): no .NET runtime is added to Wine for XAML.
  **Built (2026-11-14, `tests/uwp/frameworks`):** component `uwp-frameworks` (enabled 2026-11-14 with three Microsoft
  licence documents chosen by the owner) installs the five from nuget.org and Visual Studio's payload catalog into `C:\Program Files\WindowsApps` and
  the machine catalog, which Wine kernelbase (eba27020) reads; a package depending on them installs and resolves them on
  Wine. Open: .NET Native's `mrt100_app.dll` needs the Windows system DLL `mrt100.dll` (`GetManagedRuntimeService`,
  undocumented), and Wine's delay-load failure path passes a wrong name (Session 0).
* **Owner (2026-11-14, revises decision 2):** Uno is allowed as the XAML engine: `zcomputerwiz/uno-uwp` may be modified
  in any way needed, and a shim of ours makes applications built against `Windows.UI.Xaml` (not against Uno) work. Uno
  (Apache-2.0) and the .NET runtime it needs are never pre-packaged: setup shows their terms and downloads and installs
  them as components. Next step: a time-boxed experiment (Uno Skia Win32 on Wine; a C++/WinRT XAML app shown through
  a small shim over Uno hosted in-process) decides between this route and option A.
* **Owner (2026-11-14), where the work goes:** change Wine where the change belongs to Windows itself (WinRT runtime,
  activation, CoreWindow, DirectX, text, storage and the like: everything that is not XAML); do the XAML work in the
  Uno-UWP component where that is the better place (controls, layout, templates, the XAML object model, XBF loading).
* **Experiment step 1 done (2026-11-14, `tests/uwp/xaml/uno-spike`):** an unchanged Uno desktop application (Skia,
  Win32 host, self-contained .NET 10) runs on Wine x64 with two Wine fixes (window class lookup, `winlangdb` and
  `GetUserLanguages`) and lays out as on Windows. x86 fails on Windows and Wine alike: Uno ships no 32-bit ICU
  (component work). Next: step 2, a C++/WinRT XAML application through a small shim over Uno in its process.
* **Step 2a done (2026-11-14):** a native C program hosts CoreCLR with hostfxr, calls an `[UnmanagedCallersOnly]`
  Uno entry point and shows runtime XAML text; the layout is the same on Windows and on Wine x64, with no further Wine
  change. Next: step 2b, a compiled C++/WinRT XAML application through a `windows.ui.xaml` shim.
* **Done:** the C++ UWP build tools (XAML compiler) are installed on the Windows host (2026-11-14, by the owner).
* **Step 2b, Windows side done (2026-11-14, `tests/uwp/xaml/cx-probe`):** the smallest compiled XAML application
  (C++/CX, `App.xaml` and `MainPage.xaml`, a `TextBlock` and a `Button`) built with the XAML compiler, signed with the
  U1 oracle certificate, run on Windows 11: page 1200x900, Title `TextBlock` 237.949x26.6016 at 24,24, Go `Button`
  76x32 at 24,63 (`expected-windows.txt`). Uno laid out the same elements as 224x28 and 84x33 (its fonts and its
  WinUI 3 Fluent styles), so the component has to bring the UWP default styles and metrics, not Uno's.

### 2b: what the compiled application asks of `Windows.UI.Xaml`, and the shim it implies

What the probe uses (class names in the binary, generated code in `Generated Files`):

* `Application`: statics `Start(callback)` (the generated `main`) and `LoadComponent(component, uri, location)`
  (`InitializeComponent` of each page, `ms-appx:///MainPage.xaml`); composable factory (`App` derives from it and
  overrides `OnLaunched`); the outer object is also the app's `IXamlMetadataProvider` (generated `XamlTypeInfo`).
* `Window` (`Current`, `Content`, `Activate`), `Controls.Frame` (`Navigate(TypeName)`: the type comes back through the
  app's metadata provider, `IXamlType.ActivateInstance`), `Controls.Page` (composable), `DispatcherTimer`,
  `IComponentConnector.Connect` on the page for `x:Name` fields and `Click` handlers.
* The pages' XAML is compiled to `MainPage.xbf` and `App.xbf` and embedded in `resources.pri` (2248 bytes for both;
  no `.xbf` file is in the package): the loader reads them as embedded data candidates of `Files/MainPage.xbf`
  through the PRI reader (U1.6 reads strings and file paths; embedded data is to be added).
* On Wine without a XAML implementation (2026-11-14, Wine 1052fab3, VCLibs DLLs beside the probe for the lab run): the
  probe starts and its `main` stops at the first activation, `RoGetActivationFactory` of
  `Windows.UI.Xaml.Application` (no library registered), then fails fast (0xc0000602). Wine's `windows.ui.xaml.dll`
  has only `ColorHelper`.

Split of the work (owner's rule: Wine for Windows itself, the component for XAML):

| Piece | Where | What |
|---|---|---|
| Activation | Wine: `windows.ui.xaml.dll` (exists) | Its `DllGetActivationFactory` loads the XAML component when one is installed: host CoreCLR through `hostfxr` (step 2a) once per process and hand the class name to the component's activation entry. Without the component it stays as it is. |
| ABI wrappers | Component (C#, generated) | One native-callable wrapper per `Windows.UI.Xaml` runtime class and interface, generated from the public metadata (`Windows.winmd`) and forwarding to the Uno object (`Microsoft.UI.Xaml` in the fork); `IInspectable`, composable aggregation with the app's outer object, overrides called back through the outer. Prior art: C#/WinRT (`ComWrappers`, authoring of WinRT components in C#, MIT) |
| XBF2 loader | Component | Reads the node stream into the Uno object tree under the component object, with connection IDs passed to `IComponentConnector.Connect`; format reference XbfTools (description only) |
| Type resolution | Component | Types from the app's `IXamlMetadataProvider` (`Frame.Navigate`, custom types in XBF) next to Uno's own |
| UWP look | Component | UWP `generic.xaml` styles and metrics (WinUI 2 is MIT and has the 2.x styles), fonts through Wine's |

**Prior art checked (2026-11-14) and what is reused:**

* XBF: **XbfKit** (MIT, NuGet, separate schemas for `Windows.UI.Xaml` and `Microsoft.UI.Xaml`, reads XBF 2.1 and
  decompiles to XAML) is used as a library; the WinUI 3 sources (MIT) have Microsoft's own readers
  (`dxaml/xcp/core/Parser`, and the standalone C# reader `dxaml/xcp/tools/XbfParser/WidgetSpinner`) as the reference
  for a direct node-stream loader later.
* WinUI 3 (`microsoft/microsoft-ui-xaml`, MIT, public and buildable since 2026): the framework itself is not usable on
  Wine (it needs the closed "lifted" composition, input and content binaries of the Windows App SDK, ~80 GB MSVC
  build); its `generic.xaml` and theme resources (close to UWP's) are the source for the UWP default styles, adapted
  back to `Windows.UI.Xaml`. WinUI 2 is on its `winui2/main` branch.
* C#/WinRT (MIT): cannot export `[exclusiveto]` interfaces or act as the inner of a composable class, so the wrappers
  are generated by our own WinmdGen from `Windows.winmd`; C#/WinRT's marshaling helpers are the reference.
* PRI from managed code: PriTools (Apache-2.0) if the component reads `resources.pri` itself.

**Step 2b on Wine done (2026-11-14, `tests/uwp/xaml/shim`):** the unmodified probe runs on Wine x64 with its
`Windows.UI.Xaml` classes served by Uno in its process: `Application.Start`, the application's `App` and `MainPage` as
outer objects over the shim's `Application` and `Page`, `LoadComponent` of the compiled page (XbfKit, Uno
`XamlReader`, the application's `Connect`), `Frame.Navigate` through the application's metadata provider, `Loaded`,
`DispatcherTimer`, `Exit`. Layout: `TextBlock` 248x28 and `Button` 84x33 at the same positions as on Windows (237.9x26.6
and 76x32 there): fonts and styles differ, as expected. CoreCLR starts in 0.2 to 0.4 s. **This answers the
experiment's question for the Uno route: it works.** Generated so far: 147 interfaces, 910 methods for 19 classes.

**Licensing, decided by the owner (2026-11-14): permissive, in the fork.** The managed component (WinmdGen, XamlShim)
links Uno, so it is Apache-2.0 like Uno and lives in github.com/zcomputerwiz/uno-uwp, branch `wex/xamlshim`, folder
`wex/` (own `Directory.Build.props` and `.editorconfig` so the repository's build and analyzer settings do not apply;
it builds against the released Uno.Sdk until Uno itself needs changes). Keeping it GPL here would have made the
combination GPL-3.0. The native proxy (to become part of Wine's `windows.ui.xaml.dll`) only loads the component
through `hostfxr` and stays in WineExplorer and Wine under their licences; the test applications and lab scripts stay
here.

**After 2b (2026-11-14):** compiled pages come from `resources.pri` (PriFormat from PriTools, Apache-2.0, in the fork:
666 bytes of embedded XBF for `MainPage`); the probe gained `SecondPage` and navigates to it. Lifetimes measured on
Windows: after navigating away, `MainPage` (whose `Loaded` lambda and `Click` handler capture `this`) is not destroyed;
C++/CX handlers that capture `this` form a cycle with the XAML objects that nothing collects in a C++ application. The
shim's simple rule (it holds every outer object) gives the same observable result for such pages; releasing pages
without cycles, and cycle collection for managed applications (reference tracking, `IReferenceTracker`; .NET's
`ComWrappers` tracker support and WinUI's MIT sources as prior art), is deferred until an application needs it. Wine
matches the Windows sequence (back stack depth 1, `MainPage` alive); the second page's `TextBlock` measures 132 wide
against Windows' 80.35 (fonts and styles item).

**Start-up through CoreApplication (2026-11-14):** `Application.Start` now does what Windows' XAML does: it runs
`CoreApplication.Run` with the shim's own view source (uno-uwp `wex/XamlShim/CoreStart.cs`). Wine (U1.1) makes the
view's STA thread and its CoreWindow, calls `Initialize` (the shim subscribes `CoreApplicationView.Activated`; the
handler's IID `cf193a96-eb13-5e3b-8bdf-87b6efae8339` is computed from its pinterface signature and equals the SDK's),
`SetWindow`, `Load`, raises the launch activation (kind Launch) and calls `Run`, in which the Uno host runs. The
application's `OnLaunched` gets Wine's real launch arguments. Uno's window (class `UnoPlatformRegularWindow`, made
hidden when the `Window` is created) becomes a child of the CoreWindow before it is shown, so the first layout is at the
CoreWindow's size: the page is 1200x900, as on Windows (the lab's virtual screen must be large enough: at 1280x800
Wine fits the window to 1200x778). Without a CoreApplication (`Run` refused) the old path is kept.

**Suspend and resume (2026-11-14):** `Application.Suspending` and `Resuming` are raised from `CoreApplication`'s events
(handler IIDs computed from their signatures, equal to the SDK's: `338579bf-1a35-5cc4-a622-a6f384fd892c`,
`c50898f6-c536-5f47-8583-8b2c2438a13b`), with sender and arguments as received. Wine's CoreWindow raises its events
(visibility, hence the lifecycle) from its dispatcher's `ProcessEvents`, which Windows' XAML uses as its message loop;
Uno's host runs a `GetMessage` loop of its own, but its dispatcher only posts messages to a window of its own, which
any loop serves; so once the application has launched, a Uno action enters `ProcessEvents(ProcessUntilQuit)` and from
then on Wine's dispatcher loop runs the thread (Uno's messages included) and wakes for the CoreWindow's events, as on
Windows; `Application.Exit` posts `WM_QUIT` to end it, then Uno's loop returns (first a 50 ms polling timer was used;
replaced the same day). Measured with the probe: Windows (`plm.exe` suspend and resume) and Wine
(the CoreWindow minimized and restored) both log "suspending, sender is the application no, deadline in 4 s" and
"resuming, sender is the application no". Found on the way: Wine has no `Windows.Globalization.Calendar`
(`REGDB_E_CLASSNOTREG`); the Calculator's date calculation needs it (Wine side, to do).

**In Wine (2026-11-14, Wine 51c29e50, on `development`):** `windows.ui.xaml.dll` serves the classes it does not
implement from the installed component (`HKLM\Software\Wine\Xaml` `ComponentPath`; `docs/STOCK_CHANGES.md`); the lab
proxy is no longer used. The probe, suspend and resume included, gives the same output through it; Wine's
`windows.ui.xaml` and `combase` tests pass on x64 and x86 without a component. Setup step: install the component's
directory, write `ComponentPath`, and register `XamlShim.exe --classes FILE` (only `Windows.UI.Xaml.*`) to
`windows.ui.xaml.dll`.

**Setup component (2026-11-14):** the owner chose a GitHub release of the fork as the download source:
`zcomputerwiz/uno-uwp` release `wex-xamlshim-v0.1.1` (pre-release; 0.1.0 is superseded, its zip had `\` entry names).
Assets: `xamlshim-0.1.1-win-x64.zip` (self-contained, no debug symbols, `classes.txt`, `LICENSE.txt`, `NOTICE.txt`
with the licences of all 62 bundled packages plus ICU's and Uno's), `TERMS.txt` (Apache-2.0 then the notices). In
`scripts/wex-components.py` a new component `uwp-xaml` with execution policy `uwp-xaml-extract-register-v1`: terms as a
pinned UTF-8 text document; the pinned zip downloaded into the root cache; every member checked (under `XamlShim/`, no
`..`, no links, size limits, the required files, a valid `Windows.UI.Xaml.*` class list) and unpacked into the machine
root's `Program Files\WineExplorer\Xaml\x64` by rename; one `reg import` writes `ComponentPath` and the classes into
the machine hive; a receipt lists every file with size and digest and every class; `is_installed` checks the receipt,
the files and the hive (read as text, no Wine); `--uninstall uwp-xaml` removes exactly those. Tests:
`scripts/test_component_uwp_xaml.py` (9, synthetic); with the real zip and Wine in the lab, the install steps leave a
hive that the check accepts and the probe runs from that installation (suspend and resume included).

**UWP look (2026-11-14, uno-uwp 697805c2):** the component uses the UWP system styles, which WinUI 3 still carries as
the framework's own `generic.xaml` (MIT; `ButtonPadding` 8,4,8,5, border 2: the Windows button), merged after Uno's
Fluent resources, and Selawik (Microsoft, OFL-1.1), the open metric-compatible stand-in for Segoe UI, as the default
font. Probe on Wine: button 76x32 at 24,63 (Windows: the same), title 239x27 (Windows 237.949x26.6016; Uno rounds to
pixels), page 1200x900. Open: the default style for `ContentPresenter` (Left/Top) is left out: merged into the
application resources these default styles act as implicit styles and reach Uno's own window chrome; the fix is to
register them as default styles in the fork. The second page's `TextBlock` with a 24 margin reports 129 at `Loaded`
(Windows 80.35): Uno gives the desired size, margin included, before the first arrange (fork, layout). Applications
built with WinUI 2 (the Calculator) bring their own styles in their framework package, which the XBF loader reads.

**Common controls (2026-11-14, uno-uwp after 52bf7255):** the probe's `ControlsPage` (Grid with fixed, auto and star
rows and columns, Border with a corner radius, TextBox, CheckBox, Image, ListView filled through `Items`, ScrollViewer
around a StackPanel filled through `Children`, `ChangeView`) gives on Wine the layout Windows gives (every rectangle
equal; the border's text 69x19 against 68.17x18.62, whole pixels in Uno). What it took: 11 more generated classes;
`IVector<T>`/`IIterable<T>`/`IObservableVector<T>` written by hand over Uno's lists (IIDs from the pinterface
signatures; `GetView`, `First` and `VectorChanged` still to do); boxing through Wine's `PropertyValue`; default
construction and composition for every class (the Uno class of the same name); and default bodies for every property
accessor (948 generated), which read or write the Uno object's property of the same name, converted by ABI kind
(string, bool, number, enum, object). Then (uno-uwp after 76eac33b): struct properties (47 accessors) through a
converter between the generated ABI structs and Uno's structs of the same shape (members by name; a matching
constructor where Uno's struct is read-only); events (225 accessors) through the Uno event of the same name, the
application's handler invoked with sender and arguments as their shims; a shim for any Uno object by its type name
(event arguments included); and `IInspectable` as a block of its own: on Windows an element's `IInspectable` (an event's
sender, `OriginalSource`) is not the pointer of its default interface, and C++/CX compares those pointers directly
(the probe's "sender is the check box: no" on both now). The probe's `TextChanged`, `Unchecked`, `Padding` and
`CornerRadius` give the Windows results.

Open points that step 2b (Wine side) settles: whether CoreCLR starts cleanly inside the UWP process that Wine's
launcher made (package identity, `ms-appx` paths); the cost of starting the runtime (time to first frame);
and, for the Calculator, a .NET Native application, whether its runtime (`mrt100_app`) and CoreCLR live together in
one process. Next: the activation hand-off and the wrappers for the probe's eight classes, then the XBF loader.
