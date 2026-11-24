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
(it is not in the block map). Missing system libraries show up as load failures in the Wine log (`+loaddll`).

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
* Next gap: C++/WinRT asks `CoreApplication` for `IStaticLifetime` (IID `17b0e613-...`, not in the public metadata); the throw was
  `winrt::hresult_no_interface`, seen in the Wine log (the C++ exception that was thrown). Done in twinapi.appcore (Wine 372efb0c).

State after that: Notepad creates its windows and runs XAML code. The access violations in the log are `IsBadReadPtr` probes (handled);
the real stop is a call through a null function pointer in the main thread, right after the activation of
`Microsoft.UI.Dispatching.DispatcherQueue` and the `IStaticLifetime` query. Not yet traced: winedbg cannot attach to a packaged
process, and a gdb attach to the first process missed the right one. The answer of the StateRepository stand-in does not matter
(modes 0, 1 and 5 behave the same).

## The null call (2026-11-23)

The "page fault on execute access to 0" was traced without a debugger: a temporary dump in `dispatch_exception` printed the stack
words that point into modules (`RtlPcToFileHeader`); the module of the top one was `Microsoft.Internal.FrameworkUdk.dll`. The Wine log
shows `RoGetActivationFactory("Windows.ApplicationModel.Core.CoreApplication", 17b0e613-...)` just before, so the object of that
IID was called through a vtable slot that was null: C++/WinRT declares slots 6 and 7 of it only, and the runtime calls a later one. Slots
8 to 12 of our object now answer with success and log (what the call is asked, with which arguments, is in the log: the runtime's own
log text names the source file of the caller, `LiftedIXP\framework\UnhandledErrorDetected.cpp`, and a line number). What the method
really does is not known. Basis: Wine run log and a logging object; no inspection of the code.

The log of a run with `+relay` is the other lesson: the null call was reached through C++ `terminate`/unwinding noise at first
(a `noexcept` function propagating `hresult_no_interface`), and the real cause only showed once that was fixed.

State after that: no crash of the application any more; Notepad runs its window procedures and creates windows, but still throws
in them: access violations in `user_callback_handler` (8 per run) and one divide by zero (`c0000094`), and no window of Notepad is
shown. Next step: find the first of those exceptions (the same stack-dump trick, now for these exceptions).

## ALPC impersonation, as measured on Windows 11 (2026-11-23)

`tests/uwp/alpc/alpc-imp.c` (a server thread or a restricted-token server child, a client process or thread) gave:

* Impersonation needs `ALPC_PORTFLG_ALLOWIMPERSONATION` in the attributes of the **connection port** (the server's
  `NtAlpcCreatePort`); the flags of the client's attributes do not matter. Without it: `STATUS_ACCESS_DENIED`. The message is looked up
  first: an id that was never received gives `STATUS_INVALID_MESSAGE` whatever the flag says. The client id of the message header is not
  used (a changed one gives the same result).
* Impersonation only has an effect when the **message was sent with a security attribute** (`ALPC_MESSAGE_SECURITY_ATTRIBUTE`, with a
  context made by `NtAlpcCreateSecurityContext` and a quality of service). Without it the call succeeds and the thread gets no token, for
  a connection request as well (the connect has no such attribute in `CoreMessagingXP`). The level of the token is the one in the attribute:
  1 gives an identification token, 2 an impersonation token, 3 stays 3 (not copied by Wine, it is capped to 2), 0 gives an anonymous
  token (`OpenThreadToken` fails with `ERROR_CANT_OPEN_ANONYMOUS_TOKEN`; Wine gives a level 0 copy).
* A server without privileges that is less trusted than the client (low integrity server, medium client, the same user) gets an
  identification-level token; with the same integrity it gets the level of the attribute. This is the rule of the named pipes
  (`token_impersonation_level`: `SeImpersonatePrivilege`, or the same user and not less trusted and not less elevated); the server
  uses the same helper for ALPC.
* A port whose security descriptor does not allow the connect right (bit 1) refuses `NtAlpcConnectPort` with `STATUS_ACCESS_DENIED`; the
  creator gets its handle whatever the descriptor says.

What the server does (Wine): the token of the sending thread is taken **when the message is sent** (only with a security attribute) and kept
with the pending message; impersonation duplicates exactly that token, at the lowest of the attribute's level and the level the helper
allows. Thread and process ids play no part. `ntdll/tests/alpc.c` has the tests that can be observed inside one process (a client that
changes its token after sending; levels 1 and 2; no attribute); the downgrade for a less trusted server needs two processes and is only in
the probe (`SRVLOW=1 SRVFLAGS=10000 alpc-imp 10000 2 secattr srvrestricted`).

`\RPC Control` is a plain root directory without a security descriptor, like `NLS`: anyone can create ports in it, the first creator of a
name wins (a second create of the name opens the same port, there is no replace). Names that our own services use have to be created when
the service starts (services.exe starts first); there is no reserved-names list. A client that must be sure it talks to the service checks
the process or the token of the server, not the name.

## The window-procedure exceptions were the crash dialog, and the next gap is CoreUICreate (2026-11-23)

The access violations and the divide by zero that were counted per run (`user_callback_handler ignoring exception ...`) are in the
threads of the winedbg crash dialog ("A program on your system has crashed"), not in Notepad. `IsBadReadPtr` probes (kernel32) are the
other access violations; they are handled. So the real state of Notepad after the IStaticLifetime fix is a single stop:

* `Microsoft.Internal.FrameworkUdk.dll` (`LiftedIXP\frameworkudk\CoreWindowIntegration.cpp`) loads the **system** `CoreMessaging.dll` and
  calls `CoreUICreate` in it; Wine's `coremessaging.dll` has it as a stub (`wine: Unimplemented function coremessaging.dll.CoreUICreate`),
  and the caller treats a failure as fatal (fail-fast `c0000602` from `Microsoft.ui.xaml.dll`).
* The runtime's own `CoreMessagingXP.dll` is not a stand-in for it: it exports the other API of that generation (`CoreMsgCreateSession`,
  `CoreMsgGetExistingSession`, `WinUICreateDispatcherQueueController`, the `CoreUICall*` and `Msg*` functions, `CoreUICreateAnonymousStream`),
  and no `CoreUICreate`. `Microsoft.UI.Input`, `Microsoft.UI.Windowing.Core`, `InputStateManager`, `dcompi` and `dwmcorei` use the XP one
  (`CoreMsgCreateSession`); only `CoreWindowIntegration` asks the operating system. Forwarding the stubs to the XP library was tried and
  dropped (it only turns the abort into a fail-fast).
* `CoreWindowIntegration` is reached from `Microsoft.UI.Xaml.Hosting.WindowsXamlManager` (the activation just before the stop). What the
  operating system's `CoreUICreate` does (the arguments and the result are not documented) is the CoreUI conversation with the window
  system; it belongs to the `Windows.UI.Composition` / DWM side of the hosting route (docs/uwp/STORE_APP.md), so this is the same gap
  as for the Store's WebView2 visual hosting.

Next: probe the real `CoreUICreate` on the Windows host (black box: arguments, result, which interface comes back), and decide between
a small implementation and avoiding the integration.

## CoreUICreate probed on Windows 11 and the first layers implemented (2026-11-23)

`tests/uwp/alpc/coreui-probe.c` (Windows host, black box): `CoreUICreate(void **object)` returns S_OK with a **process-wide object**
(the same pointer on every call), E_POINTER for null; the object's IUnknown is another pointer than the object itself; it implements a
**session interface `{4b2edab9-129b-4733-be15-356853d55ace}`** (at least 24 methods; the vtable is the object's own) and slot 8 of it,
called with a null argument, gives an **item object** with IUnknown and `{fcd9d50d-0902-4a2d-ac94-b95c3316f6a0}` (the item itself, a
vtable of 4 methods after IUnknown).

What FrameworkUdk does with it, as seen from our implementation's log: it asks an object for the session interface, calls one method of
it (slot 3: creates something into an out pointer; slot 8: `(argument, out)`) and asks the result for IUnknown. Slot 12 is called with a
function of FrameworkUdk, a context, a pointer to the object and a pointer (a handler registration). `dlls/coremessaging/coreui.c` implements what is asked, in that order: the object, the session interface, the item. It gets
Notepad past three layers, then it asks the item for `{37a94c05-1aa6-4cea-9049-083bba055cec}`.

This is a blind walk down a chain of undocumented interfaces of the CoreUI session (the conversation of the application with the window
system). Each step costs one probe on Windows and one run; how deep it goes is not known. Alternatives: (a) find out what makes
`WindowsXamlManager` call `CoreWindowIntegration` (a version or feature check that can be answered the other way, which would skip the
whole chain); (b) implement the session for real (needs the window system side: the `Windows.UI.Composition` / DWM piece).

## Notepad after the CoreUI session: the compositor, the input sinks, and the basis of every finding (2026-11-24)

**Method rule (owner, 2026-11-24).** The findings below stand on black-box measurement (calls and traces at exported or vtable entry points on
the Windows host, on Wine with our own logging objects) or on MIT-licensed source. No disassembly or `objdump` of any shipped binary is used, the
package binaries' licence terms forbid it. Earlier statements of this file that rested on disassembly were removed or rewritten (the null
call, the CoreUICreate wrappers, the Store wait), and the statements of this branch's earlier revision that did are withdrawn below.

**MIT source check.** `microsoft/WindowsAppSDK` at tag `v2.7.0` (commit 85b42a60) and `microsoft/microsoft-ui-xaml` at tag
`winui3/release/2.7.0` (commit 1b5b1a0c), the version of the runtime under test: neither contains FrameworkUdk, `dcompi`, `dwmcorei`,
`Microsoft.UI.Input`, `Microsoft.InputStateManager`, the `DCompPrivates_*` functions, the composition input sink syscalls or any of the private
interfaces named here (searched for those names and for the IIDs; nothing found). What the repositories do say, and is cited below:
`microsoft-ui-xaml/src/dxaml/xcp/core/dll/xcpcore.cpp` lines 476-492 ("dcompi.dll has static DLL dependencies on a cluster of other IXP DLLs",
activation factories of the lifted compositor are taken from `dcompi.dll`) and `src/dxaml/xcp/core/inc/corep.h` line 646
(`m_dcompiModule`, "dcompi.dll (Microsoft.UI.Composition.*)").

### Where Notepad stands

Notepad (Microsoft.WindowsNotepad 11.2609 on runtime 2.7.0) now starts, creates its window, its XAML objects, the D3D11 and D2D devices of the
lifted compositor and its text-input machinery, draws the empty frame, and goes on. No text or controls are drawn, `DwmSceneI.dll` is never loaded,
and an intermittent crash (a call through a heap address on the thread of the D3D11 device) is the next thing to track.
The default `UniversalApiContract` of 10 is enough: runs with 14, 17 and 19 behave the same (Wine log), so `wintypes` is unchanged.

### Findings and their basis

| Finding | Basis |
|---|---|
| The lifted compositor is hosted by `dcompi.dll` (Microsoft.UI.Composition.*); `dwmcorei.dll` is loaded right after it (`+loaddll`) | MIT source (`xcpcore.cpp`, `corep.h`, above) for `dcompi`; the load order is in the Wine log |
| The device answers `{d14b6158-...}` and `{28d6ad3d-...}` with the very interface of `IDCompositionDesktopDevice` | black box: `tests/uwp/alpc/dcomp-privqi-probe.c` (Windows 11); the asking is in the Wine `+dcomp` log |
| `DCompositionCreateSharedVisualHandle` (export 1040) is `HRESULT (HANDLE *)`; a NULL out is E_INVALIDARG; the handle has the type "Composition" | black box: `dcomp-exports-probe.c` (a GetProcAddress sweep finds ordinal 1040, and the name resolves to the same address), `dcomp-visualhandle-probe.c`; that the runtime asks for the ordinal is the Wine loader's message for the missing import |
| Slot 29 of the desktop-device interface opens a shared handle as a visual or a target | black box: `wintrace.c` at the target `dcomp.dll!desktop[29]` (slot 29 of a live device's vtable) while Notepad starts on Windows 11; pops 16 bytes on 32-bit Windows (`dcomp-dslot32.cpp`) |
| The private interface `{2c4eef28-...}` of a visual is the visual itself (same pointer as IUnknown and IDCompositionVisual2, shared reference count, on version-1 and version-2 devices); slots 40 and 41 take one pointer and return S_OK with no visible effect | black box: `dcomp-visualpriv-probe.cpp`, `dcomp-vslot32.cpp` (8 bytes popped on 32-bit Windows); the call pair is seen with `wintrace.c` at `dcomp.dll!visual[40]` and `dcomp.dll!visual[41]` |
| Desktop-device method 46 takes one pointer and stores the newest completed batch id (0 on a fresh device); a trace of Notepad shows it rise 1, 1, 2, 3, 3, 4, 4 | black box: `dcomp-dslot32.cpp` (8 bytes popped), `wintrace.c` at `dcomp.dll!desktop[46]` |
| The device answers `{6b556968-...}` on Windows 11 too, but its slot 8 there is a different method (12 bytes, E_INVALIDARG for an event handle) | black box: `dcomp-dslot32b.cpp`. On Wine the runtime calls slot 8 with the commit event (event handle, 0, 0, a pointer): seen with a logging object. Implemented for that caller, not measured on Windows 11 |
| Without the `{6b556968}` interface the composition engine logs a failure and shuts down right after method 46 | Wine `+relay` log of the engine thread (calls and return values only) |
| `NtCreateCompositionInputSink(block, &handle)`, `NtQueryCompositionInputSinkLuid(handle, &luid)`, `NtCloseCompositionInputSink(handle)`: argument bytes, the 0xe8-byte block, validation, statuses | black box: `sink-probe.c`, `sink-probe2.c`, `sink-pops32.cpp`, `wintrace.c` at `win32u.dll!NtCreateCompositionInputSink` and `win32u.dll!NtQueryCompositionInputSinkLuid` (the block as Notepad passes it). The sizes 0xe8 and 0x38: `sink-probe3.c` (a buffer ending at an inaccessible page: 0xe7 bytes fail with an access violation, 0xe8 work; clearing the window of any entry, or a stride of 0x30, is STATUS_INVALID_PARAMETER) |
| The runtime's CoreUI session slot 13 is called like slot 12 under contract 19 | Wine log (our logging implementation) |
| FrameworkUdk's system-side tables: `GetFunctionTableById(id)` returns tables of fixed size and version (ids 0 to 5: 456/5, 8/0, 192/0, 32/0, 88/0, 56/1 bytes/version) | black box: `fudk-sys-probe.c` calls the export; informational, no code depends on it |
| `IUserActivityRequestManagerInterop::GetForWindow` is called on the factory of `UserActivityRequestManager` with the window and `IID_IUserActivityRequestManager` | black box: `wintrace.c` at `winrt!Windows.ApplicationModel.UserActivities.UserActivityRequestManager@{dd69f876-...}[6]`, `factory-iid-probe.c` |
| The owner hint: Windows sends no WM_NCHITTEST to the application while a window is positioned; Wine's `apply_window_pos` did and Notepad failed fast | black box: `nchittest-probe.c` on Windows 11 (a window under the corner of another window's new position gets no WM_NCHITTEST during SetWindowPos: count 0), reproduced by the Wine test `test_position_sends_no_nchittest`; the Wine log shows Notepad's fail-fast. The earlier statement that rested on a trace of a window procedure of Microsoft.UI.Windowing.Core (a typed RVA) is withdrawn |
| Spell checker factory missing (REGDB_E_CLASSNOTREG, fail-fast); `bcp47langs` functions called at startup | Wine log; `bcp47-probe.c` for the distance function |
| `RegisterAdaptersChangedEvent` behaviour | black box, measured by the Wine Explorer Integration session on Windows 11 |
| Thread states of the running Notepad (all idle at 25 s, one briefly busy) | `wasdk-threads.sh` (`/proc`, gdb registers on our own Wine process) |

### Withdrawn (they rested on disassembly, or were wrong)

* The FrameworkUdk dispatch description (helper "levels" 7 to 17 chosen by contract probing, the per-level helper classes) and the claim that the
  engine's call site passes a descriptor block: removed. The method-46 form with a block and a code was also wrong (registers of an x64 trace that are
  not arguments); the 32-bit measurement above replaces it.
* "Slots 40 and 41 are called with a float each": wrong, they take one pointer (measured).
* "Windows answers S_OK for slot 8 of `{6b556968}`": withdrawn, see the table.
* The helper tools that read offsets, bytes, strings, section tables or exception-type data of `Microsoft.UI.Input.dll` and other package binaries
  (trampoline patches, byte patchers, PE string, pointer and cross-reference readers such as `pe-xref.py`, call-graph scripts, `throwtype.py`) are
  not in this branch. The first revision of this branch (commit 2c06fb86) still carried `pe-xref.py` and `throwtype.py` although its message said
  otherwise; they are removed, and the claim in that message is corrected here.

### Tracing rule (Session 0 ruling, 2026-11-24)

`tests/uwp/alpc/wintrace.c` traces at call boundaries only, and only at entry points it finds at run time: `module!export` or `module!#ordinal`
(GetProcAddress by name or ordinal), or a slot of the vtable of a live object that the tracer creates itself (`dcomp.dll!desktop[N]`,
`dcomp.dll!visual[N]`, `winrt!Class@{IID}[N]`). A raw RVA on the command line is refused, no code bytes are logged, and the one byte a breakpoint
replaces is only kept in memory to be put back. Earlier traces typed RVAs (taken from export tables, from vtables of live objects and, for
some Microsoft.UI.Input traces, from a disassembly, which is why nothing from those is used). Every finding of this branch that cites a trace
was therefore taken again on 2026-11-24 with the rule's targets, and gave the same result (capture `uwp-captures/wt-clean.txt` in
`../WineExplorer-local`): slot 29 (ten calls, a handle as first argument), method 46 (low dword rising 1, 1, 2), slots 40 and 41 (pairs, the
private IID as the argument of 40), `GetForWindow` (the window and the IID), the sink calls (five creates with the 0xe8-byte block, seven LUID
queries). The traces of FrameworkUdk exports and of the system-side module that were taken with typed RVAs support no statement here any more.

## 2026-11-24: the crash on the Direct3D device thread was a race in Wine's DWrite

The intermittent crash (an execute-access page fault on a heap address, thread of the D3D11 device, `create_system_fontfallback` and
`fontfallback_MapCharacters` just before) was a use after free in Wine's own `dwrite`, not a missing piece of the compositor. Basis: Wine log
(`+dwrite,+seh`: the collection used at the crash was released to 0 earlier by another thread), `gdb` was not needed; a lab-only stack
capture on `Release` of font collections (Wine's own code, symbolised with the build tree's debug info) named `gdiinterop1_CreateFontFromLOGFONT` for riched20 and
`factory_get_system_collection`. The fix and its test are in the Wine branch `claude/staterepository-notepad-crash`.
After it Notepad no longer crashed in the runs made (two of two; before it, most runs). `DwmSceneI.dll` is still not loaded and no text is drawn.
