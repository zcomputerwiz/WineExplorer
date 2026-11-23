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

### The StateRepository probe (2026-11-22, Wine branch `claude/staterepository`, on `claude/wait-completion`)

`Windows.Internal.StateRepository.Package` is served by a logging stand-in (`staterepository.c`): the factory answers the interface
the runtime asks for (`4408fb32-c420-41c5-b6c1-edce489bb84d`) with an object of 64 methods that log and fail. First result: the
runtime calls **method 21 (vtable slot 27)** of it once, with three pointers to the stack frame of the caller:

* arg 1 points to a small structure whose third word is a pointer to the package full name
  (`Microsoft.WindowsNotepad_11.2609.11.0_x64__8wekyb3d8bbwe`) and whose first word holds the length of the name (0x38 = 56) in its
  upper half: the input (a package identified by its full name);
* args 2 and 3 point to neighbouring stack locations that are not initialised yet (outputs); a frame beside them holds the package
  path string from an earlier call, which is stale data of the caller and not an argument.

So the call looks like "give me the state-repository package for this full name", with results returned through the two output
pointers. What a result has to hold (an object of this class, an interface of it, a path, the identity) is the next thing to find:
return success with a zeroed output, then with a made-up object, and see what the caller dereferences. After the failed call the
application neither crashed nor reported anything, and the second copy of Notepad still ends silently, so it is not proven that this
call is the reason.


## Experiments with method 21, and the server crash behind the silent end (2026-11-22)

Method 21 was answered six ways (`WEX_SR_MODE` 0 to 5: not implemented, success with zeroed outputs, a made-up object in one or both
outputs, plain success). Every mode behaved the same: only method 21 is ever called, no other method, no new error line, no window.
So the StateRepository call is not what decides the start of Notepad; the experiment code was dropped again.

The silent end had another cause. Every run ended with "wineserver crashed", and a gdb attach to the server
(`GDB_WS=1 tests/uwp/lab-helpers/wasdk-win.sh`) showed a real crash in the wait completion code (the port of Wine MR 6911):

* the packet object was never initialised after allocation, so the handle close of a packet that was not associated yet removed a
  garbage list entry (`cancel_completion_packet`);
* the packet held only the target, not the sync object whose queue it is on, and the wake loop was not safe against removal;
* a target without a `signaled` operation crashed `is_obj_signaled` (now `STATUS_INVALID_PARAMETER_3`).

Fixed in Wine 0e5c3352. With the server alive Notepad now gets further: it activates `Microsoft.UI.Dispatching.DispatcherQueueController`,
which connects to `\BaseNamedObjects\CoreMessagingRegistrar-<pid>:...` through `NtAlpcConnectPort` (a stub in Wine) and sends a message
(`NtAlpcSendWaitReceivePort`, also a stub); the next thing seen is a fail-fast (`e0464645`, then `c0000602`). So WinUI 3 needs the
CoreMessaging service for its dispatcher queue: either an ALPC implementation good enough for that protocol, or a stand-in for the
dispatcher queue below it. That is the next gap for Notepad and for every WinUI 3 application.

## CoreMessaging, ALPC and the dispatcher queue (2026-11-23)

The fail-fast after `Microsoft.UI.Dispatching.DispatcherQueueController` was the next gap in the list. What was found, in order:

* The runtime ships its own CoreMessaging (`CoreMessagingXP.dll`, a complete copy including the server side). It hosts the registrar in the
  process: one thread creates the ALPC port `\BaseNamedObjects\CoreMessagingRegistrar-<id>`, another connects to it (synchronous
  connection with data), the server impersonates the client, accepts, receives a synchronous request and answers it. Wine had only
  `NtAlpcCreatePort`; every other `NtAlpc*` function was a stub.
* ALPC is implemented now (Wine abf3d5e1). No prior art: the Wine merge request "LPC: Implement proper LPC system" (MR 10611) is classic
  LPC and does not touch `NtAlpc*`. The behaviour was measured on the Windows host with `tests/uwp/alpc/alpc-probe.c` and then fixed in
  `dlls/ntdll/tests/alpc.c`, which gives the same result on Windows 11 and Wine (x64 and x86). What Windows does, found that way:
  a connection request reaches the server as type `0x200a` and the answer as type `0xb`; requests and datagrams both arrive as
  `0x2001`; a reply is a message sent with flag `0x10000` and the message id of the request (the flag is "release", not "reply"); the
  same flag with id 0 on a connection port is a datagram to the port itself (CoreMessaging uses that); a connection with a too small
  buffer for the answer fails with `STATUS_BUFFER_TOO_SMALL` and is closed again (the server then sees a port closed message, type 5,
  8 bytes); the handle is left alone when a connect fails; the connection ports of Windows live in `\RPC Control`.
* Next gap: C++/WinRT asks `CoreApplication` for `IStaticLifetime` (IID `17b0e613-...`, not in the public metadata, its collection is an
  `IMap<HSTRING, IInspectable>`); the throw was `winrt::hresult_no_interface`, read from the throw info of the exception in
  `Microsoft.Internal.FrameworkUdk.dll` (`tests/uwp/lab-helpers/throwtype.py`). Done in twinapi.appcore (Wine 372efb0c).

State after that: Notepad creates its windows and runs XAML code. The access violations in the log are `IsBadReadPtr` probes (handled);
the real stop is a call through a null function pointer in the main thread, right after the activation of
`Microsoft.UI.Dispatching.DispatcherQueue` and the `IStaticLifetime` query. Not yet traced: winedbg cannot attach to a packaged
process, and a gdb attach to the first process missed the right one. The answer of the StateRepository stand-in does not matter
(modes 0, 1 and 5 behave the same).
