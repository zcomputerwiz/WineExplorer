<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# UWP Binary Compatibility for WineExplorer

**Status:** Proposed implementation plan — research and repository inspection, not a claim of working UWP support  
**Prepared:** 2026-11-05  
**Target:** Existing, unmodified, packaged UWP applications running under the project's redistributable Wine build on Linux  
**Primary repositories:** [WineExplorer](https://github.com/zcomputerwiz/WineExplorer) and [zcomputerwiz/wine](https://github.com/zcomputerwiz/wine)

## 1. Goals and constraints

### Required

1. Run **existing Windows UWP binaries without rebuilding or modifying the application**, starting with an intentionally simple native C++ UWP test package.
2. Keep the shipped runtime, installer, and framework implementation composed of **lawfully redistributable, appropriately licensed open-source components**. Do **not** require a Windows ISO, extracted Windows system DLLs, Microsoft Store runtime payloads, or proprietary Windows assets to run the distributed product.
3. Extend **zcomputerwiz/wine**, not rebase on a separate UWP Wine fork. Integrate application installation and user experience into **WineExplorer** while preserving Wine's own LGPL-licensed runtime boundary.
4. Preserve existing Win32 applications, desktop/session behavior, WineExplorer's X11/Wayland work, WoW64 operation, graphics behavior, and security behavior. Do not make unrelated features regress in order to pass a UWP test.
5. Make **Windows 11 the behavioral oracle**, using agents' Windows 11 host to build and run reference applications, inspect installed packages, and collect supported API/ABI results. Separate results **observed on Windows** from results **observed on Wine**.
6. Once the project-built native UWP GUI test app passes on Wine, **attempt the actual installed Microsoft Windows Calculator app next**; report its package/runtime requirements, observed launch behavior, and any blockers. The Calculator milestone is not fulfilled by Uno Calculator or a rebuilt sample.

### Non-goals for initial delivery

- Full Microsoft Store integration, purchasing, DRM, licensing bypasses, or execution of encrypted packages.
- Broad arbitrary UWP compatibility before a minimal app works.
- Bundling or obtaining proprietary Microsoft system DLLs as a shipping shortcut.
- Pretending that WinUI 2 replaces Windows.UI.Xaml, or that Uno is already ABI-compatible with compiled UWP applications.
- Making all legacy .NET Native applications work as a prerequisite for the native C++ proof of concept.
- Disabling sandbox/security features to convert a failure into a reported pass.

### Licensing and provenance rules

- **Wine runtime:** favor Wine's existing LGPL-2.1-or-later code and compatible original additions. Do not silently mix GPL-only or Apache-2.0 code into LGPL-2.1-licensed Wine DLLs. Obtain a module-specific license review before any integration with different licenses.
- **WineExplorer:** honor its GPL-2.0-or-later project license, original notices, and the asset/source provenance policy. A separately distributed or dynamically linked component still needs an actual license review; merely separating filenames does not automatically settle compatibility.
- **Microsoft open source:** source explicitly released under an applicable open-source license (for example, MIT) may be evaluated and reused according to that license. *Do not assume* that the same license covers Windows-installed binaries, Store packages, SDK redistributables, fonts, or OS DLLs.
- **Apache-2.0 projects:** Uno, XbfTools, and PriTools may be useful as separately reviewed components or design references. Apache-2.0 is not directly GPLv2-only compatible; take care with the Wine LGPL-2.1 boundary and with combined-work obligations.
- **Windows reference machine:** Windows-installed components can serve as local test oracles only to the extent permitted by applicable licenses and law. Do not commit, distribute, attach to public CI artifacts, or automatically transfer proprietary packages and Windows components. Before copying a Store/inbox package from Windows to Linux for testing, verify authority to do so; if uncertain, do not copy it. Use independently built, redistributable fixtures instead.
- **Research/provenance:** public API documentation, published WinMD contracts, behavior recorded by authorized tests, and permissively licensed source are preferable. Avoid transplanting code derived from reverse-engineered proprietary implementations without provenance and legal review. Record original repository, revision, file, license, authorship/notice, adaptation, and test evidence for every source import.
- **Distribution gate:** build from a clean checkout with no locally harvested Microsoft binaries; audit package assets, notices, linked dependencies, build-time and runtime downloads, and installer output.

## 2. Existing project baseline — reuse before implementing

The work belongs in the existing repositories:

- [zcomputerwiz/WineExplorer](https://github.com/zcomputerwiz/WineExplorer): desktop shell, application UX, packaging and isolated application test harness. Read [README](https://github.com/zcomputerwiz/WineExplorer/blob/master/README.md), [architecture](https://github.com/zcomputerwiz/WineExplorer/blob/master/docs/ARCHITECTURE.md), [test harness](https://github.com/zcomputerwiz/WineExplorer/blob/master/docs/APP_TEST_HARNESS.md), [stock changes](https://github.com/zcomputerwiz/WineExplorer/blob/master/docs/STOCK_CHANGES.md), and [license policy](https://github.com/zcomputerwiz/WineExplorer/blob/master/LICENSE.md).
- [zcomputerwiz/wine](https://github.com/zcomputerwiz/wine): Wine runtime and the primary implementation base. Relevant paths include `dlls/combase/roapi.c`, `dlls/windows.applicationmodel/`, `dlls/windows.ui/`, `dlls/windows.ui.xaml/`, `dlls/windows.storage/`, `dlls/appxdeploymentclient/`, `dlls/dcomp/`, and `dlls/dxgi/`.
- **Branch discipline:** WineExplorer's `.gitmodules` names the `development` branch; its `master` submodule reference was `14e252dc8836b4b81750d6d220d67911e78efdc0` when inspected on 2026-11-05. The Wine repository's `master` branch is not equivalent to that tested development state. At kickoff, record the actual WineExplorer commit, submodule gitlink, Wine branch/HEAD, unmerged topic branches, and the build used for every test; do **not** use the latest `master` or a document's historical build pin by assumption.
- **Existing graphics work:** WineExplorer documents significant DirectComposition/animation and compositor changes, including native-Windows comparison probes. Reverify the implementation at the **selected** pinned commit rather than inferring it from a recent document. Also preserve ongoing Chromium/Electron and other graphics work.
- **WoW64 requirement:** keep `--enable-archs=i386,x86_64` and regression tests for 32- and 64-bit programs. Initial UWP fixture may be x64, but it must not force an x64-only distribution.

Proposed ownership: put ABI, package process identity, and fundamental WinRT runtime behavior in Wine; keep installer UI, app registration UX, shortcuts, and diagnostics in WineExplorer. Keep independent parsers/compilers and their own licenses outside Wine until their linkage has been reviewed.

## 3. Source and prior-art inventory

### Implementation candidates

| Project | Repository / reference | License / status | Proposed use |
|---|---|---|---|
| Existing Wine fork | [zcomputerwiz/wine](https://github.com/zcomputerwiz/wine) | Wine LGPL-2.1-or-later | **Primary runtime implementation**. Extend current WinRT activation, COM, package model, DComp, CoreWindow. |
| WineExplorer | [zcomputerwiz/WineExplorer](https://github.com/zcomputerwiz/WineExplorer) | Project GPL-2.0-or-later; mixed provenance by component | **Primary integration/test project**. Reuse isolated harness, installer/shell UX, source policy. |
| Wine upstream | [wine-mirror/wine](https://github.com/wine-mirror/wine) | Wine LGPL | Check current implementations before writing new code; preserve upstreamability. |
| MSIX SDK | [microsoft/msix-packaging](https://github.com/microsoft/msix-packaging) | MIT project; inspect bundled dependencies | Preferred parser/unpacker for `.appx`, `.msix`, and bundles; does **not** implement the whole Windows installed-package model. |
| C++/WinRT | [microsoft/cppwinrt](https://github.com/microsoft/cppwinrt) | MIT | Build native fixtures and generate reliable WinRT interface/projection code; not an OS runtime. |
| C#/WinRT | [microsoft/CsWinRT](https://github.com/microsoft/CsWinRT) | MIT | Candidate marshaling/projection infrastructure **if** a managed Uno backend is selected. |
| WinUI 2 | [microsoft/microsoft-ui-xaml — winui2/main](https://github.com/microsoft/microsoft-ui-xaml/tree/winui2/main) | MIT repository; inspect dependencies | Source for UWP-specific native controls, resource behavior and WinUI 2 semantics; **not** a complete `Windows.UI.Xaml.dll` replacement. |
| WinUI 3 / framework sources | [microsoft/microsoft-ui-xaml](https://github.com/microsoft/microsoft-ui-xaml) | MIT repository; inspect dependencies | Native XAML internals and test/reference techniques; do not conflate WinUI 3 ABI with legacy UWP XAML. |
| Uno | [unoplatform/uno](https://github.com/unoplatform/uno) | Apache-2.0 | Candidate **separate** cross-platform XAML backend, subject to native ABI, XBF and licensing prototype gates. |
| XBF tools | [chausner/XbfTools](https://github.com/chausner/XbfTools) | Apache-2.0; incomplete XBF2 decoding | XBF2 parser reference and experimental decompiler. Do not assume byte-for-byte round-trip or support for all XBF. |
| PRI tools | [chausner/PriTools](https://github.com/chausner/PriTools) | Apache-2.0 | PRI format reference and inspection tooling; API designed mainly for reading. |
| Uno Runtime Tests Engine | [unoplatform/uno.ui.runtimetests.engine](https://github.com/unoplatform/uno.ui.runtimetests.engine) | Review package and source licenses | Cross-runtime behavior tests/reference; optional, not a dependency of the Wine runtime. |
| Original Calculator source | [microsoft/calculator](https://github.com/microsoft/calculator) | MIT repository; installed package has separate terms | Source-level reference / optionally source-built fixture. Real installed Microsoft Calculator is a **separate acceptance target**. |

### Architecture-only references (do not import wholesale)

- [designgears/wine-uwp](https://github.com/designgears/wine-uwp): prior art for packaged UWP startup, activation, composition, and tracing. **Do not rebase or copy unreviewed code**, and do not adopt its requirement to harvest proprietary Windows system DLLs.
- [jungwuk-ryu/switchyard-wine](https://github.com/jungwuk-ryu/switchyard-wine): package store and transaction ideas. Its documented scope is signed full-trust packaged desktop apps, **not** complete UWP/AppContainer support.
- [microsoft/WindowsAppSDK](https://github.com/microsoft/WindowsAppSDK): WinUI 3 and package dependency architecture/specifications; distinguish desktop WinAppSDK from UWP.
- [microsoft/xlang](https://github.com/microsoft/xlang): WinRT metadata and language projection concepts.
- [microsoft/windows-rs](https://github.com/microsoft/windows-rs): optional Rust WinRT/COM probe tooling.
- [wine-staging/wine-staging](https://github.com/wine-staging/wine-staging): prior patches and contract tests to review individually for provenance and license compatibility.

**Research rule:** before any new module, search current Wine, Wine-Staging, the project's topic branches, and the licensed reference projects. Log why existing code was inadequate.

## 4. Work phases and dependency gates

Every phase requires (a) pinned source revisions, (b) actual build/run logs or static-analysis results explicitly labeled as such, (c) accepted results from Windows when applicable, (d) a regression summary, and (e) documented blockers. Do not record “tested” merely because a proposed test exists.

### Phase 0 — Baseline, architecture inventory, and legal provenance

**Purpose:** avoid duplicating capabilities or destabilizing WineExplorer.

**Work**

- [ ] Record exact Git SHAs: WineExplorer, Wine submodule, all relevant topic branches and any out-of-tree assembly overlays; reconcile the pinned build documentation with current branches.
- [ ] Build both Wine architectures and run existing WineExplorer smoke tests (shell, Win32, 32-bit installer, graphical app, compositor/DComp, prefix isolation).
- [ ] Inventory implemented versus stubbed APIs for `combase`, `windows.applicationmodel`, `windows.ui`, `windows.ui.xaml`, `windows.storage`, `appxdeploymentclient`, `dcomp`, `dxgi`, `kernelbase`, `ntdll`, and relevant `user32/win32u` behavior. Record class names, IIDs, methods, status, tests.
- [ ] Review the MSIX SDK, WinUI 2, Uno, XbfTools and PriTools licenses **including dependencies**, and document permissible integration models.
- [ ] Create `docs/uwp/` in WineExplorer (or another approved directory) for contracts, source provenance, feature matrix, oracle observations, and test artifacts. Do not commit local Windows packages.
- [ ] Define a feature flag / installed-package opt-in so incomplete UWP support cannot disrupt ordinary Win32 execution.

**Exit gate:** reproducible baseline and an evidence-backed gap matrix; no production changes until source/license boundaries are approved.

### Phase 1 — Windows 11 oracle and self-owned native UWP fixtures

**Purpose:** establish authoritative behavior using the agents' Windows 11 host, without Microsoft system binaries in the deliverable.

**Work**

- [ ] Inventory Windows 11 version/build, SDK, Visual Studio/C++ UWP workload, available signing/developer-mode tooling, architecture, and test accounts. Do not assume every host has the C++ UWP workload installed.
- [ ] Create a **minimal packaged native C++/WinRT UWP** test fixture with no managed/.NET Native requirement. Preserve source and build instructions under the project's chosen GPL/LGPL-compatible license.
- [ ] Fixture A (nonvisual): packaged identity, `RoInitialize`, activation factory retrieval, `IInspectable`/`QueryInterface`, string marshaling, async operation, app launch activation, event dispatch, clean shutdown.
- [ ] Fixture B (GUI): `Application.Start`/CoreApplication pathway, `CoreWindow`, `App.xaml`, `MainPage.xaml`, `TextBlock`, `Button`, a click counter, basic keyboard/pointer events, one style/resource dictionary, and explicit `InitializeComponent`/`LoadComponent` use. Add ordinary `Binding`, then a separate `x:Bind` case to localize failures.
- [ ] Build x64 first, then at least one 32-bit fixture for regression. Produce both debug traces and ordinary optimized/release build artifacts; inspect generated XBF and PRI.
- [ ] Run both fixtures **on Windows 11**, capturing package manifest, package dependencies, module/import list, activation trace, WinRT ABI results, event order, and screenshots. Pin builds and hashes.
- [ ] Locate installed Calculator with PowerShell, e.g. `Get-AppxPackage -Name Microsoft.WindowsCalculator`; record its `Version`, `Architecture`, `PackageFullName`, `Dependencies`, `InstallLocation`, and manifest metadata. Run the installed app on Windows and preserve an approved oracle baseline. **Do not transfer or distribute its files at this stage.**

**Exit gate:** purpose-built fixtures pass on Windows; tests are deterministic and self-owned; installed Calculator is classified without assuming its current runtime dependencies.

### Phase 2 — Redistributable package inspection, staging, and identity

**Purpose:** open legal APPX/MSIX packages without a Microsoft Store or Windows image.

**Work**

- [ ] Prototype `microsoft/msix-packaging` on Linux; measure which signature, archive, bundle, manifest, architecture, language/resource, and block-map cases it supports. Verify dependency notices and security maintenance of bundled libraries.
- [ ] Implement per-prefix package store with atomic install/rollback, full name and family name, dependency graph, architecture selection, file registration, resource paths, and uninstall. Keep its schema versioned and tested.
- [ ] Extend Wine's existing `GetCurrentPackage*` and `Windows.ApplicationModel.Package` behavior rather than duplicating implementations in a new shell module.
- [ ] Register WinRT `ActivatableClassId` and package-private classes from supported manifest declarations; integrate with Wine's activation context and class lookup.
- [ ] Adopt a clear signature policy. Untrusted packages cannot be described as signature-verified unless chain and block-map checks actually run; enable local unsigned **test fixtures** only via explicit development settings.
- [ ] Test malformed packages, path traversal, decompression bombs, dependency cycles, conflicting versions, rollback and fresh-prefix install/uninstall.

**Exit gate:** reproducible native package installation and identity queries on Wine, without a harvested Windows runtime.

### Phase 3 — Native WinRT ABI, process model, and activation

**Purpose:** allow an existing PE executable to reach its own startup code using standard WinRT ABI interfaces.

**Work**

- [ ] Extend `combase`/WinRT activation (`RoInitialize`, `RoGetActivationFactory`, `RoActivateInstance`, `DllGetActivationFactory`), COM apartment/ASTA requirements, HSTRING, `IInspectable`, agile/weak references, and event/async marshaling as demanded by the fixture.
- [ ] Use WinMD metadata and C++/WinRT to generate **tests and interface definitions**, not to replace Wine's native runtime wholesale.
- [ ] Add manifest-driven packaged process context, dependency DLL search, application activation arguments, lifecycle callbacks and dispatcher/core-window startup.
- [ ] Extend existing `windows.ui`/`CoreWindow` and `Windows.ApplicationModel.Core` behavior; make native Windows oracle tests for all changes in observably specified HRESULT, threading, lifetime and callback ordering.
- [ ] Preserve sandbox/security semantics. Record unsupported AppContainer/token/broker behavior honestly and fail closed where a security claim would otherwise be false.
- [ ] Run Wine `combase`, apartment, security and API tests on x86 and x64; test non-UWP COM clients for regressions.

**Exit gate:** Fixture A enters its own code and completes packaged activation and clean shutdown on Wine without proprietary OS DLLs.

### Phase 4 — Native UWP lifecycle without XAML

**Purpose:** demonstrate true unmodified packaged execution before introducing a UI framework.

**Work**

- [ ] Run the **same built fixture A executable and package bits** on Windows 11 and Wine. Do not relink or rebuild specifically for Wine.
- [ ] Exercise lifecycle callbacks, async completion, package-qualified resource lookup, user-data storage, multiple launches, termination and restart.
- [ ] Validate process lifetime, thread/apartment identity, result codes, packages, module list, and no leaked registry/installed-state modifications after uninstall.
- [ ] Capture load/activation faults into a machine-readable missing-API report; reject workarounds that disable safety or alter the application's binary.

**Exit gate:** unmodified native nonvisual UWP package passes its functional test matrix on Wine and Windows.

### Phase 5 — Select and prototype an open-source XAML backend

**Purpose:** prove an ABI-correct XAML implementation can be built and distributed.

**Parallel investigations**

1. **Uno-backed candidate:** build Uno's Windows-compatible rendering host under Wine; prototype a *real native WinRT ABI wrapper* over a Uno `Button`, including `IInspectable`, class/interface IDs, factory, reference identity/lifetime, dependency property access, event callbacks and UI-thread affinity. Evaluate `microsoft/CsWinRT` for interop rather than inventing marshaling. A managed `Button` alone is not proof of UWP binary compatibility.
2. **Native-backed candidate:** assess `microsoft-ui-xaml` **WinUI 2** MIT source for reusable controls and native object patterns. Enumerate its dependencies on the Windows system `Windows.UI.Xaml`; do **not** misclassify WinUI 2 as a standalone system-XAML implementation.
3. **Graphics integration:** reuse current WineExplorer `dcomp`/DXGI/renderer behavior and existing X11/Wayland integration. Demonstrate one retained visual tree in an owned Wine window; preserve the existing compositor and Chromium regressions.

**Decision criteria**

- Native UWP ABI, activation factory and interface identity correct against WinMD.
- Runtime-created control behaves correctly for dependency properties, events, bindings and lifetime.
- ABI adapter implementable without pervasive custom forks of Uno.
- Clearly distributable under a reviewed license arrangement.
- Executes and renders under the existing WineExplorer build and both relevant graphical backends.
- Performance/maintenance burden acceptable; missing APIs measurable.

**Exit gate:** choose a backend **based on a functioning native ABI-to-control prototype**; document why alternatives were rejected. If no backend meets this gate, stop and scope a smaller native XAML subset rather than claim compatibility.

### Phase 6 — PRI/XBF and compiled XAML semantics

**Purpose:** consume resources from an original Microsoft-toolchain-built UWP package, without recompiling its markup.

**Work**

- [ ] Use [PriTools](https://github.com/chausner/PriTools) and [XbfTools](https://github.com/chausner/XbfTools) for format knowledge and standalone inspection; verify XBF version detection and known parser limitations.
- [ ] Implement or adapt a licensing-compatible resource reader at a documented module boundary. Handle `ms-appx:` URI resolution, PRI qualifiers, resource lookup, themes and `Generic.xaml`.
- [ ] Implement `Application.LoadComponent` contract: find compiled resources, build objects with exact class/type mapping, initialize namescopes and resources, connect generated code-behind callbacks and bindings.
- [ ] Treat `x:Bind` generated code, application-defined types, and XBF-to-object conversion as independent compatibility features. Do not assume text XAML reconstruction preserves compiled metadata.
- [ ] Differential-test `InitializeComponent`, resource lookup, namescope order, bindings, style lookup, template instantiation and event hookup on Windows and Wine.
- [ ] Add negative tests for corrupt, unsupported or adversarial XBF/PRI data.

**Exit gate:** the **original compiled** UI resources in Fixture B load and bind correctly under Wine.

### Phase 7 — Unmodified native UWP GUI fixture: acceptance gate

**Purpose:** make the packaged GUI test pass end to end *before trying Calculator on Wine*.

**Work**

- [ ] Run untouched Fixture B build on Wine using its original packaged executable, metadata, XBF/PRI and code-behind.
- [ ] Verify initial pixels, button click updates, keyboard input, focus, resizing, DPI, resource lookup and clean closure in WineExplorer's X11 test session; test Wayland as supported by the pinned compositor.
- [ ] Use the existing [WineExplorer application harness](https://github.com/zcomputerwiz/WineExplorer/blob/master/docs/APP_TEST_HARNESS.md): isolated prefix/display, explicit process/window identity, deterministic fixture, screenshot of actual app content, API traces, and independently checked cleanup.
- [ ] Compare API-level outcomes and UI behavior against the Windows 11 oracle. Do not use merely a visible splash screen or a process existing as passing evidence.
- [ ] Run both architecture checks and surrounding Win32/graphics regressions; document remaining platform-specific failures separately.

**Hard gate:** **no Microsoft Calculator execution attempt under Wine is counted as an acceptance test until Fixture B is demonstrably working.** Windows-side Calculator inventory and reference runs from Phase 1 can proceed earlier.

### Phase 8 — Real Microsoft Windows Calculator (explicit next application test)

**Purpose:** after the built fixture passes, test a meaningful, pre-existing Microsoft UWP application rather than another port.

**Required sequence**

1. **On the agents' Windows 11 host**, find and open the *installed* `Microsoft.WindowsCalculator` package. Capture version, architecture, manifest, dependency package names/versions, binaries' managed/native form, resources, launch pathway and a screenshot of Standard mode performing a known calculation. Example inventory:

   ```powershell
   $calc = Get-AppxPackage -Name Microsoft.WindowsCalculator
   $calc | Select-Object Name, Version, Architecture, PackageFullName, PackageFamilyName, InstallLocation, Dependencies
   if ($calc) { Get-AppxPackageManifest -Package $calc.PackageFullName | Out-File -Encoding utf8 .\calculator-manifest.local.xml }
   ```

   Save this output as **private oracle evidence**, not as a public release artifact. Do not assume all Windows 11 Calculator versions have identical dependencies or that the current installed package is C++-only.
2. **Acquire a testable package by an authorized route.** Confirm rights before exporting/transferring a Windows-installed Microsoft binary. **Do not** copy installed Microsoft runtime DLLs, framework packages, extracted Windows files, or protected Store assets into the project or public CI. A source-built Calculator from [microsoft/calculator](https://github.com/microsoft/calculator) is a useful *fallback development fixture*, but it must be labeled separately and **does not satisfy** the “actual installed Microsoft Calculator” acceptance criterion.
3. If legally available to the test environment, install the **unchanged original Calculator package** in an isolated Wine prefix. Preserve and report exact package version and SHA-256; compare against the version inventoried on Windows.
4. Attempt launch. Even an early failure is useful: capture the first unsupported activation, import, interface, PRI/XBF resource, WinUI 2, VCLibs, broker, `.NET Native` or other dependency. Classify precisely rather than stubbing every missing method with success.
5. **When runtime dependencies permit**, exercise Standard mode: launch, enter `7 + 5 =`, verify `12` in the actual window, keyboard and pointer input, resize, theme/resources, history/memory if available, and clean exit/relaunch. Capture screenshot plus window/process identity; repeat to detect startup races.
6. Produce a clear verdict: **Pass / Partial (first failing feature) / Blocked by unavailable legally redistributable dependency / Not tested (rights or environment)**. Never mark a launch or splash screen alone as a functional Calculator pass.

**Critical contingency:** Microsoft documents historical Calculator packages with dependencies on `Microsoft.UI.Xaml.2.8`, `Microsoft.NET.Native.Framework.2.2`, `Microsoft.NET.Native.Runtime.2.2`, and VCLibs. The **current host's** dependency inventory is authoritative. If the tested Calculator requires proprietary runtime packages with no redistributable open replacement, this test may be **blocked** despite successful native C++ fixture support. Do not harvest those packages as a workaround; open a separate capability track and proceed with other lawful test applications.

**Exit gate:** publish a traceable Calculator result with exact dependencies, screenshot/evidence, and honest limitation or demonstrated arithmetic functionality. A blocked result remains a blocker, not a passed milestone.

### Phase 9 — Wider compatibility, hardening, distribution

**Work**

- [ ] Expand to additional lawfully obtained native UWP apps and then to WinUI 2 controls, packaging variations and nontrivial resource graphs.
- [ ] Evaluate legacy `.NET Native` application support as its own project, after identifying a viable lawful, redistributable runtime strategy. Do not assume modern .NET, Mono, or C#/WinRT can directly replace that specialized native runtime.
- [ ] Audit security: package validation, extraction safety, path handling, signature policy, exposed WinRT brokers, process identity, permissions, AppContainer semantics, URL/protocol activation, untrusted resources and resource exhaustion.
- [ ] Run X11, Wayland, fresh-prefix, WoW64 and native Linux desktop regressions; ensure the user-facing install flow works in WineExplorer without private tools or locally harvested files.
- [ ] Produce a clean offline release build; perform dependency/SBOM/license/notice review and search artifacts for Windows system DLLs, licensed Store assets and accidental test-machine data.
- [ ] Document supported and unsupported app classes; publish reproducible self-owned fixtures and observed compatibility status.

**Exit gate:** distributable, reproducible, licensable implementation with supported-scope evidence and no proprietary Windows runtime prerequisites.

## 5. Test strategy and evidence contract

| Test | Windows 11 oracle | Wine result needed | Earliest phase |
|---|---|---|---|
| WinRT ABI probe | HRESULTs, IIDs, HSTRING, lifetime, apartment | Same documented observable contract | 3 |
| Native package identity | Name, family, path, dependencies, manifest | Correct and per-prefix isolated | 2–3 |
| Native nonvisual UWP Fixture A | Normal launch and callbacks | **Same binary** launches/completes | 4 |
| XAML/native COM control probe | Factory/QI/property/event behavior | Correct native wrapper around selected engine | 5 |
| XBF/PRI Fixture B resource loading | Namescopes, bindings, resources | Original compiled resources load | 6 |
| Native UWP GUI Fixture B | Buttons, text, input, resize, pixels | **Same unchanged packaged executable works** | 7 |
| **Installed Microsoft Calculator** | Installed `Microsoft.WindowsCalculator` and known arithmetic output | Attempt after Fixture B; report pass/partial/blocker with evidence | **8, immediately after Phase 7** |
| Existing Windows/desktop apps | Existing baseline behavior | No regression | Every phase |

Every acceptance record should include: source and commit, binary/package hashes, architecture, Windows build (for oracle), Wine/WineExplorer/submodule SHAs, runtime dependencies, prefix and compositor, exact commands, stdout/stderr/trace logs, process/window identity, captured *content* pixels, expected/actual values, repeat count, and final verdict. Mark tests **static-only**, **built but not run**, **Windows-only**, **Wine-only**, or **compared** where appropriate. Never infer pass status from a tool's earlier summary without actual artifacts.

## 6. Proposed pull-request / agent work slices

To avoid independent agents colliding in the Wine fork, assign narrow changes and use isolated branches/worktrees. Do not merge branch work merely because its tests are claimed to have run.

1. **Inventory and licensing:** baseline capability matrix, branch pinning, license/provenance ledger; documentation-only.
2. **Windows oracle/fixtures:** C++/WinRT sample sources, PowerShell inventory, Windows test logs, build hashes; no Wine edits.
3. **Package parser proof:** MSIX SDK Linux probe, manifest/dependency model, extraction validation tests; no shell integration yet.
4. **Wine packaged identity/activation:** `kernelbase`, `combase`, `windows.applicationmodel` changes and x86/x64 tests.
5. **CoreWindow/lifecycle:** event loop and app activation focused ABI slice; independent tests.
6. **XAML backend spike:** Uno vs native prototype and licensing decision memo, before committing to backend integration.
7. **XBF/PRI pipeline:** standalone parser integration and Windows comparison corpus; record formats that remain unsupported.
8. **GUI fixture acceptance:** native compiled XAML and rendering tests using existing isolation harness.
9. **Calculator acceptance:** **first external app after the GUI fixture gate**; inventory, lawful acquisition gate, runtime trace, and exact verdict.
10. **WineExplorer UI/distribution:** package management UX, desktop integration, reproducible clean package and license audit.

Each pull request needs a bounded scope, implementation rationale, test commands, actual log links, before/after evidence, provenance, and a clear list of untested paths. Prefer upstream-shaped patches in LGPL Wine modules; keep WineExplorer-specific UI out of the runtime.

## 7. Open decisions and highest risks

| Risk / question | Why it matters | Mitigation / decision gate |
|---|---|---|
| Uno native WinRT ABI mismatch | Managed WinRT-shaped classes aren't callable as native UWP interfaces | Phase 5 single-control ABI prototype before broad adoption |
| Native system XAML not independently available | WinUI 2 relies on UWP system XAML | Treat WinUI 2 as controls/reference; choose or implement genuine framework backing |
| XBF version/semantics incomplete | Decompiled XAML may not reproduce `InitializeComponent`, namescopes, or `x:Bind` | Capture actual Windows-generated XBF/PRI corpus and test original compiled resources |
| .NET Native and framework dependencies | Real Calculator may require proprietary runtime packages | Inventory actual host package first; independent legacy runtime track, no harvesting |
| Wine fork branch divergence | Git submodule pin and newer DComp changes may differ | Record pinned state; cherry-pick only verified narrow changes |
| Package signature/security | Parser output is not the same as trusted install | Explicit signature policy, strict validation, isolate untrusted packages |
| Mixed source licenses | LGPL Wine modules and Apache/GPL code cannot be indiscriminately combined | Module-level provenance/legal review, compatible alternative implementation when required |
| Behavioral compatibility | Matching visible pixels alone won't prove lifecycle/ABI behavior | Windows-vs-Wine differential tests and repeated deterministic runs |
| Windows Calculator acquisition rights | Being installed on a Windows host is not itself a redistribution license | Private Windows oracle always; Wine trial only under an authorized acquisition/test route |

## 8. References: external contracts and documentation

- [Microsoft: WinRT type system](https://learn.microsoft.com/en-us/uwp/winrt-cref/winrt-type-system)
- [Microsoft: WinMD files](https://learn.microsoft.com/en-us/uwp/winrt-cref/winmd-files)
- [Microsoft: UWP app lifecycle](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/app-lifecycle)
- [Microsoft: WinUI 2 for UWP](https://learn.microsoft.com/en-us/windows/apps/winui/winui2/)
- [Microsoft: MSIX SDK on Linux](https://learn.microsoft.com/en-us/windows/msix/msix-sdk/msix-linux)
- [Microsoft: Windows Calculator project](https://github.com/microsoft/calculator) and [its application architecture](https://github.com/microsoft/calculator/blob/main/docs/ApplicationArchitecture.md)
- [Microsoft: inspect installed Calculator package (`Get-AppxPackage`)](https://learn.microsoft.com/en-us/troubleshoot/windows-client/shell-experience/modern-inbox-store-apps-troubleshooting-guidance)
- [Uno XAML architecture and generated `x:Bind`](https://github.com/unoplatform/uno/blob/master/doc/articles/uno-development/Uno-UI-xBind-architecture.md)
- [XbfTools limitations and parser projects](https://github.com/chausner/XbfTools)
- [PriTools file format and parser](https://github.com/chausner/PriTools)

---

### First action to assign

**Phase 0 baseline + Phase 1 Windows oracle may proceed in parallel.** Produce a pinned capability/branch matrix and a buildable, self-owned native C++/WinRT UWP fixture first. Do not implement an Uno bridge or import another Wine fork until those gates establish the exact missing native APIs and the acceptable licensing boundary.
