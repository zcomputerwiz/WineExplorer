<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Second review: UWP compatibility plan

2026-11-05. Static review only: no build, Windows fixture, package transfer,
installation, Wine run, pixel check, or sandbox validation. Inspected
WineExplorer `8e6e1037e5cdad71e262baa65f07c7b9b0fd3172` and Wine
`14e252dc8836b4b81750d6d220d67911e78efdc0` (Wine working tree clean).
`INVENTORY.md:11` pins an older parent commit; its Wine pin still matches.
Paths below are repository-relative. Windows contract links are documentation
checks, not observations from this host. No external source licences were audited.

## Verdict and sequencing

Keep the smaller U1.0, with a narrower acceptance claim: **packaged process
identity and native WinRT component activation**, not yet UWP application
lifecycle compatibility. Keep CoreApplication/CoreWindow together in U1.1,
then native presentation, then XAML. Move basic MRT/PRI and framework dependency
discovery ahead of XAML: both can block a non-XAML native package.

The decision's priority below DWM/browser/system-tier work is reasonable
(`README.md:15-18`, `docs/ROADMAP.md:240-283`). Inventory and Windows fixtures
can proceed now; overlapping token/service/ACL changes need a stable base.
Completing all system-tier, ISO and compositor work is not a technical
prerequisite for a per-user, per-prefix nonvisual probe. Treat those as project
scheduling/release gates, not Windows ABI dependencies. Machine-wide deployment
and cross-user package registration must wait for their corresponding model.

Resolve the first fixture's application model **before implementing its launch
path**. A packaged full-trust console probe is useful for U1.0 but does not prove
UWP/PLM startup. A genuine native UWP `IFrameworkView` application exercises
CoreApplication and window/view creation even if it draws nothing. If that is
required at U1.0, its runtime scope is substantially larger. The prior review's
“nonvisual CoreApplication” label (`CODEX_REVIEW.md:194-206`) hides this distinction.
Do not silently substitute direct EXE execution for normal application activation.

## Earlier review: source verification and corrections

| Claim / decision | Finding in this checkout |
|---|---|
| WinRT activation exists but is incomplete (`CODEX_REVIEW.md:86-94`) | Correct, but “factory call-through” is not itself a defect: `wine/dlls/combase/roapi.c:45-119` searches activation context then machine registration; `:150-209` loads the DLL, calls its factory and QIs the requested IID; `:227-241` correctly delegates instance creation. Missing package-scoped resolution is the relevant gap. `RoInitialize` maps to ordinary COM STA/MTA (`:127-136`), which alone does not establish UWP ASTA behavior. |
| Package identity APIs “implement” identity (`CODEX_REVIEW.md:90-94`) | Too generous: **all seven inspected current/process queries are explicit `APPMODEL_ERROR_NO_PACKAGE` stubs**, `wine/dlls/kernelbase/version.c:1561-1623`. Parsing a package name elsewhere is not process identity. `RtlQueryPackageIdentity` independently returns `STATUS_NOT_FOUND`, `wine/dlls/ntdll/rtl.c:1626-1630`. The inventory's stronger classification is correct. |
| Package objects/deployment are shells | Correct: `Package.Current` allocates an object without package lookup (`wine/dlls/windows.applicationmodel/package.c:499-513`); `get_Id` is unimplemented (`:445-448`), `InstalledLocation` allocates a folder shell (`:451-466`) whose path is the EXE directory (`:323-333`), dependencies are unimplemented (`:475-478`). Deployment Add/Update/Remove/Stage/Register and queries are unimplemented (`wine/dlls/appxdeploymentclient/package.c:102-206`). Object/factory success must not count as identity/install success. |
| CoreWindow and CoreApplication are skeletal | Correct, and worse than just missing method bodies: CoreApplication QI only exposes generic factory/inspectable/agile interfaces (`wine/dlls/twinapi.appcore/core_application.c:35-54`); there is no `ICoreApplication`/`Run` implementation there. Its four factory methods return `E_NOTIMPL` (`:72-93`). CoreWindow factory methods are stubs (`wine/dlls/windows.ui/corewindow.c:81-102`); current-thread lookup returns **success with NULL** (`:120-125`). |
| Windows.UI.Xaml is not implemented | Correct for this module: only ColorHelper is declared (`wine/dlls/windows.ui.xaml/classes.idl:26-27`); its inspectable/activation methods are stubs, but `FromArgb` has real narrow functionality (`color_helper.c:80-125`). Likewise “broad stubs” is fair for `windows.ui`, not “everything is stubbed”: see `uisettings.c:113-204`, `uiviewsettings.c:84-105`, `inputpane.c:89-134`. |
| Storage and messaging provide a reusable baseline | Partial only. In-memory stream async paths exist (`wine/dlls/windows.storage/streams.c:512-561,622-675`); URI/file stream-reference creation is stubbed (`:121-143`). ApplicationData properties remain stubbed (`wine/dlls/windows.storage.applicationdata/applicationdata.c:172-264`). `coremessaging` returns a controller object, but cannot return its queue (`main.c:188-191,269-286`); its dedicated-thread factory is unimplemented (`:225-229`). Creation success is not a working dispatcher. |
| Reuse DComp/DXGI | Sound direction, but the inventory misses the creation blocker. **`CreateSwapChainForCoreWindow` is `E_NOTIMPL`** (`wine/dlls/dxgi/factory.c:329-336`). `GetCoreWindow` clears the output and returns `DXGI_ERROR_INVALID_CALL` (`swapchain.c:1308-1316`); a vtable slot proves no target support. Existing DComp visual content/commit machinery (`wine/dlls/dcomp/device.c:3281-3318,4110-4248`) does not fill that gap. |
| U1.1 vs U1.2; separate oracle repository | The prior review contradicts itself: CoreWindow is U1.2 at `CODEX_REVIEW.md:52-53`, U1.1 at `:206`. Follow the decision's U1.1 (`README.md:22`). Its third proposed repository (`CODEX_REVIEW.md:116-121`) is superseded by `../WineExplorer-local` (`README.md:45`). |

One additional correction to the inventory: do not put a default-deny gate at
the front of **all** `RoGetActivationFactory` calls (`INVENTORY.md:64-74`).
Ordinary unpackaged Wine applications already use WinRT through the existing
actctx/registry path (`wine/dlls/combase/roapi.c:56-75,166-179`). Gate creation
and use of the **new package context/resolver**, preserving existing WinRT
lookup and failure behavior. Shell policy should not become a prerequisite
for ordinary Wine WinRT APIs. Define gate changes during process lifetime;
do not let toggling the registry key erase an already running process's identity.

## Windows contracts the fixture must distinguish

1. **Identity, process attributes and token are separate observations.** A
   manifest in a directory, environment variable or registry entry is not a
   packaged process. Establish identity at supported launch/context creation,
   and compare current-process APIs with an external process-handle query,
   token queries, application ID and package graph. Record buffer-size/error
   semantics, full name versus family name, paths and x86/x64 behavior.
   `NtCreateUserProcess` handles a token attribute but no package context in its
   inspected attribute switch (`wine/dlls/ntdll/unix/process.c:723-754`).
   `RtlQueryPackageIdentity` takes a **token**, not a process handle
   (`wine/dlls/ntdll/rtl.c:1626-1630`); token security-attribute queries currently
   report an empty claim set (`ntdll/unix/security.c:707-716`). Do not invent a
   package-name process attribute or equate `SECURITY_CAPABILITIES` with full
   package identity. Exact undocumented Windows attribute/claim encodings need
   separate evidence; they are unverified here.

2. **Package identity is not AppContainer.** Packaged full-trust apps and
   unpackaged AppContainer processes prevent either from being a valid proxy
   for the other. Probe `TokenIsAppContainer`, AppContainer SID, capability SIDs,
   integrity and representative denied access separately. Windows process
   creation uses `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` for AppContainer
   ([process attributes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute),
   [AppContainer launch](https://learn.microsoft.com/en-us/windows/win32/secauthz/implementing-an-appcontainer)).
   **Reuse existing work:** this fork already consumes that attribute and creates
   a LowBox token (`wine/dlls/kernelbase/process.c:675-681,709-729`); the server
   stores the SID, low label and capabilities (`wine/server/token.c:2096-2137`),
   and token queries reach server state (`wine/dlls/ntdll/unix/security.c:640-681`).
   These are partial mechanisms, not verified host containment. “No sandbox
   claim” is appropriate; disabling token restrictions to make the UWP fixture
   pass would violate the plan's own gate (`UWP_COMPATIBILITY_PLAN.md:30,145`).

3. **Package class activation is not global COM registration or application
   launch.** Include a self-owned component DLL with a manifest-declared
   `windows.activatableClass.inProcessServer`, class ID and threading model
   ([manifest contract](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-f-inprocessserver)).
   Activate that class through its native factory; activating only an existing
   system class would miss the package resolver entirely. Test same class name
   in two packages, dependency visibility, missing class/DLL and rollback.
   Existing SxS `winrt.v1` parsing (`wine/dlls/ntdll/actctx.c:611,2370-2373`)
   is useful infrastructure, not AppxManifest deployment. Application EntryPoint,
   activation arguments and process launch need separate tests from factory lookup.
   Executable-server activation is another scope; do not imply it from an in-proc pass.

4. **CoreApplication.Run is a lifecycle contract, not RoActivateInstance.**
   The native no-XAML route supplies `IFrameworkViewSource` and receives view
   initialization, window, load, run and teardown callbacks
   ([native app model](https://learn.microsoft.com/en-us/windows/uwp/gaming/about-the-uwp-user-interface-and-directx)).
   U1.1 should measure launch activation, callback order/thread/apartment,
   CoreWindow activation, event pumping and clean termination; later add
   suspend/resume/relaunch. XAML `Application.Start` is a different framework
   entry route. A test that merely calls `RoInitialize`, awaits work and exits
   must not claim either lifecycle.

5. **CoreWindow requires actual presentation and input.** Its thread identity,
   dispatcher, bounds/DPI, focus, visibility, resize and closure must agree with
   the native view. A Direct3D fixture needs the CoreWindow DXGI target, not an
   arbitrary substitute HWND
   ([DXGI contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcorewindow)).
   Validate presented pixels and pointer/keyboard behavior on X11/Wayland.
   WinRT `Windows.UI.Composition` is a distinct API surface from desktop
   DirectComposition; record whether the fixture uses it before expanding scope.
   Existing desktop DComp success cannot certify either WinRT composition or
   the currently missing CoreWindow swapchain path.

6. **Name the dispatcher.** UWP CoreDispatcher/ASTA semantics, Windows.System
   DispatcherQueue and WinAppSDK Microsoft.UI.Dispatching.DispatcherQueue are
   distinct contracts, not interchangeable names
   ([threading differences](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/migrate-to-windows-app-sdk/guides/threading),
   [Windows.System queue](https://learn.microsoft.com/en-us/uwp/api/windows.system.dispatcherqueue)).
   Queue probes need actual enqueue/callback thread, pumping, shutdown and
   post-shutdown failure checks. An async completion on a worker thread does
   not prove UI dispatch. Require DispatcherQueue only if the chosen fixture
   calls it; CoreDispatcher is already part of the CoreWindow lifecycle slice.

7. **MRT/PRI is not XAML-only.** Native code can use ResourceLoader, and manifest
   `ms-resource:` strings need resource resolution. PRI supplies string/file
   candidates selected by context; ordinary Win32 PE resources are no replacement
   ([resource system](https://learn.microsoft.com/en-us/windows/uwp/app-resources/resource-management-system)).
   Add a small non-XAML string-resource probe before U2, using the view-independent
   loader where no view exists; that loader cannot resolve scale-qualified
   candidates ([loader contract](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.resources.resourceloader.getforviewindependentuse)).
   Test package/resource-map identity, language fallback and missing resources;
   leave compiled XAML/XBF for U2. Local `classes.idl` search found no
   ResourceLoader/ResourceManager registration; the three expected resource DLL
   directories were absent. This limited search is not proof that no related
   resource code exists anywhere in Wine.

8. **Native C++ is not dependency-free.** Freeze the generated manifest,
   imports, Release/Debug configuration, toolset and complete package graph
   before promising U1.0 runs with open components. Check VCLibs and any framework
   packages by exact name, publisher, minimum version and architecture; include
   dependency DLL/class/resource lookup and missing/wrong-version negatives.
   Desktop Bridge VCLibs and UWP VCLibs are not interchangeable packages
   ([runtime package distinctions](https://learn.microsoft.com/en-us/troubleshoot/developer/visualstudio/cpp/libraries/c-runtime-packages-desktop-bridge)).
   C++/WinRT is a projection, not a CRT or an OS runtime. Pin a minimal lawful
   runtime closure for the self-owned fixture; if one cannot be established,
   report blocked rather than silently copying the host's framework packages.

## Uno decision and licence evidence

The authoritative owner decision is **official `unoplatform/uno`, reference
only, no fork, no import**, correctly recorded at `README.md:44`. It is not a
pending choice about running Uno in a separate process. Remaining contradictions:

- `README.md:22` still offers “Uno or native”; `:49-50` suggests Apache components
  may live in separate repositories/processes “until the owner decides”. Scope
  that language to separately reviewed tools; the Uno decision is already made.
- `docs/ROADMAP.md:281` still offers “Uno or native”.
- The original plan is deliberately preserved (`README.md:9`), but its
  `UWP_COMPATIBILITY_PLAN.md:65,68,169,177,275,287,314` still proposes a managed
  backend, adapter or deferred bridge. Mark these explicitly superseded in the
  decision; reference-only investigation must not become an integration gate.
- `INVENTORY.md:47` still evaluates Uno's transitive/runtime ABI boundary as an
  integration candidate. `CODEX_REVIEW.md:123-127` labels the fork proposal
  superseded, but `:135-138,144-149,228` retains backend/process/prototype framing.
  Its ABI warning remains useful history; it is no longer an instruction to build
  an Uno wrapper. Do not create or use the abandoned fork for this track.

| Component | What this repository actually establishes |
|---|---|
| Wine runtime | LGPL-2.1-or-later supported by `wine/LICENSE:4-8` and inspected DLL headers, e.g. `wine/dlls/combase/roapi.c:5-9`; mixed-file exceptions still require review. |
| WineExplorer | GPL-2.0-or-later with component exceptions: `LICENSE.md:4-22`; source-sharing constraint and deliberate copyleft changes: `CLAUDE.md:21-25`, `LICENSE.md:24-31`. |
| MSIX SDK, C++/WinRT, C#/WinRT, WinUI 2/3, Calculator source | **MIT claimed, unverified here**: `UWP_COMPATIBILITY_PLAN.md:63-67,72`, not pinned upstream licence texts or dependency audits. No inspected candidate source checkout establishes these claims. |
| Uno, XbfTools, PriTools | **Apache-2.0 claimed, unverified here**: `UWP_COMPATIBILITY_PLAN.md:68-70`. `INVENTORY.md:39-54` explicitly records plan claims only. “Apache-2.0-only” is not established. |
| Uno Runtime Tests Engine; other prior-art repositories | **Unverified**, including dependencies and redistribution terms; plan `:71,76-82` is no licence proof. |

Keep a pinned licence/notice ledger before any tools import. A separate repository
or process does not by itself prove licence compatibility (`README.md:47-50`);
follow the project's module-specific decision rule. Open-source licence claims
about source do not license installed Store binaries, SDK payloads or framework
packages. Uno's reference-only decision stands regardless of any later licence
confirmation.

## Oracle and Calculator constraints

Windows 11 is a useful **versioned comparison environment**, not universal
truth. Pin OS build, SDK/toolset, package/dependency versions, architecture,
locale, DPI/theme, activation route and debug/PLM settings. Use clean-account
Release runs as well as diagnostic runs: an installed framework or debugger can
mask dependencies/lifecycle faults. Compare documented observable contracts;
do not hardcode undocumented implementation details or assume callback timing
and pixel output are identical across Windows revisions. Keep lawful fixture
sources/build recipes public and host-specific evidence private so results remain
reproducible without making a host dump part of the runtime.

The actual proprietary-material rule is stricter than “do not redistribute”:
`CLAUDE.md:9-14` also excludes proprietary reference sources and copied/traced
art and directs local material to **`../WineExplorer-local`**. Neither
`README.md:30-32` nor `CODEX_REVIEW.md:184-190` should authorize an arbitrary
untracked directory as a substitute. Untracked files inside the public tree,
screenshots/attachments, extracted manifests, dumped resource strings and logs
containing embedded assets need the same provenance boundary. Public records
should be minimal original observations, hashes and verdicts; do not reproduce
proprietary assets to make an oracle comparison reviewable. The policy's “never
named” wording is also in tension with the docs' explicit installed-product and
package names (`UWP_COMPATIBILITY_PLAN.md:21,219-224`, `README.md:29-34`): private
test permission does not itself resolve that literal wording. Keep detailed
proprietary inventories private; flag any intended naming exception for the owner.

The installed Calculator goal is therefore a **conditional private experiment**,
not a guaranteed redistributable acceptance milestone. Being installed on a
Windows host does not establish transfer/use rights under Wine. Its exact
version may require a different UI/runtime/dependency stack; this review did
not inventory the host, and historical .NET Native/WinUI/VCLibs lists are not
evidence for its current package. Stop at a rights/runtime blocker. A source-built
fixture is separate and cannot prove installed-package compatibility; successful
private execution using proprietary prerequisites cannot prove the product's
open-runtime distribution goal (`UWP_COMPATIBILITY_PLAN.md:17-18,228-246`).

## Recommended README changes (not made)

1. Define U1.0's launch/application model and its limited claim; require a
   manifest-declared self-owned component, consistent process/token identity,
   one specified async contract and negative cases. Put actual
   CoreApplication/ASTA/CoreWindow lifecycle in U1.1 unless explicitly enlarged.
2. Bring dependency/import closure and basic non-XAML MRT/PRI probing forward;
   separate these from XBF. Name the CoreWindow DXGI creation blocker and
   the distinction between CoreDispatcher and DispatcherQueue.
3. Make the Uno reference-only decision override every historical backend/fork
   proposal; remove “Uno or native” and restrict separate-process import language
   to tools. Label external licence names claimed/unverified until pinned proof.
4. Specify that the feature gate applies to new package launch/context/resolution,
   preserving existing WinRT activation. Clarify stable process identity after
   launch, default-off regressions, WoW64 and rollback acceptance.
5. Use `../WineExplorer-local` for private oracle material; distinguish behavioural
   observations from proprietary artifacts. Make the installed-app experiment
   conditional on rights and runtime availability, and identify the unresolved
   policy wording rather than granting a new exception.
6. Link this review, refresh the parent SHA in the inventory when adopting changes,
   and clarify that completing unrelated system/ISO work is a scheduling gate,
   while token/ACL overlap and machine-wide registration have concrete dependencies.

Only this review was written. No implementation or runtime success is claimed.
