<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# UWP / package compatibility (track U1)

## Status (2026-11-13)

Each item was measured on Windows 11 with a self-owned probe, and the same binaries pass on Wine x64 and x86 in the
owned U1 guest. Wine code is on `codex/uwp-runtime`, merged to Wine `development` (9484da1a). Evidence and findings:
`tests/uwp/u1_0`, `tests/uwp/u1_1/README.md`, `tests/uwp/u1_3/README.md`; Wine changes: `docs/STOCK_CHANGES.md`.

| Item | State | Checks |
|---|---|---|
| U1.0 package identity, activation of a packaged component | done | fixture, 14 context negatives |
| U1.1 view lifecycle (`CoreApplication.Run`, `CoreWindow`, `CoreDispatcher`) | done | 32, 30, 3 |
| U1.1b presentation and input (`CreateSwapChainForCoreWindow`, key and pointer events) | done | 29 |
| U1.1c suspend and resume | probe built, not measured (needs a person at the Windows machine) | |
| U1.2 package information (`GetCurrentPackageInfo` and related) | done | 39 |
| U1.3 package dependencies (framework packages on the DLL search path) | done | `tests/uwp/u1_3` |
| U1.4 `ApplicationData` (folders, settings, composite values) | done | 24, 26 |
| U1.5 `StorageFolder`, `StorageFile`, `FileIO`, `PathIO`, file streams, `DataReader`, `DataWriter` | done | 87 |
| Package installer (`wexappx`) | done, without a signature check | 63, 59 |
| MRT/PRI resources (`ResourceLoader`, `ms-resource:`) | not started | |
| Installer signature check, server-side compatibility gate | not started | |
| `Windows.UI.Xaml` (U2) | plan only: `PLAN_XAML.md`, waits for the owner's decision | |

Not done inside the finished items: thumbnails, file queries, `KnownFolders`, pickers, `GetParentAsync`,
`MapChanged` events of the settings, `GetPublisherCacheFolder`. Storage operations finish on the calling thread.

The sections below are the decisions of 2026-11-05 and the plan as it was written then; where they say something is
unimplemented, the table above is newer. See `EXECUTION_PLAN.md` for the earlier evidence and sequencing. Documents:

* `UWP_COMPATIBILITY_PLAN.md` - the proposed plan, kept as written. Where it conflicts with this file, this file wins.
  In particular every proposal in it to build, fork or bridge to a managed (Uno) backend is superseded (see Uno).
* `CODEX_REVIEW.md` - first static review (2026-11-05). Its U1.2 mention of CoreWindow and its third repository are
  superseded by this file.
* `CODEX_REVIEW_SOL.md` - second, stronger static review; it verified the first against the source and added the
  Windows contracts below. Its corrections are adopted here.
* `INVENTORY.md` - static module inventory and gate proposal (parent SHA is older than HEAD; the Wine pin still matches).

## Decision (2026-11-05, revised after the second review)

1. **New roadmap track U1**, after the open DWM, browser-installer and system-tier work (`ROADMAP.md`). Completed phases
   are not renumbered. Priority is a scheduling choice: finishing the ISO or system-tier work is not a technical
   prerequisite for a per-user, per-prefix nonvisual probe. The real dependencies are the token, service and registry-ACL
   code that the browser-installer work is changing (U1 Wine changes wait for it) and machine-wide registration, which
   waits for the system-tier model. The inventory and the Windows-host fixture can start now.
2. **U1.0: packaged process identity and native WinRT component activation.** A self-owned, gated, packaged native
   C++/WinRT fixture, same package bits on Windows 11 and on Wine, with a self-owned component DLL that declares an
   in-process activatable class in its manifest. It checks that the process identity, the token and the activation agree,
   activates that class through the package resolver (not a global registration), completes one specified async
   operation, handles missing class and missing DLL, and exits. Negative cases are part of acceptance. Its application
   model is fixed before any launch code is written: a packaged full-trust console probe. It does **not** prove UWP
   lifecycle, and the docs must not claim it does. Direct EXE execution is never substituted for package activation.
3. **U1.1: UWP lifecycle.** `CoreApplication.Run` with an `IFrameworkViewSource`, view, `CoreWindow` and `CoreDispatcher`
   callbacks (order, thread, apartment), launch activation, event pumping, clean termination, later
   suspend/resume. `CoreWindow` and `CoreApplication` are near-empty shells today. A presented fixture needs a
   `CoreWindow` DXGI target: `CreateSwapChainForCoreWindow` is `E_NOTIMPL` (`wine/dlls/dxgi/factory.c`) and desktop DComp
   success does not cover it or `Windows.UI.Composition`. The dispatcher in use is named (CoreDispatcher, Windows.System
   `DispatcherQueue` and WinAppSDK's queue are different contracts).
4. **Brought forward from the old XAML phase:** the package dependency and import closure (VCLibs and framework
   packages by exact name, publisher, version and architecture; missing and wrong-version negatives), and a small
   non-XAML MRT/PRI string-resource probe (`ms-resource:`, `ResourceLoader`). XBF and compiled XAML stay in U2.
5. **U2 XAML, U3 `.NET Native`/Calculator** are separate later tracks, each with its own gate and acceptance record.
6. **Feature gate.** `HKCU\Software\WineExplorer\Shell\UwpCompatibility` (REG_DWORD, default 0, exact 1 enables; see
   `INVENTORY.md`). It gates the creation and use of the **new package context and resolver** only. It is not put in front
   of every `RoGetActivationFactory`: ordinary Wine WinRT lookup (activation context, then registry) is unchanged, so
   unpackaged programs behave as before. A running process keeps its identity when the value changes. Unsupported
   identity or activation fails with an error, never a fabricated success. Package identity is not AppContainer: no
   sandbox claim is made, and token restrictions are never relaxed to make a fixture pass. The existing LowBox token work
   in the fork is partial machinery, not verified containment.
7. **Windows 11 is a versioned comparison environment, not truth.** Pin OS build, SDK and toolset, package and dependency
   versions, architecture, locale, DPI and activation route; also run clean-account Release builds, because an installed
   framework or debugger can hide a dependency. Compare documented observable contracts only.
8. **Windows Calculator is a conditional private experiment** (U3), not an acceptance milestone. The installed Microsoft
   package, its frameworks and any extracted Windows files never enter this repository, CI or releases. Private material
   lives only in `../WineExplorer-local` (as `CLAUDE.md` requires), not in an arbitrary untracked folder in the public
   tree; screenshots, manifests, resource strings and logs that embed assets count as the same material. It is tried only
   where the owner is entitled to use the package, and only hashes and a pass, partial or blocked verdict are written
   here. A source-built Calculator is a separate fixture. Open point for the owner: `CLAUDE.md` says proprietary Microsoft
   material is "never named" in the repository, while these docs name the installed product; keep detail private, and
   decide whether the product name is an accepted exception.

## Uno

Owner decision: use the official `unoplatform/uno` repository as a **reference only**. No fork, no import, no adapter, no
bridge, no separate-process backend. The Apache-2.0 code stays out of this repository and out of `wine/`. A XAML backend,
if U2 ever happens, is native and original (the WinUI 2 MIT sources are a design reference, licence claims unverified).
The old `uno-uwp` fork is abandoned and must not be used for this track.

## Repositories

| Where | What | Notes |
|---|---|---|
| `wine/` (fork, LGPL) | WinRT ABI, activation, package identity and context, CoreWindow contracts | each change listed in `STOCK_CHANGES.md`; upstream-shaped |
| `WineExplorer` (GPL-2.0-or-later) | the feature gate, the per-prefix install/uninstall command, shell integration, test harness, these docs | no parser, no XAML runtime here |
| **new: `WineExplorer-UWP-Fixtures`** (public) | self-owned C++/WinRT fixtures, manifests, build scripts, hashes, result schema | needed first (U1.0 item 2); built on the Windows host |
| **new: `WineExplorer-UWP-Tools`** (public) | package inspection and store prototype, safe extraction, PRI/XBF inspectors | needed when package parsing starts; separately reviewed tools only |
| [`unoplatform/uno`](https://github.com/unoplatform/uno) | design reference | not forked, not imported |
| `../WineExplorer-local` (private) | Windows-host oracle logs and inventories, any proprietary material | the only place for it |

Licence status: every licence named for an external project (MIT for the MSIX SDK, C++/WinRT, C#/WinRT, WinUI, the
Calculator source; Apache-2.0 for Uno, XbfTools and PriTools) is a claim from the plan and is **unverified** until a
pinned licence text and dependency audit are recorded. Keep a pinned licence and notice ledger before any tool is
imported. A separate repository or process does not by itself settle compatibility. Apache-2.0 is not GPLv2-only
compatible; the project is GPL-2.0-or-later, so a combined work distributed as GPLv3-or-later can include Apache-2.0
code with notices, which does not make it suitable inside an LGPL-2.1 Wine DLL. Not legal advice; decisions are the
owner's.

## First backlog (all behind the gate)

1. Codex, static: inventory (done, `INVENTORY.md`); refresh its parent SHA when changes land.
2. Antigravity: fixture sources exist in `tests/uwp/u1_0/`; MSVC x64/x86 builds and an x64 packaged Windows oracle
   pass are recorded there. The full-trust fixture uses the desktop workload; a C++ UWP workload and a new repository
   are not prerequisites. Windows x86 packaged activation and complete same-binary/hash evidence remain open.
3. Qwen: gated conformance probe for package identity (process, token, external handle), `RoInitialize`, HSTRING,
   package-resolver activation and `RoActivateInstance`. Today all seven `GetCurrentPackage*` queries are
   `APPMODEL_ERROR_NO_PACKAGE` stubs and `RtlQueryPackageIdentity` takes a token and returns not found.
4. Claude: review of ABI, vtable, threading and lifetime changes.
5. Qwen: smallest per-prefix package record and manifest activation path with atomic install and rollback, path-traversal
   rejection, same class name in two packages, dependency visibility.
6. Antigravity: run the unchanged fixture on the WoW64 build; identity, trace, exit code, negative cases.
7. Codex: static audit of services, registry ACLs, elevation and rollback paths touched.
8. Claude: gate review for U1.0; decide whether U1.1 starts.

Acceptance for every item names the exact Wine build, prefix, commands, logs and a pass, partial, blocked or not-run
verdict; static-only and not-run results are labelled as such.
