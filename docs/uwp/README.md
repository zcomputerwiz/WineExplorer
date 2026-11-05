<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# UWP / package compatibility (track U1)

Status: planned, not started. Nothing here is implemented. Documents:

* `UWP_COMPATIBILITY_PLAN.md` - the proposed plan (original, kept as written).
* `CODEX_REVIEW.md` - static review of the plan against the repo (2026-11-05).
* This file - the decision: where the track sits and what is built first.

## Decision (2026-11-05)

1. **New roadmap track U1**, after the open DWM, browser-installer and system-tier work (`ROADMAP.md`). Completed
   phases are not renumbered. Inventory (U1.0 item 1) and the Windows-host fixture build can run in parallel with the
   open work; Wine changes wait until the browser-installer service and registry-ACL blockers are closed, because both
   tracks change the same token, service and registry code.
2. **The plan's first target is replaced.** The first milestone is U1.0: a feature-gated, self-owned, packaged native
   C++/WinRT fixture with no XAML. It runs the same package bits on Windows 11 and on Wine, reads its identity
   (`GetCurrentPackage*`), initialises WinRT, activates a class, completes one async operation and exits.
   CoreWindow is U1.1. XAML (Uno or native), PRI/XBF, `.NET Native` and the real Calculator are separate later tracks,
   each with its own gate. Reason (Codex review, checked against the tree): `corewindow.c`, `core_application.c`,
   `windows.ui.xaml` and `appxdeploymentclient` are almost entirely `E_NOTIMPL` shells, and `RoActivateInstance` is a
   factory call-through.
3. **Everything is off by default** behind one registry gate (name to be fixed in U1.0, under
   `HKCU\Software\WineExplorer\Shell` like the other gates). Failure for an unsupported identity or activation is an
   error, never a fabricated success. No AppContainer or sandbox claim is made.
4. **Windows Calculator rule.** The installed Microsoft package, its framework packages and any extracted Windows files
   never enter this repository, CI or releases (`CLAUDE.md`). A private local test is allowed only from an untracked
   directory outside the repository, only where the owner is entitled to use the package, with only hashes and a
   pass/partial/blocked verdict recorded here. If entitlement is unclear it is not copied. A Calculator built from the
   MIT source is a separate fixture and does not satisfy the installed-package goal. Calculator is U3 and may end in
   "blocked".

## Repositories

| Where | What | Notes |
|---|---|---|
| `wine/` (fork, LGPL) | WinRT ABI, activation, package identity and context, CoreWindow contracts | each change listed in `STOCK_CHANGES.md`; upstream-shaped |
| `WineExplorer` (GPL-2.0-or-later) | the feature gate, the per-prefix install/uninstall command, shell integration, test harness, these docs | no parser, no XAML runtime here |
| **new: `WineExplorer-UWP-Fixtures`** (public) | self-owned C++/WinRT fixtures, manifests, build scripts, hashes, result schema | needed first (U1.0 item 2); built on the Windows host |
| **new: `WineExplorer-UWP-Tools`** (public) | package inspection / store prototype, safe extraction, PRI/XBF inspectors | needed at U1.2; Apache-2.0 and MIT code stays here |
| [`unoplatform/uno`](https://github.com/unoplatform/uno) (official, reference only) | design reference for a XAML backend and its native-ABI limits | not forked and not imported (owner decision 2026-11-05); Apache-2.0 code stays out of this repository and of `wine/` |
| `../WineExplorer-local` (existing, private) | Windows-host oracle logs, screenshots, package inventories | replaces the proposed third repo `WineExplorer-UWP-Oracle` |

Licence note for the owner (not a legal opinion): Apache-2.0 (Uno, XbfTools, PriTools) is not GPLv2-only compatible; the
project is GPL-2.0-or-later, so a combined work distributed as GPLv3-or-later can include Apache-2.0 code, with notices.
That does not make Apache-2.0 code suitable inside an LGPL-2.1 Wine DLL. Until the owner decides, Apache-2.0 code stays
in the separate repositories and in separate processes, and nothing from Uno enters `wine/`.

## First backlog (all behind the gate)

1. Codex, static: freeze SHAs, module inventory (stub vs real, with file and line), licence ledger, gate contract.
   Output in `docs/uwp/INVENTORY.md`.
2. Antigravity on the Windows host: C++/WinRT nonvisual fixture, x64 and i386, packaged; oracle log kept private.
   Blocked on the Fixtures repo and on the C++ UWP workload being installed on the host.
3. Qwen: gated conformance probe for package identity, HSTRING, `RoInitialize`, factory lookup, `RoActivateInstance`.
4. Claude: review of ABI, vtable, threading and lifetime changes.
5. Qwen: smallest per-prefix package identity and manifest activation path with atomic install and rollback.
6. Antigravity: run the unchanged fixture package on the WoW64 build; record identity, trace, exit code.
7. Codex: static audit of services, registry ACLs, elevation and rollback paths touched.
8. Claude: gate review for U1.0; decide whether U1.1 (CoreWindow) starts.

Acceptance for every item names the exact Wine build, prefix, commands, logs and a pass, partial, blocked or not-run
verdict; static-only and not-run results are labelled as such.
