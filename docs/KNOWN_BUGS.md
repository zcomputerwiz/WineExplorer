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
| B12 | The installed system's own host name does not resolve: setup (and sysdm.cpl's Computer Name tab) set it through wexhost `WexHostnameSet` (hostname1 `SetStaticHostname`) and `/etc/hostname`, but `/etc/hosts` keeps the live image's `127.0.0.1 localhost debian`, so `hostname -f` fails and sudo warns "unable to resolve host WEX-PC". Not the cause of B10 (tested) | Integration session, 2026-11-14 | Fix that also covers later renames: add `libnss-myhostname` to `installer/live-build/config/package-lists/wineexplorer.list.chroot` (its postinst adds `myhostname` to the hosts line of nsswitch.conf). **Fixed** on `claude/vintner-boot-integration` 7fe233da (Integration session, 2026-11-14): the package, plus checks in `installer/tests/install-unattended.sh` and `scripts/test_safemode.py`; zero-input install and accounts pass on BIOS and UEFI. Lands on `master` with the next merge |
| B13 | `SystemTimeToTzSpecificLocalTimeEx` and `TzSpecificLocalTimeToSystemTimeEx` are stubs in kernelbase (and absent from kernel32), so callers with a `DYNAMIC_TIME_ZONE_INFORMATION` fall back to the non-Ex functions | UWP session (windows.globalization Calendar), 2026-11-14 | Written on `claude/wine-reuse`: the year's rules from `GetTimeZoneInformationForYear`, then the existing conversion; a zone without a key name (or with dynamic daylight time disabled) keeps its own rules. Test in `kernel32/tests/time.c` |
| B14 | `LdrResolveDelayLoadedAPI` failure path: for an import by name the module's failure hook got the low word of the name's RVA as `Description.Name` (hooks that read it crash; seen with .NET Native's `mrt100_app.dll`); a hook returning NULL skipped the system routine; `LastError` was an NTSTATUS | UWP session, 2026-11-14 | Written on `claude/wine-reuse` after a Windows probe (`unsloth-tmp/b10trace/delayprobe.c`): the name pointer (or the ordinal), the system routine after a NULL from the module's hook, the Win32 error (126); the IAT entry is left alone. Test in `ntdll/tests/rtl.c` |
| B15 | DirectWrite gives a variable font in a custom collection only its typographic family name (name IDs 21, 16, 1: "Segoe Sans"); Windows' WWS model names each named instance from STAT/fvar ("Segoe Sans Text"). Microsoft Copilot (Edge 156 build) loads Segoe Sans from memory, looks up "Segoe Sans Text", gets nothing and fails a CHECK (B7) | Integration session, 2026-11-14 | **Fixed for Copilot (2026-11-15, Wine development a5b6756b):** Copilot adds its in-memory Segoe Sans with `AddFontFaceReference` (no axis values), not `AddFontFile`; on Windows such a reference is the named instance at its position (default position: "Text Regular"), measured with the generated font. Wine now does the same (52b4c380). Integration: Copilot starts on an installed machine and shows its sign-in page (10 processes, no fatal dump); its font has STAT 1.1 (3 axes, 13 values) and 27 instances, the reference is instance 14 "Text Regular". Not tested: sign-in, per-instance rendering (step 2), opsz selection of Small/Display. **Step 1 (naming):** named instances in font sets with WWS and typographic names from fvar/STAT (`dlls/dwrite`); `test_fontset_named_instances` with a generated variable font passes on Windows 11 and Wine (the Windows run is the reference). Step 2 left: render each instance (FreeType named-instance face index). **Copilot not fixed by step 1** (Integration session, 2026-11-15, Wine f1347be7 on an installed machine): same CHECK; `+dwrite` shows one `FindFamilyName(L"Segoe Sans Text")` on its custom collection and only "Segoe Sans" names, so its in-memory font gave no instances. Trace commit c27eca75 (why a face gives none) is out for the next run; the font itself is not available to us. Confirmed on Windows 11 (2026-11-15, probe `unsloth-tmp/b15/vfnames.cpp`, a custom font set from one file): Segoe UI Variable (axes wght, opsz) gives 3 WWS families "Segoe UI Variable Small/Text/Display" (the non-WWS axis opsz splits the family by named instance) but 1 typographic family "Segoe UI Variable" with faces Small/Text/Display; Bahnschrift (wght, wdth only) stays 1 family in both models (width and weight go to the face names, plus synthesized obliques in WWS). Next: the same probe on Wine (needs an open variable font with opsz), then fix `dlls/dwrite`. Prior art (2026-11-15): nothing in upstream Wine (no merge request enumerates named instances; only Sivov's 2022 axis methods on the resource object, and a closed `HasVariations` request turned down until variable fonts really work; win32u's GDI does not enumerate them either). Microsoft's "OpenType variable fonts" page states the behaviour: `AddFontFile` adds every named instance; without `STAT` a variable font is only its default instance; wght/wdth/ital/slnt stay in the face and other axes go into the WWS family name (Sitka "Text Regular" -> family "Sitka Text", face "Regular"), but not how names are built. Naming guidance: fontTools `varLib/instancer/names.py` (MIT; the STAT axis value matching each coordinate, non-elided names in axis order, `elidedFallbackNameID` when all are elided) and HarfBuzz's STAT parser (MIT) for the table layout; we follow them as references, not copy. Rendering (step 2): FreeType instantiates a named instance itself when the face index carries the instance number in its upper 16 bits, so per-instance rendering may need no coordinate code |
| B16 | Wine's DirectWrite reads only `HKCU\Software\Wine\Fonts\Replacements`, not the `HKLM ...\FontSubstitutes` aliases the image writes (Segoe UI → Selawik and others, read by GDI only), so DirectWrite and Skia users (Edge, Chromium, Copilot) never see WineExplorer's aliases | Integration session, 2026-11-14 | **Fixed** (2026-11-15, dwrite font tests pass on Wine x64 with the new HKLM test): Wine's DirectWrite also reads `HKLM\Software\Wine\Fonts\Replacements` (after HKCU), and `src/resources/fonts/font-substitutes.reg` writes the aliases there too. `FontSubstitutes` is left to GDI: Windows' DirectWrite does not read it (Antigravity's reading of the documentation and of Chromium's GDI workaround; not probed, since that would change the host's HKLM) |
| B17 | A service in session 0 cannot be reached through its own RPC endpoint (`\pipe\lrpc\...`, `ncacn_np`) by a program in a user's session: pipe namespaces are per server, the T5 relay carries `svcctl` only (`SLOpen` of Office's licensing client fails with `0x80070078`, `c0000034` on the pipe open) | Office session, 2026-11-15 | **ncalrpc part fixed** (2026-11-15, T1 `lrpc_*` checks and the full wex-run pass): a user's server bridges `lrpc\NAME` it does not have to session 0 over the existing T5.2 pipe bridge (`server/named_pipe.c`), except the per-session rpcss endpoints and pid/OXID names; session 0 checks the pipe's descriptor. Left: `ncacn_np` pipes of services (no name rule tells them from a session's own; needs a list or registration), and waiting for an endpoint that is not listening yet; detail OB-1 in `OFFICE_BUG_NOTES.md` (details: `docs/OFFICE_BUG_NOTES.md` on `claude/office-wineexplorer-vm-600501`, ec9b5550) |
| B18 | `MsiSourceListEnumSourcesW` returns 1605 for a package that is installed; `setup.exe /uninstall` of Office 2016 stops at once | Office session, 2026-11-15 | **cause found by the Integration session (2026-11-15, `+msi` trace in the Office lab):** Wine's msi wrote `Assignment=0` for every install, so setup (`MsiGetProductInfo(AssignmentType)`, then `MsiIsProductElevated`, a stub that says TRUE) asked `MsiSourceListEnumSourcesW` in context USERMANAGED with `S-1-5-18`: 1605. With `Assignment=1` set by hand the uninstall ran to the end (return code 0). **Fixed** (`dlls/msi/action.c`; Wine's existing test lost its todo and passes on x64, 2026-11-15; not yet rerun with Office). Still to look at: `MsiIsProductElevated` is a stub. Windows 11 (probe `unsloth-tmp/b18/srclist.c`, 3 per-machine products): context MACHINE gives the source (NETWORK) or 259 (URL), ALL gives 87, USERUNMANAGED/USERMANAGED give 1605; so 1605 means setup asked in a per-user context: which call told it so needs a `+msi` trace of the uninstall (asked of the Office session, 2026-11-15). Wine answers ALL with 1605 instead of 87 (a small conformance gap, not this bug); OB-2 (details: `docs/OFFICE_BUG_NOTES.md` on `claude/office-wineexplorer-vm-600501`, ec9b5550) |
| B19 | Session 0's account cannot save its own `HKCU` (`machine/user.reg` is `root:wexsys` 0644): a setting such as the Windows version is lost at every restart of the unit | Office session, 2026-11-15 | **Fixed** (2026-11-15, `test_b3_machine_root.sh` passes; takes effect at the next `setup-machine-root.sh` run): `user.reg` is session 0's own `HKCU`; the server checks that a hive file is writable before saving and silently dropped the changes. `setup-machine-root.sh` now gives it mode 664 and `g:wexsys:rw` like `system.reg`/`userdef.reg`; test in `test_b3_machine_root.sh`; OB-3. Related: B5 (details: `docs/OFFICE_BUG_NOTES.md` on `claude/office-wineexplorer-vm-600501`, ec9b5550) |
| B20 | An installer cannot overwrite a font that the machine root links to an open font (`EACCES` through the link, setup 1603) | Office session, 2026-11-15 | fixed on the fork branch `claude/office-wine` (`53e7017`, `server/fd.c`), tested directly. Reviewed by session 0 (2026-11-15): fine to merge. Removing the link needs write access to its directory (the kernel checks it, sticky directories included), so nobody can remove a link they could not delete, and a standard user still cannot touch the machine root. If the create fails after the unlink, the link is gone (harmless; the setup makes it again). To do before merging: a suite case (a link to an unwritable file in a writable directory, CREATE_ALWAYS through Wine replaces it; a read-only open still follows the link); design question in OB-4 (details: `docs/OFFICE_BUG_NOTES.md` on `claude/office-wineexplorer-vm-600501`, ec9b5550) |
| B21 | Office 2016's licensing service refuses to register its plugin during setup in session 0 (`0xc004f025`, rolls back with 1603) | Office session, 2026-11-15 | cause unknown; OB-5 (details: `docs/OFFICE_BUG_NOTES.md` on `claude/office-wineexplorer-vm-600501`, ec9b5550) |
| B22 | Word 2016's "First things first" dialog: Accept does nothing and starts `DW20.EXE` | Office session, 2026-11-15 | cause unknown; OB-6 (details: `docs/OFFICE_BUG_NOTES.md` on `claude/office-wineexplorer-vm-600501`, ec9b5550) |
| B23 | `wintypes` `IPropertyValue::GetRuntimeClassName`/`GetTrustLevel` are `E_NOTIMPL` (upstream too): .NET Native unboxes an Object by its class name, so the Store Calculator fails with `E_NOINTERFACE` reading `Application.Resources["AppMinWindowWidth"]` | UWP session, 2026-11-15 | fixed on `codex/uwp-runtime` 3bf58b67 (``Windows.Foundation.IReference`1<T>`` / ``IReferenceArray`1<T>``, `BaseTrust`; test of every scalar and array type passes on Windows 11 and Wine, x64/x86); UWP session cherry-picks it to Wine `development` |
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
  host: in-process in `twinapi.dll`). Wine had neither the class nor the interface. Written 2026-11-14 (branch
  `claude/wine-reuse`): the class in `shell32` for one desktop, with a test.
- Baseline in a plain prefix (wexbuild, Xvfb, no WineExplorer shell, Wine of the reuse branch WITHOUT the class,
  `unsloth-tmp/edge-prefix-test.sh` in the systier worktree): the MSI installs (0) and the WINDOWED browser starts:
  main window "about:blank - Profile 1 - Microsoft Edge" 1050x980, a "DComp Swapchain" window, 8 processes alive
  at 45 s, the `{aa509086-...}` lookup failing exactly as on the image. So the missing VirtualDesktopManager is NOT
  what kills Edge, and the crash needs something of the WineExplorer desktop session: the shell, wexhost and its DWM
  broker, the project's own `dcomp` (Edge composes through DirectComposition; `IDCompositionDevice3`'s effect
  creators are E_NOTIMPL stubs), or the machine-root prefix.
- Split on the image (Integration session, master 2162596c, Wine 1052fab3, Xorg on QEMU std VGA): the crash is
  identical with `--disable-direct-composition`, with `Composition=0`, and from a fresh plain prefix in the same
  session. So neither the WineExplorer prefix, the shell, DWM composition nor DirectComposition is the cause.
- Wine 1052fab3 itself in wexbuild under Xvfb (master 2162596c, clone verified): Edge starts windowed (8
  processes). So the Wine commit is ruled out too. Left: the display (Xorg + Mesa on the VM's VGA vs Xvfb), the CPU
  model QEMU gives the guest, the packaged Wine vs a build tree, and memory. Asked of the Integration session: the
  guest's CPU flags and `-cpu host`, and Edge under Xvfb inside the guest.
- Memory and disk ruled out (Integration session, one VM alone): at 4 GB the guest had 2.8-3.3 GB available during
  the start, no swap, 6.3 GB free on root, no OOM in the kernel log; restarted with 8 GB (6.8 GB available) the crash
  is identical. The CPU is already `-enable-kvm -cpu host` (vmtest.py). Edge under Xvfb inside the guest is running.
- Display and CPU ruled out (Integration session): Edge under Xvfb inside the guest crashes the same way; hiding
  AVX-512 and changing the CPU count (smp 8) change nothing. The crash follows the QEMU guest, not the display.
- The packaged Wine ruled out: Wine extracted from the installer image (`/root/out/wex-el.iso`, wine-11.19-1027-
  g2ebf65d5, `/usr/local` of the squashfs) starts Edge windowed in wexbuild under Xvfb (main window, 8 processes).
- WSL (wexbuild) has no `/sys/class/dmi/id`, so Wine's SMBIOS tables there hold empty strings, while the guest has
  QEMU's values. QEMU q35-like DMI (sys_vendor `QEMU`, product `Standard PC (Q35 + ICH9, 2009)`, SeaBIOS
  1.16.3, 04/01/2014, chassis type 1, empty board and serials) overlaid on `/sys/class` in a private mount namespace
  (`unsloth-tmp/edge-fake-dmi.sh`): Edge still starts windowed (8 processes). Approximate DMI is not the cause;
  asked of the Integration session for the guest's exact `/sys/class/dmi/id` values and an Edge start in the guest
  with `/sys/class/dmi` hidden, which settles DMI either way.
- DMI ruled out (Integration session, master 2162596c): the guest's DMI is SeaBIOS 1.16.3-debian-1.16.3-2,
  04/01/2014, QEMU "Standard PC (Q35 + ICH9, 2009)" pc-q35-10.0, chassis type 1; with `/sys/class/dmi` hidden (a
  tmpfs over it in a private mount namespace, back to the user with `setpriv`) Edge crashes the same way. A host name
  that resolves (B12) does not stop it either.
- Named with Microsoft's public symbols (owner's OK, 2026-11-14; `msedge.dll.pdb`
  `03775A411C2857584C4C44205044422E1`, 686 MB, kept outside the repository, resolved with Windows' dbghelp):
  `msedge.dll+0x2f431fa` is `ExtensionKeybindingRegistryViews::OnHostActivationChanged+0xc`, a member read
  through a null pointer when the browser window's activation changes: Edge's extension shortcut registry gets an
  activation before it is set up (or after teardown). Not reproduced in WSL with X focus forced onto every Edge
  window as it maps, nor with a session-like locale and input method environment (`en_US.UTF-8`,
  `XMODIFIERS=@im=ibus`). The dump every working WSL run leaves is Crashpad's "dump without crash" (code
  `0x517a7ed`), not a crash. Asked of the Integration session: the session's environment, locale, keyboard layout,
  input method and window manager, and a guest crash dump to walk with the symbols.
- Environment ruled out (Integration session, 7fe233da): fresh wineservers with `env -i` and anything from a minimal
  set to the full session set (D-Bus, XDG, desktop names, `WEXMODE`) all crash; en_US.UTF-8, us/pc105, no input
  method. The guest's Crashpad dump holds no memory (empty stack descriptor and memory list), only the context:
  `rdx=1`/`rbx=1` (activation = true), `rax=rcx=0` (the null member). The guest loads the same extra modules as
  working WSL runs (oneauth, Kerberos, MSV1_0, jsproxy, windows.ui, msctf). In WSL, Wine's virtual desktop
  (`explorer /desktop`) starts Edge too. Next: a frame list from `WINEDEBUG=+seh,+unwind` in the guest, symbolized
  here, and `--disable-extensions` as a possible workaround (asked of the Integration session).
- **Cause found (2026-11-14): Edge's first-run experience.** Integration session: `--no-first-run`, or Edge's
  policy `HKLM\Software\Policies\Microsoft\Edge\HideFirstRunExperience=1` (REG_DWORD), makes Edge start in the
  guest; every WSL test had passed `--no-first-run`. Without it, WSL crashes identically
  (`unsloth-tmp/edge-prefix-test.sh` with `EDGE_FRE=1`).
- Full stack (an injected vectored handler walking the faulting context, `unsloth-tmp/b10trace/`, symbolized):
  `Browser::Browser` → `BrowserWindow::CreateBrowserWindow` → `BrowserWidget::InitBrowserWidget` →
  `views::Widget::Init` → `SendMessageW(WM_SYSCOMMAND, SC_MAXIMIZE)` (the first-run window starts maximized) →
  `HWNDMessageHandler::OnSysCommand` → `DefWindowProcW` → the hidden window is shown and activated →
  `WM_ACTIVATE` → `HWNDMessageHandler::PostProcessActivateMessage` → `BrowserView::OnWidgetActivationChanged` →
  `ExtensionKeybindingRegistryViews::OnHostActivationChanged` on a registry the unfinished `Browser` has not made.
- Wine's part is not the activation: a probe (`b10trace/scmax.c`) shows Windows 11 and Wine both show, activate
  and focus a hidden window synchronously on `DefWindowProc(WM_SYSCOMMAND, SC_MAXIMIZE)` (Wine adds one extra
  `WM_SIZE`). The difference is earlier: a WinEvent probe (`b10trace/evtprobe.c`) on Windows (Edge 157, fresh
  profile) shows Edge first creating and showing a full-screen modal first-run popup (style `0x86000000`) that
  takes the foreground, then the main frame maximized but hidden and disabled under it; under Wine the popup is
  never created, the main frame is shown and activated at once, and Edge's own code activates a half-built
  browser. Not the cause: the Windows version Wine reports (win11 crashes too), the screen size (2560x1440
  crashes too). Open: why Edge skips its modal first-run dialog under Wine.
- **Why the popup is missing (2026-11-14), a separate Wine bug:** breakpoints on Edge's decision chain
  (`b10trace/b10bp.c`): `edge_unified_fre::MaybeLaunchUnifiedFRE` returns false because `IsEdgeUnifiedFREEnabled`
  is false, because `edge::util::IsManagedOrEnterpriseDevice` is true, because `base::win::IsEnrolledToDomain` is
  true, which Chromium takes from `IsOS(OS_DOMAINMEMBER)`; Wine's `shcore` returned TRUE for it unconditionally (a
  FIXME). Every Chromium-based program saw a domain-joined, managed machine. Fixed: answer from
  `NetGetJoinInformation`. Forcing the popup on (feature flags) did not stop the crash, so this is not the trigger.
- **Root cause, in Wine (2026-11-15): `DefWindowProc(WM_SYSCOMMAND, SC_MAXIMIZE)` on a window that is already
  maximized.** Edge creates its frame hidden with `WS_MAXIMIZE`, then `Widget::Init` sends `SC_MAXIMIZE`. On
  Windows that is a no-op for a maximized window: the hidden frame stays hidden and inactive until Edge shows it,
  when the browser is complete. Wine called `ShowWindow(SW_MAXIMIZE)` regardless, which shows and activates it, so
  Edge's activation handler ran inside the `Browser` constructor. (The earlier probe compared a hidden window
  *without* `WS_MAXIMIZE`, where both systems show it.) Fix (branch `claude/wine-reuse`): `win32u/defwnd.c` leaves
  an already maximized window alone; test `test_SC_MAXIMIZE_maximized` in `user32/tests/win.c` passes on Windows 11
  and Wine. Verified 2026-11-15 (`edge-prefix-test.sh` with `EDGE_FRE=1`, fresh profile): Edge 156 starts its
  first run and stays up (9 processes, full-size DComp windows); the only dump is Chromium's own
  DumpWithoutCrashing report (code `0x517a7ed`), not a crash. **Confirmed on an installed machine** (Integration
  session, 2026-11-15, test ISO of master de8059c4 with Wine 31e80465, fresh install, BIOS): Edge 157.0.4341.60
  started plainly (fresh profile, no policy, no switches) stays up and shows its first-run page "Make Microsoft
  Edge your own"; the same with `--no-first-run`. **B10 is fixed.**
- Workaround until the fix lands: the `HideFirstRunExperience` policy (side effect: Edge shows "managed by your
  organization").
- Not a Wine bug, seen while testing (2026-11-15): fresh prefixes "hung" in wineboot's 32-bit `Wow64Install` step
  because the WSL kernel (6.18.33.2) livelocks in `filemap_get_read_batch` → `__xas_next` on a plain `read()` of one
  build file (`xaudio2_2/i386-windows/xaudio2_2.dll`, a corrupt page-cache entry). The readers cannot be killed,
  the stuck CPUs then stall RCU, and ext4 commits hang in every WSL distro. Moving the file aside (no read) and
  relinking it fixes new boots; only `wsl --shutdown` frees the stuck CPUs. Check `dmesg | grep 'soft lockup'`.
  Writes made while commits hang are lost by the shutdown: redo them (rebuilds, synced sources) afterwards.
- Left: other differences between the guest and WSL (a real kernel with its own devices: PCI, network, audio,
  input, `/proc/cpuinfo` and `/sys` layout), or naming the crashing function with Microsoft's public symbols for
  `msedge.dll` (a download from Microsoft's symbol server, needs the owner's OK).

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
