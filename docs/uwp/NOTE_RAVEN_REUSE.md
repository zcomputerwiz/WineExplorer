<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Note for the U1 agents: reusable parts of the Raven project (2026-11-11)

From the elevation track (Claude), at the owner's request. This is a proposal to evaluate, not a decision and not a
work order. Nothing from Raven has been copied into this repository or into the Wine fork.

## What Raven is

<https://github.com/zcomputerwiz/raven> (a fork of mirkobrombin/raven), read at commit `dc87fd0a` (2026-11-11). It runs
Windows programs on Linux without Wine: one Linux host process plays the Windows kernel, and a set of Windows-side
libraries ("guest adapters", PE DLLs under `native/guest/`) provide user-mode components. Only those guest adapters are
of interest here.

## Licences

| Part | Licence | Use in U1 |
| --- | --- | --- |
| Guest adapters listed by file in Raven's `LICENSES.json` (component "Raven guest adapters") | MIT, `Copyright (c) 2026 Mirko Brombin` | Compatible with Wine (LGPL-2.1-or-later) and with this repository (GPL-2.0-or-later); keep the copyright and permission notice |
| Headers generated from Microsoft's windows-rs metadata (`raven-appx/appxpackaging.h`, `raven-winrt/foundation.h`) | MIT (Microsoft) | Allowed by the project rules as an open Microsoft release with its notice. They are not in the MIT file list of `LICENSES.json`; their own file header states the origin. Wine's own IDL is preferable where it exists |
| Linux host (`native/nt`, `native/pe`, `native/desktop`, `native/graphics`, `scripts`) | GPL-3.0-or-later | Do not copy. Concepts only (`CLAUDE.md`: GPL-3.0 code needs a deliberate decision) |

`LICENSES.json` is authoritative per file. Check every file taken against that list; a file under `native/guest/` that
is not listed is not covered by the MIT grant.

## Candidates

1. **Package reader, `native/guest/raven-appx/`** (`archive.c`, `blockmap.c`, `manifest.c`, `package.c`, `support.c`,
   `appx.h`; about 2,700 lines, all in the MIT list). Implements the packaging API classes `AppxFactory` and
   `AppxBundleFactory`: opens `.appx`/`.msix` as a ZIP archive (ZIP64, stored and deflate through zlib), requires
   manifest, block map and content types, checks every payload file block by block against the block map
   (`APPX_E_CORRUPT_CONTENT`), and reads the manifest (identity with full and family name, properties, dependencies,
   target device families, capabilities, resources, applications). It uses ordinary Win32 only: `objidl`, `bcrypt`
   (SHA-256), XmlLite, zlib. No call into Raven's host was found in these files.
   Stated as not implemented by its own comments: writing packages, reading bundles (`E_NOTIMPL`), block maps
   validated against a signature, the manifest readers 2 to 7, schema validation. The signature file is handed out
   as it is; trust is left to `WinVerifyTrust`.
   Relevance: `UWP_COMPATIBILITY_PLAN.md` names the MSIX SDK (microsoft/msix-packaging, MIT) as the preferred parser.
   The Raven reader is much smaller, is plain C against Win32 and could sit in a Wine DLL (`appxpackaging`-style
   classes) without a C++ runtime or bundled third-party libraries beyond zlib. It does less (no bundles, no signature
   validation). Worth a side-by-side before the Phase 2 parser choice is fixed.
2. **WinRT object plumbing, `native/guest/raven-winrt/`** (`winrt.c`, `winrt.h`; about 1,450 lines, MIT). Shared
   helpers for writing runtime classes in C: objects answering several interfaces with one reference count, the
   common `IInspectable` methods, asynchronous operations, boxed values, string maps with iterators. Possibly useful
   for self-owned fixtures and for small runtime classes; Wine's WinRT DLLs have their own conventions, so for code
   that goes into Wine this is a reference, not a drop-in.
3. **Start-up runtime classes, `native/guest/raven-twinapi/twinapi.c`** (about 1,770 lines, MIT). Classes desktop
   programs ask for while starting: `Windows.Globalization.Language`, `Windows.System.Profile` (RetailInfo,
   SharedModeSettings, AnalyticsInfo), `Windows.Networking.Connectivity.NetworkInformation`, the online-id and web
   account managers (answering that there is no account), `Windows.Foundation.Metadata.ApiInformation`. It has one
   optional trace call into Raven's host (`ravenhost.dll`, `RavenTrace`, looked up at run time) that would have to be
   removed. Compare with what Wine already has before taking anything: several of these classes exist in Wine.
4. **Package manager stub, `native/guest/raven-deployment/deployment.c`** (about 330 lines, MIT).
   `Windows.Management.Deployment.PackageManager` answering "no packages" for every query. Only a starting shape; U1
   needs the real installed-package model.

## What was not checked

Nothing was built or run. The claims above come from reading the sources and their comments; correctness of the reader
(ZIP edge cases, hostile archives, hash handling) was not reviewed, and it must get the same security review as any
parser of untrusted input (`UWP_COMPATIBILITY_PLAN.md`, security audit item). Wine's current state of the same
classes was not compared. Raven's upstream is young and moves quickly; pin a commit if anything is taken.

## Suggested next step for U1

One agent compares `raven-appx` with the MSIX SDK for the U1.0 fixture package (open, enumerate, verify block hashes,
read identity and dependencies) and records the result in `EXECUTION_PLAN.md`. Until then this note changes nothing in
the plan.
