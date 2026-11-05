<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Review: UWP compatibility plan

## Verdict

The plan is a useful research outline, but it is too large to be a current
implementation track and it puts the Calculator milestone too close to the
front of the narrative. The first credible target is a feature-gated,
packaged, native C++/WinRT **non-XAML** fixture that reaches its own startup
code and exits. A real XAML fixture, Uno interop, .NET Native, and Calculator
are later capability tracks, not prerequisites that can be collapsed into one
"UWP support" phase.

This review is static. No build, guest run, Windows oracle run, package
installation, screenshot, or legal determination was performed.

## 1. Placement in the roadmap

This is a new numbered track after the currently open verification and
presentation work, not a replacement for an existing roadmap phase. I
recommend naming it **U1--UWP/package compatibility**, with subphases U1.0,
U1.1, etc.; do not renumber the completed Phases 1--11.

The ordering should be:

1. Close or explicitly freeze the current DWM/DComp slices needed by
   application windows. `docs/ROADMAP.md` says the browser work is below the
   DWM phases, while `docs/DWM_ARCHITECTURE.md` still records child-HWND
   Chromium targets and Wayland as unfinished. UWP must reuse that boundary,
   not introduce a second compositor.
2. Continue the browser-installer work far enough to resolve its existing
   service/registry-DACL/elevation blockers, or record a deliberate pause.
   `docs/ROADMAP.md` places stock installers after DWM and records Chrome's
   service/COM failure; `docs/BROWSER_INSTALLER_TEST_PLAN.md` calls the
   registry-DACL blocker unresolved. This work is a useful security and
   package-installation dependency, but UWP should not wait for every Edge
   result.
3. Stabilize the session-0/system-tier boundary before claiming machine-wide
   package registration. The roadmap's Future: system tier describes one
   machine `services.exe`/HKLM owner and cross-user wineserver work; UWP's
   first package store should therefore be per-prefix/per-user and explicitly
   avoid pretending to be the final system tier.
4. Finish the ISO/installed-system acceptance gates that the handoff still
   lists as open (DComp/native-app, Wayland, physical-output, upgrade and
   rollback checks). The UWP fixture can be developed earlier, but its
   acceptance must name the exact build and prefix rather than inherit an ISO
   claim.
5. Start U1.0 inventory and the Windows-host oracle in parallel with the
   above. Only after the nonvisual fixture works should U1.2 begin graphical
   CoreWindow work, and only after that should XAML be a separate U2 track.

Existing prerequisites are concrete:

* **DComp/DXGI:** reuse the retained scene and surface producer path. The
  staging plan says it is not a rebase candidate and that child-HWND,
  alpha/premultiplied targets and Wayland remain distinct work
  (`docs/DCOMP_STAGING_PLAN.md`, sections “Executive findings” and “Ours”).
* **Services/COM LocalService:** packaged apps and dependencies may need
  service activation, but the current LocalService fix is specifically a
  Chrome updater fix, not proof of a UWP service model. It is documented in
  `docs/STOCK_CHANGES.md` under “combase: LocalService name buffer and
  ServiceParameters”.
* **Registry ACLs/elevation:** package installation must use the existing
  token, service, registry descriptor, and broker boundaries. The stock-change
  log records `WRITE_DAC`, system-role, service-token, and elevation-broker
  work; do not make package installation an ACL bypass or broaden the
  privileged path.
* **WoW64:** every proposed acceptance must use the project’s combined
  `--enable-archs=i386,x86_64` build. `CLAUDE.md` makes this a repository rule;
  x64-only UWP evidence is insufficient.

## 2. Placement and repository/licence boundary

### Wine (`wine/`, LGPL boundary)

Put only Windows API/runtime semantics here: WinRT ABI and apartment behavior
in `wine/dlls/combase`, package identity/context in the appropriate Wine
modules and `kernelbase`, manifest-driven activation, CoreApplication/CoreWindow
contracts, HSTRING/IInspectable/async/event behavior, and any DComp API
contract needed by an application. Every stock-Wine edit must be listed in
`docs/STOCK_CHANGES.md` and retain the existing Wine notices.

The current tree is not close to complete. `wine/dlls/combase/roapi.c:127-180`
implements RoInitialize/RoGetActivationFactory through activation-context or
registry DLL lookup, but labels the path semi-stub; `RoActivateInstance` at
about lines 228-240 is only a factory call-through. `kernelbase/version.c`
does export and implement `GetCurrentPackageFamilyName`,
`GetCurrentPackageFullName`, `GetPackageFullName`, and `GetPackageFamilyName`
(about lines 1559-1623), but the presence of these exports is not proof of a
working package identity context. Several conversion/verification exports in
`kernelbase.spec` remain stubs.

### WineExplorer (`WineExplorer`, GPL project boundary)

Keep shell-facing package UX here: opt-in registry/feature flag, per-prefix
install/uninstall command or UI, Start-menu/shortcut integration, diagnostics,
test isolation, and the existing screenshot/pixel evidence harness. Keep
project-specific Linux/X11/Wayland policy and shell integration out of Wine
DLLs. Do not put a general MSIX parser or an Uno runtime in this repository.

### Separate repositories

Create exactly these new repositories:

1. **`zcomputerwiz/WineExplorer-UWP-Tools`**: open-package inspection,
   package-store transaction prototype, manifest/dependency model, safe
   extraction, and standalone PRI/XBF inspection tools. It is a tool boundary,
   not a DLL dependency of Wine until a separate licence and ABI review is
   accepted.
2. **`zcomputerwiz/WineExplorer-UWP-Fixtures`**: self-owned native C++/WinRT
   fixtures, manifests, build scripts, test commands, hashes, and machine-
   readable result schemas. It may contain a small XAML fixture only after the
   non-XAML gate. It must contain no Microsoft-installed package.
3. **`zcomputerwiz/WineExplorer-UWP-Oracle`** (private): Windows-host scripts,
   private oracle logs, screenshots, package inventories, and local hashes.
   Public documentation may describe observations and schemas, but private
   Windows package contents and protected Store assets do not belong in the
   project or public CI.

(Superseded: the owner dropped the fork; Uno is reference-only.) Originally: use the already available **`zcomputerwiz/uno-uwp`** as the separate XAML
research/backend repository. Do not copy its managed implementation into
Wine or WineExplorer. A small adapter/prototype can live there until a native
WinRT ABI and licence decision exists; no XAML backend should be added as a
WineExplorer subdirectory now.

Apache-2.0 is not compatible with a GPLv2-only combined work, but this project
is GPL-2.0-or-later, so GPLv3 distribution can in principle combine Apache-2.0
code subject to Apache notice/patent/redistribution terms. That does **not**
make Apache-2.0 code automatically suitable for an LGPL-2.1 Wine DLL, nor does
separate directories or dynamic linking automatically settle derivative-work
questions. The owner needs a legal decision on: (a) whether any combined
distribution is expressly GPLv3-or-later, (b) whether Uno/tools remain separate
processes, and (c) whether their transitive dependencies have compatible
licences. Until then, use MIT/open-interface artifacts as references and keep
Apache components in the named separate repositories.

## 3. Critique of the proposed phases

### Observed gaps and optimistic assumptions

* **Uno is not a native UWP ABI backend by being a XAML framework.** A compiled
  native UWP app asks for WinRT runtime classes, factories, IIDs, vtables,
  reference identity, dependency properties, events, UI-thread affinity and
  compiled-resource loading. A managed Uno `Button` is not evidence that a
  native `Windows.UI.Xaml.Controls.Button` ABI exists. The plan correctly
  identifies this risk, but the proposed work should be a hard stop unless a
  one-control native wrapper prototype passes.
* **CoreWindow is currently skeletal.** `wine/dlls/windows.ui/corewindow.c`
  has E_NOTIMPL methods around lines 84-102. `twinapi.appcore/core_application.c`
  has the same pattern around lines 75-93; the surrounding appcore files are
  similarly mostly generated object shells. This makes “Phase 4 lifecycle” a
  substantial runtime project, not a small activation follow-up.
* **Windows.UI.Xaml is not a real XAML framework here.** The tree contains
  `windows.ui.xaml/color_helper.c`, whose IInspectable methods return E_NOTIMPL
  around lines 83-101, plus tests that exercise factory lookup; this is not
  evidence of controls, layout, templates, namescopes, bindings,
  `InitializeComponent`, XBF loading, or a retained UI tree. `windows.ui` also
  has broad E_NOTIMPL surfaces in `uisettings.c`, `uiviewsettings.c`,
  `inputpane.c`, and `corewindow.c`.
* **Package/deployment code is mostly a shell.**
  `wine/dlls/windows.applicationmodel/package.c` has many E_NOTIMPL property
  methods, and `wine/dlls/appxdeploymentclient/package.c` returns E_NOTIMPL for
  Add/Register/Find/Remove and package-manager queries. A package store,
  identity context, dependency graph, activation context and rollback are
  missing as a coherent feature even though some API objects and IDLs exist.
* **Calculator is a poor early milestone.** Its version-specific framework,
  VCLibs, WinUI/XAML, resource, and possibly .NET Native dependencies must be
  inventoried on the stated Windows host. “Phase 8 real Calculator” is valid
  only as a later attempt and may end in a lawful dependency blocker. A source-
  built Calculator is useful as a separate fixture but cannot satisfy the
  installed-Calculator criterion.
* **.NET Native, MRT/PRI, XBF, AppContainer, brokers and signing are separate
  projects.** The plan names them, but the exit gates need to prevent a native
  C++ fixture from silently expanding into a claim about managed apps,
  compiled XAML, resource qualification, or sandbox enforcement.

### Proprietary-material rule

The repository may contain self-owned fixtures, open-source code with reviewed
licences, public API/WinMD contracts, and observations. It must never commit
genuine Windows DLLs, Store packages, extracted Calculator files, Windows
reference sources, proprietary fonts/art, or harvested runtime packages.
For a private local test, an authorized owner may place an unchanged package or
dependency in an untracked private test root outside the repository, record
only hashes/metadata and a pass/partial/blocked result, and remove it after
the run. If authority is uncertain, do not copy it at all. Do not attach such
files to CI, releases, patch reviews, or this repository.

### Smaller first milestone

Replace the plan’s initial target with **U1.0: packaged identity + native
nonvisual CoreApplication fixture**:

* self-owned x64 and i386 C++/WinRT fixture, same package bits on Windows and
  Wine;
* package manifest read from a safe per-prefix store;
* `GetCurrentPackage*`, `RoInitialize`, HSTRING, activation factory,
  `RoActivateInstance`, one async completion and clean shutdown;
* no XAML, no Calculator, no Store dependency, no AppContainer security claim;
* registry/feature flag off by default, with failure rather than a fabricated
  success for unsupported identity or activation.

Only after this passes should CoreWindow become U1.1, and only after a native
control ABI spike should the XAML work become U2.

## 4. First eight ordered work items

All eight are opt-in behind a registry/feature flag (for example an
uncommitted name to be designed in U1.0); default-off behavior must preserve
ordinary Win32 execution. The owner types below are assignments, not claims
that those agents have run anything.

| # | Owner type | Work | Acceptance evidence |
|---|---|---|---|
| 1 | Codex static | Freeze SHAs, submodule/Wine branch, named module inventory, licence ledger, and feature-flag contract. | `docs/uwp/` matrix citing source lines; `git diff --check`; no runtime claim. |
| 2 | Antigravity lab/Windows-host | Build a self-owned native C++/WinRT nonvisual fixture x64 and i386; capture Windows build, manifest, imports, hashes and oracle result. | Private oracle log plus public fixture source/build recipe; no Microsoft binaries committed. |
| 3 | Qwen build/test | Add a disabled-by-default Wine conformance probe for package identity, HSTRING, RoInitialize, factory lookup and `RoActivateInstance`; do not implement broad stubs. | x86/x64 test logs showing off-mode unchanged and explicit unsupported failures; Windows comparison where available. |
| 4 | Claude review | Review ABI/vtable/IID/threading/lifetime changes in `combase`, `kernelbase`, appmodel and activation-context paths; check `STOCK_CHANGES.md` entry and LGPL boundary. | Written review with callers, ownership, WoW64 layout and exact unrun guest checks. |
| 5 | Qwen build/test | Implement the smallest per-prefix package identity/store record and manifest activation path, with atomic install/rollback and path-traversal rejection. | Fresh-prefix install/query/uninstall logs, malformed-package negatives, x86/x64 results; feature remains off by default. |
| 6 | Antigravity lab/Windows-host | Run the unchanged fixture package on the selected Wine WoW64 build and capture process identity, activation trace, clean exit and presented evidence where applicable. | Exact command, prefix, SHAs, stdout/stderr, exit code and verdict; no screenshot is required for nonvisual success. |
| 7 | Codex static | Re-read all edited Wine callers and package-store boundaries; audit services, registry ACLs, elevation, AppContainer claims and rollback publication. | Static report; `STOCK_CHANGES.md` complete; no claim of sandbox support without evidence. |
| 8 | Claude review | Gate U1.0 completion and decide whether CoreWindow/XAML is authorized as a new slice; reject Calculator/Uno claims if the ABI fixture is not green. | Signed review decision listing pass, partial, blocked and exact next prerequisites. |

The first visual milestone should then be a self-owned CoreWindow/native
window fixture, not Calculator. XAML/Uno, PRI/XBF and .NET Native must each
have their own acceptance record and feature gate.

## Sources inspected

`CLAUDE.md`; `docs/ROADMAP.md` (including Future sections);
`docs/HANDOFF.md`; `docs/STOCK_CHANGES.md`; `docs/DWM_ARCHITECTURE.md`;
`docs/DCOMP_STAGING_PLAN.md`; `docs/STAGING_OVERLAP.md`;
`docs/BROWSER_INSTALLER_TEST_PLAN.md`; the proposed plan;
`wine/dlls/combase/roapi.c`; `wine/dlls/kernelbase/version.c` and
`kernelbase.spec`; `wine/dlls/windows.applicationmodel/package.c`;
`wine/dlls/windows.ui/corewindow.c`; `wine/dlls/twinapi.appcore/core_application.c`;
`wine/dlls/windows.ui.xaml/color_helper.c` and its tests;
`wine/dlls/appxdeploymentclient/package.c`; and the corresponding
`windows.storage`, `coremessaging`, `twinapi.appcore`, and `windows.ui` source
trees. This was source inspection only; no files other than this review were
written.
