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
| `ntdll.NtCreateWaitCompletionPacket` | wait completion packets (a wait on an object delivered as a completion on an I/O completion port); the UI and messaging libraries use them | **done**: Wine merge request [6911](https://gitlab.winehq.org/wine/wine/-/merge_requests/6911) (Zhiyi Zhang, CodeWeavers; not merged upstream) ported by hand onto this tree (branch `claude/wait-completion`, d7fa3755): the three calls, the WaitCompletionPacket object and server objects, wow64 thunks, the regenerated protocol, request and syscall-table headers, and the ntdll tests (the `file` tests and the object type test pass on x64 and x86) |

A scan of every PE file of the runtime package and of Notepad found **no other missing static import** than the two
libraries above (and `winspool.drv`, which Wine has). Delay-loaded imports and classes activated at run time are not covered.

## Where Notepad stands after that (2026-11-22, candidate u12 = this tree with the port)

The application starts and keeps running (the launcher is still waiting after 50 s): it activates its own XAML application class
(`NotepadXamlUI.App`), the resource manager and context, the globalization classes, asks `Windows.Internal.StateRepository.Package`
(not found, not fatal), reads `ResourcesConfig` through `profapi` 114 and creates a window handle. **Not verified**: whether a
window is shown or the process is idle or stuck (the Xvfb screenshot is black for these windows, and a thread listing was taken
on a leftover process); the next step is to look at the process the way the Store's was looked at (a stack probe, `+relay` of
the framework libraries). A new full build of the tree is `newcand-build.sh` (about 90 minutes on the guest); whole files of the
worktree can then be copied in (`apply.sh`, with `CAND=u12 mk.sh`; touch the files, an extracted file older than its object is not rebuilt).

A trap met on the way: after the generated files of a change are regenerated on the guest (`perl tools/make_requests`,
`perl tools/make_specfiles`), they have to be copied back into the worktree and committed with the change.

### What Notepad does (2026-11-22, probes `wasdk-win.sh`, `wasdk-peek.sh`, `enumwin.c`)

* `Notepad.exe` starts a **second copy of itself** (a `CreateProcess` of its own image from its main thread, which then waits on the
  child in three-second slices). The child is the one that loads the Windows App Runtime and walks its activation table.
* With a relay trace the child is alive and busy for the whole 40 s (it was slowed by the trace); **without a trace only the parent
  is left after 40 s** (three threads, waiting) and the desktop has **no top-level window** (`enumwin`), so the child ends early and
  silently, after `Windows.Internal.StateRepository.Package` was asked for and not found. That undocumented class is now the first
  suspect, not a harmless log line.
* Next: serve that class with an object that logs every interface asked for and every method called (the way `profapi` 114 gave up
  its signature), to learn what the runtime wants from the package state repository.
