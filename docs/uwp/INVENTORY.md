<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# U1.0 static inventory (2026-11-05)

Static-only snapshot. No build, guest run, Windows oracle, legal conclusion, or
runtime validation was performed.

## Pinned source

| tree | exact state |
|---|---|
| WineExplorer | `169a826ae29a9976f01fa680183176a3d208e363` (`git rev-parse HEAD`) |
| `wine/` gitlink | `14e252dc8836b4b81750d6d220d67911e78efdc0` |
| checked-out Wine | `14e252dc8836b4b81750d6d220d67911e78efdc0`, branch `development`, clean status observed |
| `wexcomp/` | `41339f2cae25068c6e94d122cc3f5babf23bc251`, branch `codex/g4-native-render-bridge` |
| pin match | yes: gitlink equals checked-out Wine `HEAD`; no Wine-development divergence observed |

## Module inventory

`Implemented` means there is nontrivial code; it does not mean Windows/UWP
compatibility. `Partial` means a factory/object or narrow behavior exists;
`E_NOTIMPL stub` is explicit in the cited code. Existing test paths are listed
where present. Upstream/Wine-Staging status is **not checked** except for the
local audit noted below.

| area / classes or functions | static status, evidence, tests |
|---|---|
| `combase`: HSTRING; apartments; `RoInitialize`, `RoGetActivationFactory`, `RoActivateInstance`; `IInspectable` support | HSTRING storage/allocation is implemented in `combase/string.c:31-80`; apartment state exists in `combase/apartment.c:41-72`; `RoInitialize` maps to `CoInitializeEx` (`combase/roapi.c:127-136`), but activation is explicitly semi-stub and DLL/factory call-through (`roapi.c:150-209`, `227-241`). `RoGetParameterizedTypeInstanceIID` is `E_NOTIMPL` (`roapi.c:214-221`); agile reference requires an initialized apartment (`roapi.c:414-435`). Tests: `combase/tests/string.c`, `combase/tests/roapi.c:85-138,260-290`. Upstream/staging: not checked. |
| `kernelbase`: package identity exports `GetCurrentPackage*`, `GetPackageFullName/FamilyName`, `GetCurrentPackageId/Info/Path` | Exports are present in `kernelbase/kernelbase.spec:493-497,632-636`, but all requested current/process identity functions return `APPMODEL_ERROR_NO_PACKAGE` with FIXME (`kernelbase/version.c:1561-1623`). `PackageIdFromFullName` is separate parsing code (`version.c:1691` onward). Tests: `kernel32/tests/version.c:827-875,1107-1125` cover package-name/id parsing, not current process identity. Upstream/staging: not checked. |
| `ntdll` / process package context | `RtlQueryPackageIdentity` is a direct `STATUS_NOT_FOUND` stub (`ntdll/rtl.c:1623-1630`). `NtCreateUserProcess` parses ordinary process attributes but no package-context handling is evidenced in the inspected path (`ntdll/unix/process.c:685-760`). Existing activation-context/manifest machinery is general SxS (`ntdll/actctx.c`, e.g. `5267-5426`), not proof of MSIX identity. Tests: `ntdll/tests/*` have no current-package test found; `kernel32/tests/version.c` only tests package-name parsing. Upstream/staging: not checked. |
| `windows.applicationmodel`: `Package`, `DesignMode` | IDL classes are declared at `windows.applicationmodel/classes.idl:29-31`. `Package.Current` allocates an object (`package.c:499-512`), but package metadata/object methods and factory inspectable methods contain `E_NOTIMPL` (`package.c:83-104,427-478`); this is partial shell behavior. Tests: `windows.applicationmodel/tests/application.c`, `model.c` (activation/factory tests; `model.c:210-235`). Upstream/staging: not checked. |
| `windows.ui`: `CoreWindow`, `AccessibilitySettings`, `UISettings`, `UIViewSettings`, `InputPane` | All are declared at `windows.ui/classes.idl:38-46`. `CoreWindow` factory methods are stubbed (`corewindow.c:81-102`); `GetForCurrentThread` returns `S_OK` with NULL (`corewindow.c:120-126`), so partial/stub and not a usable target. `uisettings.c`, `uiviewsettings.c`, and `inputpane.c` contain broad inspectable/property stubs (e.g. `uisettings.c:83-112`, `uiviewsettings.c:86-105`). Tests: `windows.ui/tests/corewindow.c`, `uisettings.c`. Upstream/staging: not checked. |
| `twinapi.appcore`: `CoreApplication`, `ApplicationView`, `DataTransferManager`, `EasClientDeviceInformation`, `AnalyticsInfo`, `AdvertisingManager` | Classes are declared at `twinapi.appcore/classes.idl:37-53`. `CoreApplication` factory is an E_NOTIMPL shell (`core_application.c:72-93`); the module has additional object files but no evidence here of a complete lifecycle. Tests: `twinapi.appcore/tests/twinapi.c:65-85` and further factory cases. Upstream/staging: not checked. |
| `windows.ui.xaml`: every declared class is `Windows.UI.ColorHelper` | The IDL declares only `ColorHelper` (`windows.ui.xaml/classes.idl:26-27`). Its factory inspectable/activation methods return `E_NOTIMPL` (`color_helper.c:80-101`), while `FromArgb` is a narrow static operation (`color_helper.c:119` onward). Test: `windows.ui.xaml/tests/xaml.c:60` (factory lookup). No controls, layout, XBF, namescope, or resource loader observed. Upstream/staging: not checked. |
| `windows.storage*`: `RandomAccessStreamReference`, `InMemoryRandomAccessStream`, `ApplicationData` | Classes: `windows.storage/classes.idl:32-34`; `windows.storage.applicationdata/classes.idl:26-27`. Storage has partial in-memory streams and async helpers (`streams.c:204-231,535-689`; `async.c:57-299`), but multiple stream/factory methods are `E_NOTIMPL` (e.g. `streams.c:121-145,287-305`). `ApplicationData` has an inspectable object, but factory methods/properties are largely stubs (`applicationdata.c:90-102,129-147`). Tests: `windows.storage/tests/storage.c:139-180`, `windows.storage.applicationdata/tests/data.c`. Upstream/staging: not checked. |
| `appxdeploymentclient`: `PackageManager` | The only class is declared at `appxdeploymentclient/classes.idl:26-27`. Activation factory is present, but Add/Register/Find/Remove/manager operations return `E_NOTIMPL` (`appxdeploymentclient/package.c:105-206,239-291`); `DllGetActivationFactory` is only dispatch (`main.c:33-38`). No module tests found. Upstream/staging: not checked. |
| `coremessaging`: `DispatcherQueueController` | Class declaration: `coremessaging/classes.idl:26-27`. Factory dispatch exists (`coremessaging/main.c:254-267`); controller creation is labelled semi-stub (`main.c:269-280`) and inspectable/controller methods include `E_NOTIMPL` (`main.c:176-229`). Test: `coremessaging/tests/coremessaging.c:416-543`. Upstream/staging: not checked. |
| DComp/DXGI surface producers relevant to a CoreWindow target | DComp has surface/virtual-surface implementation in `dcomp/device.c` (surface methods around `~900-1030`; some methods return `E_NOTIMPL`, e.g. `device.c:5444`). DXGI exposes `IDXGISurface*` and subresource surface (`dxgi/resource.c:31-42,172-318,741-778`); swapchain has a CoreWindow slot (`dxgi/swapchain.c:1308-1319`) and DComp binding/present path (`swapchain.c:411-460,512-679`), with unsupported operations returning `E_NOTIMPL` (e.g. `swapchain.c:1348-1506`). Tests: `dcomp/tests/dcomp.c`, `dxgi/tests/dxgi.c`. Local staging audit: `docs/STAGING_OVERLAP.md:7-15,23-32` checks DComp/DXGI surface prior art only; individual UWP support is not checked. |

## Licence ledger (plan claims only; no legal conclusion)

| candidate | plan-stated licence/status | must verify before reuse |
|---|---|---|
| `microsoft/msix-packaging` | MIT | bundled archive/signature/crypto and other dependency notices; plan explicitly says inspect dependencies (`UWP_COMPATIBILITY_PLAN.md:63,126`). |
| `microsoft/cppwinrt` | MIT | bundled/generated dependencies and fixture redistribution notices (`UWP_COMPATIBILITY_PLAN.md:64`). |
| `microsoft/CsWinRT` | MIT | bundled managed/build dependencies and whether its selected use remains outside Wine DLLs (`UWP_COMPATIBILITY_PLAN.md:65,169`). |
| `microsoft-ui-xaml` WinUI 2 | MIT repository; dependencies must be inspected | system `Windows.UI.Xaml` and all bundled/native dependencies; it is not a complete replacement (`UWP_COMPATIBILITY_PLAN.md:66-67,170`). |
| Uno | Apache-2.0 | transitive NuGet/native dependencies, combined-work model, and native ABI/XBF boundary (`UWP_COMPATIBILITY_PLAN.md:68,169,287-293`). |
| XbfTools | Apache-2.0; incomplete XBF2 decoding | bundled dependencies and parser provenance/limitations (`UWP_COMPATIBILITY_PLAN.md:69,190`). |
| PriTools | Apache-2.0 | bundled dependencies and any adapted reader boundary (`UWP_COMPATIBILITY_PLAN.md:70,190-191`). |
| `wine-uwp` | plan identifies prior art; licence is not stated | repository licence, authors/notices, dependencies, and proprietary Windows-DLL requirement; **not checked** (`UWP_COMPATIBILITY_PLAN.md:76`). |
| `switchyard-wine` | plan identifies prior art; licence is not stated | repository licence, bundled dependencies, scope, and package/signature code provenance; **not checked** (`UWP_COMPATIBILITY_PLAN.md:77`). |

The plan requires a module-specific review at the Wine LGPL boundary and says
Apache-2.0 components stay separate until reviewed (`UWP_COMPATIBILITY_PLAN.md:32-39`).

## Feature-gate contract proposal

Use `HKCU\Software\WineExplorer\Shell\UwpCompatibility` as a `REG_DWORD`,
default `0`; only exact `1` enables U1.0. This follows the existing gate
pattern: `DwmService` reads the same key and returns when absent/zero
(`src/dlls/wexshell/dwm_bridge.c:365-372`), while `Composition` is read as a
DWORD and defaults off (`dwm_bridge.c:514-523`).

When off, no packaged identity is fabricated, no package/dependency store or
manifest activation is entered, and unsupported WinRT activation must return
its existing failure (`APPMODEL_ERROR_NO_PACKAGE`, `REGDB_E_CLASSNOTREG`, or
the applicable HRESULT), never success. The minimal stock-Wine placement is a
shared read-only helper called at the front of `combase` activation
(`roapi.c:150-171`, and therefore `RoActivateInstance` at `227-241`) and in
`kernelbase` package-context queries (`version.c:1561-1623`); keep the
WineExplorer registry policy out of the package object implementation until a
separate stock-change review. The check must also cover process/package
context creation before `RtlQueryPackageIdentity` can claim success
(`ntdll/rtl.c:1623-1630`).

## Existing tests U1.0 can reuse

WinRT activation/HSTRING/apartments: `wine/dlls/combase/tests/roapi.c` and
`string.c`; `windows.applicationmodel/tests/application.c` and `model.c`;
`twinapi.appcore/tests/twinapi.c`; `windows.ui/tests/corewindow.c` and
`uisettings.c`; `windows.ui.xaml/tests/xaml.c`; `windows.storage/tests/storage.c`
and `windows.storage.applicationdata/tests/data.c`; and
`coremessaging/tests/coremessaging.c`. Representative activation/HSTRING use
is `combase/tests/roapi.c:85-107`, and representative package-adjacent
factory use is `windows.storage/tests/storage.c:139-180`.

Package identity/name parsing: `wine/dlls/kernel32/tests/version.c:827-875`
and `1107-1125`. No existing test for a successful current process package
identity (`GetCurrentPackage*`, `RtlQueryPackageIdentity`) was found; **not
checked beyond the repository search above / no such test observed**.

## Unverified boundaries

No upstream Wine or Wine-Staging per-component comparison was performed beyond
the DComp/DXGI scope recorded in `docs/STAGING_OVERLAP.md`. No runtime status,
Windows branch equivalence, package-store behavior, legal conclusion, or
dependency licence audit is claimed.
