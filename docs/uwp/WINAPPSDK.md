<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Windows App SDK applications on Wine (lab only)

Windows App SDK (WinUI 3) applications are the second family of packaged applications after the UWP ones, and they
drive what the Store also needs (composition, package services, the language and resource libraries). The installed
Microsoft apps that use the Windows App Runtime (Notepad, Paint, Photos, Alarms, Snipping Tool, Clipchamp and others, on
runtime 1.2 to 2.7) are the test targets. As with the Calculator and the Store, a package is copied into the owned lab
guest only; none of its files enter this or any other repository. Only observations are written here.

## How it runs (lab)

`tests/uwp/lab-helpers/wasdk-run.sh` (packages in `claude/wasdk` on the guest): a fresh prefix, the `uwp-frameworks`
component, the **Windows App Runtime framework package installed for every user** (`wexappx install --all-users`: the
server takes the classes of a dependency from the machine catalog only) and the application, then `applaunch`.
`RELAY_FROM=Microsoft.Internal.FrameworkUdk.dll` with `"" "err+all,+relay"` traces what one module calls. A copy made from an
*installed* package folder holds a `microsoft.system.package.metadata` directory the system added; the script removes it
(it is not in the block map). `pe-imports.sh` (a scan of every PE file of the packages against the module names of Wine) finds the
missing system libraries in one pass.

## Where Notepad (Microsoft.WindowsNotepad 11.2609, runtime 2.7.0) stood, one wall after the other (2026-11-22)

| Wall | What it was | What was done |
|---|---|---|
| `wexappx`: unsupported activatable class declaration | the runtime declares 1033 classes; the manifest reader took 1024 and the catalog record 64 KiB | limits raised (wexappx 4096 items and 200,000 characters of record, the server 512 KiB per record and 1 MiB of classes per context, kernelbase 1 MiB) |
| `elscore.dll` not found | Notepad imports the Extended Linguistic Services library | new module `elscore` (header from the public-domain mingw-w64 one): `MappingGetServices` answers an empty list |
| `Windows.Foundation.GuidHelper` not found | the runtime's startup | implemented in `wintypes` (`CreateNewGuid`, `Empty`, `Equals`; test) |
| the framework's classes not found | the runtime had been installed into the user catalog | installed for every user (see above) |
| `bcp47mrm.dll` not found | the runtime's resource library imports `IsWellFormedTag` | new module `bcp47mrm`: `IsWellFormedTag` (RFC 5646 syntax as Windows 11 applies it, 113 checks taken from Windows) |
| `PackageFamilyNameFromFullName` unimplemented | called at startup (it was a commented stub in kernelbase) | implemented from the existing family-name code |
| `profapi.dll` ordinal 114 | `Microsoft.Internal.FrameworkUdk.dll` calls it right after `PackageFamilyNameFromFullName`; Wine's `profapi` had 17 nameless stubs | the signature was **found by logging what the caller passes** (no probe of Windows could call it: every guess crashed, the export wants something else): `(family name, 0, "ResourcesConfig", KEY_READ, &key)`, a Win32 error code returned; it opens a key of a package's registry settings and the caller then reads `UseSystemMetadataPath`. Wine's export logs the call and answers `ERROR_FILE_NOT_FOUND` (no package settings: the caller takes its default). It is an inference from one call, not a documentation |
| `Windows.Internal.StateRepository.Package` not found | the runtime's deployment layer asks the system's package state repository | **open** (logged, not fatal so far); undocumented internal API |
| **`ntdll.NtCreateWaitCompletionPacket`** | the next call after that: wait completion packets (a wait on an object that is delivered as a completion on an I/O completion port; the UI and messaging libraries use them) | **open**. Prior art: Wine merge request [6911](https://gitlab.winehq.org/wine/wine/-/merge_requests/6911) (Zhiyi Zhang, CodeWeavers, seven patches: `NtCreateWaitCompletionPacket`, `NtAssociateWaitCompletionPacket`, `NtCancelWaitCompletionPacket`, the server objects and tests). It does not apply to this tree as it is (`server/completion.c`, `ntdll/unix/sync.c` and the tests moved since); the next step is to merge it by hand |

A scan of every PE file of the runtime package and of Notepad found **no other missing static import** than the two
libraries above (and `winspool.drv`, which Wine has). Delay-loaded imports and classes activated at run time are not covered.
