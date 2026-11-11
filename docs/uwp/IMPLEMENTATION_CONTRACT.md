<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1.0 implementation and acceptance contract

Planning owner: Codex. Implementers: Qwen and Antigravity. Status: design gate;
this document does not establish working package support. The existing U1 README
and current owner authorization in `EXECUTION_PLAN.md` govern scope.

## Concrete next result

The unchanged, self-owned full-trust package from `tests/uwp/u1_0/` must be
installed into a fresh prefix and activated by its AUMID through
`IApplicationActivationManager::ActivateApplication`. Its original MSVC-built
probe and component must report consistent process/token identity, package
path, package-provided factory, component instance and completed folder async
operation. It must fail with the observed missing-class/missing-DLL HRESULTs.
The same binary hashes must be used on Windows and Wine. Direct EXE launch is
the unpackaged control, never a substitute for package activation.

This is the first gate, not UWP lifecycle, AppContainer containment or XAML.
Lifecycle, CoreWindow presentation/input/dispatcher, resource/dependency closure
and later framework gates remain required by the roadmap.

## Source facts that determine packages

At isolated Wine base `c3f59e05a37134aee529a7d636af3ba67a9efc5c`:

* `dlls/kernelbase/version.c` current/explicit process identity APIs return
  `APPMODEL_ERROR_NO_PACKAGE`; `dlls/ntdll/rtl.c:RtlQueryPackageIdentity` returns
  `STATUS_NOT_FOUND`. These are not package context implementations.
* `dlls/kernelbase/kernelbase.spec` comments out both token identity exports.
  The unchanged Windows probes import these; loader compatibility must be
  established before trying to interpret their runtime results.
* `dlls/kernelbase/version.c:PackageFullNameFromId` overwrites `*length` before
  comparing available capacity, and substitutes a fixed publisher ID when only
  a publisher is supplied. Neither behavior is acceptable input to the package
  catalog: add native size/publisher observations and repair these paths before
  relying on the helper. In particular, a small caller buffer must not be
  overrun and an unknown publisher must not acquire a fabricated identity.
  The NULL `resourceId` path also omits the empty-field separator; compare
  NULL/empty/nonempty resource IDs with native Windows before changing it.
* `include/winternl.h` declares `RtlQueryPackageIdentity(HANDLE,WCHAR*,SIZE_T*,
  WCHAR*,SIZE_T*,BOOLEAN*)`. The preparatory probe's empty disabled branch is
  not an implementation, and an invented information-class ABI is not allowed.
* `dlls/combase/roapi.c:get_library_for_classid` searches activation context,
  then global WinRT registration. New package lookup must not gate or rewrite
  ordinary unpackaged WinRT lookup. A declared package class must resolve to
  its package-local module; load errors must not silently select a global DLL.
* `dlls/windows.storage/classes.idl` and `main.c` expose stream classes, not
  `Windows.Storage.StorageFolder`. The fixture's async call is a real additional
  implementation requirement. Current upstream versions inspected at
  [main.c](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/windows.storage/main.c)
  and [classes.idl](https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/windows.storage/classes.idl)
  show the same gap; no upstream patch has been imported. Targeted GitHub code
  searches of `wine-staging/wine-staging` for `GetFolderFromPathAsync` and
  `IStorageFolderStatics` returned no results; this is a scoped search, not
  an exhaustive audit of all historical staging patches.
* `server/process.c:create_process` duplicates the primary token. Package
  context must remain consistent through that path without changing elevation,
  groups, privilege, desktop or ACL decisions. Work is isolated from the active
  elevation checkout and must be reviewed against its later integration base.

## Runtime invariants

1. The gate is the exact DWORD value 1 at
   `HKCU\Software\WineExplorer\Shell\UwpCompatibility`. Absent, zero, wrong
   type and malformed values leave new package context/resolution disabled.
   Existing WinRT activation keeps its ordinary behavior. Changing the gate
   does not erase or fabricate identity in an already running process.
2. Package identity is immutable process/token context created only by the
   package activation path. Environment variables, executable directory names,
   arbitrary PEB pointers and global class registrations are not evidence of
   package activation. The server-owned context must be queryable through an
   authorized explicit process/token handle as well as current-process APIs.
3. Full-trust package identity confers no administrator privilege or AppContainer
   guarantee. Existing token validation, ACL/access checks and elevation paths
   remain operative. Failure to create context leaves no launched process or
   leaked process/thread/context handle. Cross-prefix context is rejected.
4. Identity APIs follow observed size units, insufficient-buffer behavior,
   termination and HRESULT/Win32/NTSTATUS domains. Verify packaged Windows
   observations before asserting them; Wine's current stub is not the oracle.
   Do not return a fabricated package identity or success on unsupported cases.
5. A package-declared class maps to its own installed DLL with traversal and
   dependency checks. System WinRT classes retain their supported lookup.
   Declared missing DLL must return the original load error. Factory module
   identity/lifetime/apartment tests remain part of acceptance.
6. The async folder operation opens a real allowed directory and returns a real
   object; nonexistent or denied paths fail accurately. Reuse Wine's existing
   async machinery, including cancellation/completion/lifetime rules. Merely
   returning an object or Completed status to satisfy the fixture is not enough.

## Per-prefix installation requirements

The prototype installer is per-user/per-prefix only. It does not implement
Store licensing, encrypted packages, administrator installation or arbitrary
AppContainer applications. Development fixtures require explicit opt-in; no
signature-verification claim is allowed unless the verification actually ran.

Before publishing a record: validate bounded archive/manifest input; reject
absolute/drive/UNC paths, dot traversal, alternate streams, aliases/duplicates,
symlinks, unsupported entry types, unsafe compression expansion and dependency
cycles. Resolve names using Windows case/path rules. Never extract over an
existing foreign tree or follow a symlink/reparse ancestor. Stage under an owned
temporary directory, publish atomically and roll back only resources created
by that transaction. Keep the source archive/binary hashes in the record.

Catalog versions, exact full/family names, architecture, publisher, executable,
application ID, in-process classes and dependency graph are immutable inputs to
activation. Missing, wrong-architecture or wrong-version dependencies fail
before a process is launched. No partial record becomes visible on failure.
Uninstall removes only the exact installed record and files it owns; running
contexts have defined retained lifetime rather than dangling catalog pointers.

## Work packages and review gates

| Package | Scope | Required evidence before acceptance |
|---|---|---|
| R0 | Token identity exports and unpackaged ABI | Native size/error observations; x64/x86 import/load and identity comparisons on the owned WoW64 baseline. No packaged success claim. |
| R1 | Bounded package inspection/staging/catalog | Synthetic archives and self-owned fixtures; traversal/alias/bomb/dependency/rollback negatives; no unreviewed third-party parser import. |
| R2 | Server context and AUMID activation | Immutable context, process/token/current/external-handle agreement, cleanup-on-failure, gate controls, cross-prefix and direct-EXE negatives; no elevation regressions. |
| R3 | Package-local WinRT resolver | Two packages with the same class, missing class/DLL, exact module identity, unload/reference and ordinary unpackaged WinRT regressions. |
| R4 | StorageFolder async slice | Windows/native ABI observations plus real directory/error/callback/cancel/lifetime tests on both Wine architectures. |
| U1.0 | Combined unchanged package | Fresh-prefix install/activation/uninstall, all positive/negative cases and hash/pin evidence; Codex review. |

Implementer orders must name exact files and contracts before edits. Protocol,
Unixlib and WoW64 tables must be regenerated and their call sites reviewed when
changed; a table-count check is not a runtime test. Every stock Wine change is
listed in the parent `docs/STOCK_CHANGES.md` before integration. No runtime
package is accepted from static checks alone.

## Evidence still needed

The existing x64 Windows oracle does not contain all buffer/error/threading
observations needed by R0-R4, and Windows x86 packaged activation remains open.
Retain proprietary oracle artifacts only under `../WineExplorer-local`; our
own fixture outputs, hashes and numeric ABI observations may be recorded here.
Keep full OS build/UBR, compiler/SDK, source pins, gate, prefix, commands,
stdout/stderr and cleanup outcome with every new run. No approval for copying
proprietary application packages or dependencies is implied.
