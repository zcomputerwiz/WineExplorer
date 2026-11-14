# Known bugs (general Wine and WineExplorer)

Bugs that no feature session introduced. The system tier session owns this list (the owner's rule, 2026-11-14): other
sessions report here, the system tier session fixes or routes them and tells the reporter when one lands. Each entry:
who reported it, where it shows, what is known, the state.

| # | Bug | Reported | State |
|---|---|---|---|
| B1 | `user32:win`: 2 failures on x86_64, also on the older baseline (merged-a524eaf3) | UWP session, 2026-11-14 | to reproduce (`wine-tests.sh user32:win`) |
| B2 | `rpcrt4:server`: 2 `np_basic` failures, older than the UWP work | UWP session, 2026-11-14 | to reproduce |
| B3 | A new prefix has no `Themes\Personalize` light-theme values and no `Control Panel\Accessibility\HighContrast`; Uno and WinUI programs fall back to their own defaults | UWP session, 2026-11-14 | fixed in `wine.inf` (Windows' defaults, no clobber), branch `claude/wine-reuse`; a missing `TextScaleFactor` is Windows' own default |
| B5 | Programs see Windows 10 (`ver` 10.0.19045, CurrentVersion 6.3, no CSDVersion); XP-only installers stop at their launch condition (`VersionNT = 501 AND ServicePackLevel >= 2`). `HKCU\Software\Wine\AppDefaults\<exe>\Version=winxp` gets past it | Integration session, 2026-11-14 | owner chose Windows 10 plus a Compatibility tab; to do, see below |
| B6 | `.theme` files have no association (`HKCR\.theme`, a `themefile` ProgID): `start X.Theme` and a double-click fail. XP opens Display Properties' Themes tab with the theme selected (`rundll32 shell32.dll,Control_RunDLL desk.cpl desk,@Themes /Action:OpenTheme /file:"%1"`, as remembered, not checked). Our `desk.cpl` has no such command line yet | Integration session, 2026-11-14 | written, branch `claude/wine-reuse` (not built yet): `themefile` in `wine.inf` with XP's command; `Control_RunDLL` passes several arguments after the applet whole (as Windows and ReactOS; it kept only the last); `desk.cpl` reads `@<tab>`, `/Action:OpenTheme`, `/file:` and opens the Themes tab with the file selected |
| B7 | Copilot (Edge family, 156.0.4314.58) installs (msiexec 0, EdgeUpdate service runs) but the browser process stops itself within about 3 s: breakpoint exception 0x80000003 at msedge.dll+0x458ee9c (IMMEDIATE_CRASH/CHECK, message stripped), same with `--disable-gpu` and `--no-sandbox`; no window, no log | Integration session, 2026-11-14 (master 3a08b9b7, Wine 5ec3c009) | for the Edge/Chromium track (`EDGE_INSTALLER_ANALYSIS.md`, G4 headful readiness): probably the same early-start failure, not proven |
| B10 | Edge 156.0.4314.58 (enterprise MSI) on the stock image (master 3a08b9b7): install works (msiexec 0, EdgeUpdate service), headless rendering works in a user prefix, but the windowed browser dies before any window: ACCESS_VIOLATION reading 0xb0 at msedge.dll+0x2f431fa, same with `--no-sandbox --disable-gpu`. Just before: `app_bound_encryption_provider_win.cc:124` "Unable to encrypt key (0x80040155)" (REGDB_E_IIDNOTREG from the elevation service's COM call, an RPC fault packet), class `{aa509086-5ca9-4c25-8f95-589d3c07b48a}` not registered (local server), `WSALookupServiceBegin` failed with 8, WinRT EducationSettings and PenDevice without a library. Edge worked windowed on 2026-11-01 on the lab build (own labwc, VirGL) | Integration session, 2026-11-14 | for the Edge/Chromium track. The system tier part: 0x80040155 is an interface not registered for marshalling, i.e. the Edge elevation service's interface (a COM local server in a service) has no proxy/stub or type library registration that the caller sees; with session 0, services and the caller are in different servers, so check where the per-machine install registered it |
| B11 | `scripts/wex-components.py` maintains the machine root by running Wine as root on it (its own wineserver, `wineserver -w` for idleness), and has no notion of session 0. On an image with session 0, session 0's wineserver (another uid) also holds the machine hive: two servers load and save `system.reg`, so a component's registry changes (vcrun2022, d3dcompiler, the coming uwp-xaml) can be lost when session 0 saves, or session 0's changes lost when root's server saves | system tier session (review of the XAML component), 2026-11-14 | to do (system tier). Decided by the owner: the registry writes go THROUGH session 0 (rule in `SYSTEM_TIER_WORK_ORDERS.md`, "every write to the machine registry goes through session 0"), not by stopping session 0. Not a blocker while session 0 is opt-in; required before session 0 is on by default |
| B8 | `msiexec /qb` shows no progress window (`fixme:msi:internal_ui_handler internal UI not implemented for message 0x0b000000 (UI level = 3)`); XP shows a basic progress dialog | Integration session, 2026-11-14 | to do in `dlls/msi` (basic UI) |
| B9 | `RoGetActivationFactory` finds no library for `Windows.System.Profile.PlatformDiagnosticsAndUsageDataSettings`, `Windows.System.UserProfile.AssignedAccessSettings`, `Windows.Internal.System.Profile.RegionPolicyEvaluator`, `Windows.System.Diagnostics.Telemetry.PlatformTelemetryClient` (noise, not blocking) | Integration session, 2026-11-14 | not blocking; the UWP session leaves it until after its XAML start-up work (small: statics returning defaults); whoever finds it blocks Copilot takes it |
| B4 | A per-machine MSI run by an administrator from the desktop fails silently (exit 1627, empty `/l*v` log, no message) | Integration session, 2026-11-14 | analysed; owner chose the consent prompt for administrators; see below |

## B10: Edge's elevation service call (0x80040155)

Looked into 2026-11-14 (the COM part only; the windowed crash itself is the Edge/Chromium track's).

- On Windows (this host, Edge 157), the elevation service `MicrosoftEdgeElevationService`
  (`...\Edge\Application\<version>\elevation_service.exe`) has one COM interface,
  `{12B23868-6DCD-4D1A-AD4C-65B0285201FC}`, marshalled through its type library: `HKCR\Interface\{12B23868-...}`
  with `ProxyStubClsid32 = {00020424-0000-0000-C000-000000000046}` (the type library marshaller) and
  `HKCR\TypeLib\{12B23868-...}\1.0\0\win64` (and `win32`) = the service's exe.
- 0x80040155 is `REGDB_E_IIDNOTREG`; in Wine it comes from `CoGetPSClsid` (`dlls/combase/combase.c`, about 2290)
  when `HKCR\Interface\{iid}\ProxyStubClsid32` is missing in the process's registry. The fault came back in an RPC
  fault packet, so it is the service's side (the process that marshals the interface) that does not see the
  registration.
- First guess (session 0 not seeing the registration) does NOT apply to this test: the tested image (master
  3a08b9b7, unattended install) runs neither session 0 nor enforced elevation (both are packaging opt-ins,
  `WEX_SESSION0=1` / `WEX_ELEVATION=1`), so the service ran in the user's own server and saw the same registry as the
  user's programs, in one boot (no restart between install and launch, per the Integration session).
- Remaining candidates: the install wrote the registration into another view (the 32-bit view, or `HKCU` as a
  per-user registration) or a VirtualStore; a write was refused; or the browser asks for a different interface than
  `{12B23868-...}` (Edge 156 vs the host's 157).
- To decide it on the next VM (asked of the Integration session): as the installing user, `reg query` of
  `HKLM\Software\Classes\Interface\{12B23868-...}` and `HKLM\Software\Classes\TypeLib\{12B23868-...}` (both with
  `/reg:64` and `/reg:32`), the same under `HKCU\Software\Classes`, and
  `HKCU\Software\Classes\VirtualStore\MACHINE\SOFTWARE\Classes`; and `WINEDEBUG=+ole` on the elevation service to see
  which IID it looks up. A session 0 image would add the cross-server question; the eventual per-machine path is the
  elevated (consent) install of B4.
- Result of the checks (Integration session, fresh VM, one boot): `{12B23868-...}` is Edge 157's; Edge 156 registers
  `Interface\{C9C2B807-7731-4F34-81B7-44FF7779522B}` (typelib marshaller) with its TypeLib, and CLSID/AppID
  `{1FCBE96C-1697-43AF-9140-2897C7C69767}` (LocalService `MicrosoftEdgeElevationService`, LocalSystem, demand
  start), all present in both views. Under `+ole,+rpc` neither run logged 0x80040155, yet both crashed the same way:
  the 0x80040155 is intermittent or a side path and NOT what kills the windowed start.
- Stock Chromium (snapshot 1736300, 159.0.8145.0) runs windowed in the ordinary session (sandbox on, GPU process,
  renderers, WebGL, no crash): B10 is in an Edge-only path.
- The class `{aa509086-5ca9-4c25-8f95-589d3c07b48a}` Edge looks up (and stock Chromium does not) is
  `CLSID_VirtualDesktopManager` (`IVirtualDesktopManager` `{a5cd92ff-29be-454c-8d04-d82879fb3f1b}`, checked on this
  host: in-process in `twinapi.dll`). Wine had neither the class nor the interface. If Edge does not check that
  creation, a null read at +0xb0 fits. Done 2026-11-14 (branch `claude/wine-reuse`, not built yet): the class in
  `shell32` for one desktop, with a test; Edge to be retried with it.

## B5: the Windows version programs see

Wine reports Windows 10 by default, which modern programs need (they refuse XP, and their APIs exist). XP-only
programs and installers then refuse to run. XP itself solves the opposite case with the Compatibility tab of a
program's Properties ("Run this program in compatibility mode for"), which in Wine maps to
`HKCU\Software\Wine\AppDefaults\<exe>\Version`. Options: keep Windows 10 for everything and add the Compatibility tab
(per program, as XP); or report XP everywhere (breaks modern programs).

**Owner's decision (2026-11-14): Windows 10 for everything, plus the Compatibility tab.** Prior art: base Wine has
only `winecfg`'s Applications tab (`AppDefaults\<exe>\Version`); ReactOS has the Compatibility tab
(`dll/shellext/acppage`, GPL-2.0-or-later, C++/ATL, about 15 translations) on top of Windows' compatibility layers
(`HKCU`/`HKLM\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers`, value name = the program's path,
data such as `WINXPSP3`) through `apphelp`'s `SdbGetPermLayerKeys`/`SdbSetPermLayerKeys`, which are stubs in Wine.
Plan, the Windows way:

1. `apphelp`: implement `SdbGetPermLayerKeys` / `SdbSetPermLayerKeys` (written for Wine, LGPL; ReactOS's
   `apphelp/layer.c` as reference only).
2. `ntdll` `version_init`: when `AppDefaults` names no version, take the program's layer (HKCU, then HKLM) and map it
   (`WINXPSP3` -> winxp, `WIN2000` -> win2k, ...). Programs and installers that set a layer themselves then work too.
3. The tab: a C port of ReactOS's `acppage` page (its dialog and translations; the ATL C++ does not build in Wine's
   tree), registered for `exefile` and `lnkfile`. The XP check boxes with no Wine meaning (256 colours, 640 x 480,
   disable visual themes) are greyed.

Started 2026-11-14: parts 1 and 2 (Qwen, branch `claude/wine-reuse`). Open: how an `.msi` gets a version (the one
`msiexec.exe` sees).

The theme MSI also showed the theme engine itself works: with its files in place,
`explorer.exe /theme=...\Zune.Theme` applied the third-party msstyles, wallpaper, taskbar, frames, task pane and
Start menu.

## B4: per-machine MSI by an administrator

Seen on the master 3a08b9b7 image (ISO wex-vb, no enforced elevation), normal start: `msiexec /i C:\x.msi /qb
/l*v C:\x.log` as `wineuser` (in `sudo`, an XP administrator) returns 1627 with no window and an empty log. Wine's
`MSI_OpenPackageW` copies the package into `GetWindowsDirectory()\Installer\` (`dlls/msi/package.c`, about 1060);
`C:\windows` is the machine root (`root:wexsys` 2775), which only the system group writes, so `CreateFileW` fails and
the open ends with `ERROR_FUNCTION_FAILED` before anything is logged.

What is wrong, in two parts:

1. **No message and no log.** A failed open of the package must say so (Windows shows "This installation package
   could not be opened" and logs it). Small fix in `dlls/msi`: report and log the failure of the local copy.
2. **No way for an administrator to install per machine.** On Windows the per-machine part of an installation runs in
   the Windows Installer service as LocalSystem; the client only asks it. Wine's `msi` does everything in the
   caller's process. Without enforced elevation nobody, administrators included, writes the machine root (by design:
   C2, "the machine hive is not saved without enforcement"). With enforced elevation, an administrator's elevated
   process can. The fix that matches Windows: per-machine installation through session 0 (the `msiserver` service as
   SYSTEM, after a check that the caller is an administrator), or, smaller, `msiexec` asking the elevation broker
   when a package is per machine (Vista's installer detection).

**Owner's decision (2026-11-14): Vista's way.** An administrator gets the consent prompt for a per-machine
installation, as on Vista and later (not XP's silent install). So part 2 is: `msiexec` (and `msi` when a package is
per machine) asks the elevation broker, the administrator confirms in the consent dialog, and the installation runs
elevated. This needs enforced elevation; on an image without it, per-machine installation stays impossible, which is
one more reason for enforcement by default (`E1_PRODUCTION_PLAN.md`). Part 1 (message and log) is done first.

Part 1, 2026-11-14 (branch `claude/wine-reuse`): `MSI_OpenPackageW` reports a failed local copy, see
`STOCK_CHANGES.md`. The failing case itself is to be checked on an image (the Integration session's package).
