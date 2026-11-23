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

## The null call (2026-11-23)

The "page fault on execute access to 0" was traced without a debugger: a temporary dump in `dispatch_exception` printed the stack
words that point into modules (`RtlPcToFileHeader`), the top one was `Microsoft.Internal.FrameworkUdk.dll` + 0x188df. The code there
(`objdump`) is `RoGetActivationFactory("Windows.ApplicationModel.Core.CoreApplication", 17b0e613-..., &obj)` followed by a call of
**vtable slot 11 of that object** with six arguments. So the interface with the IID C++/WinRT calls `IStaticLifetime` has more
methods than C++/WinRT declares (it uses slots 6 and 7 only). The slots of our object beyond 7 were null. Logging the arguments
showed the source file of the caller, `LiftedIXP\framework\UnhandledErrorDetected.cpp`, and a line number. Slots 8 to 12 now answer
with success (and log); what the method really does is not known.

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

Next: find out what `CoreWindowIntegration` does with the result (the function is a delay-load wrapper at FrameworkUdk RVA 0x73d0 that
tail-calls the OS function with its own arguments), probe the real `CoreUICreate` on the Windows host (black-box: arguments, result,
which interface comes back), and decide between a small implementation and avoiding the integration.

## CoreUICreate probed on Windows 11 and the first layers implemented (2026-11-23)

`tests/uwp/alpc/coreui-probe.c` (Windows host, black box): `CoreUICreate(void **object)` returns S_OK with a **process-wide object**
(the same pointer on every call), E_POINTER for null; the object's IUnknown is another pointer than the object itself; it implements a
**session interface `{4b2edab9-129b-4733-be15-356853d55ace}`** (at least 24 methods; the vtable is the object's own) and slot 8 of it,
called with a null argument, gives an **item object** with IUnknown and `{fcd9d50d-0902-4a2d-ac94-b95c3316f6a0}` (the item itself, a
vtable of 4 methods after IUnknown).

The FrameworkUdk side (`Microsoft.Internal.FrameworkUdk.dll`, wrappers around 0xf350..0xf4a0 and 0x13a1c..): ask an object for the
session interface, call one method of it (slot 3: creates something into an out pointer; slot 8: `(argument, out)`), ask the result for
IUnknown. Slot 12 is called with a function of FrameworkUdk, a context, a pointer to the object and a pointer (a handler
registration). `dlls/coremessaging/coreui.c` implements what is asked, in that order: the object, the session interface, the item. It gets
Notepad past three layers, then it asks the item for `{37a94c05-1aa6-4cea-9049-083bba055cec}`.

This is a blind walk down a chain of undocumented interfaces of the CoreUI session (the conversation of the application with the window
system). Each step costs one probe on Windows and one run; how deep it goes is not known. Alternatives: (a) find out what makes
`WindowsXamlManager` call `CoreWindowIntegration` (a version or feature check that can be answered the other way, which would skip the
whole chain); (b) implement the session for real (needs the window system side: the `Windows.UI.Composition` / DWM piece).

## The trigger was a set of registrations; Notepad now reaches XAML object creation (2026-11-23)

What makes `Microsoft.Internal.FrameworkUdk.dll` ask the operating system's CoreMessaging for a CoreUI session is not one feature check: its
exports `FinalUnhandledErrorDetected_RegisterHandler`, `WindowPositionChanged_RegisterListener`, `BackButtonIntegration_*`,
`FrameworkInputView_*`, `InputPaneFramework_*` and similar register listeners with the CoreUI session, and Xaml calls them at start-up.
So the chain is bounded (one registration object per kind) and there is no switch to turn it off. Instead of one probe per interface, an
interface of the item that is not known is answered with a generic object whose 32 methods succeed and log (`dlls/coremessaging/coreui.c`,
the arguments are in the log). With that the registrations pass and Notepad goes on, no fail-fast from the session any more.

Further gaps found on the way, in the order Notepad hit them (all done, Wine 599b86ef):

* `Windows.Storage.AppDataPaths` (new in `windows.storage.applicationdata`): the prefix has to be updated for a new class (`wineboot -u`, or a
  fresh prefix; the registration is made from the module). On Windows the properties need a package identity (`APPMODEL_ERROR_NO_PACKAGE`
  otherwise, even for a null argument): the test checks it on both.
* `bcp47mrm.GetDistanceOfClosestLanguageInList` (a stub of my own module): a similarity score, measured with
  `tests/uwp/alpc/bcp47-probe.c` (1 same tag, 0.98 region missing, 0.975/0.976 other region, 0.96 for "und", 0.1 less per list position,
  `ERROR_NO_MATCH` for no match, `E_POINTER` for null).

State: Notepad creates its XAML objects (Uri, AcrylicBrush ...), DirectX adapter enumeration and window handles run, and then a C++/WinRT
`hresult_no_interface` (again) propagates out of a `noexcept` function after a run of failed `QueryInterface` probes on a boxed value and
on a `Uri` ({0c881986-86e1-40b3-9642-9e5090a8a6b0}, {4bd682dd-...} = IPropertyValue). Next: find which object is unboxed as which type.

## Notepad reaches the compositor (2026-11-23)

What the std::runtime_error of Notepad was (found with the stack dump and the string xref): `Notepad.exe` throws
`std::runtime_error("E22E2A8B-...")` from a start-up function when `LoadCursorW(NULL, IDC_WAIT)` returns null. In the lab prefix **every**
system cursor failed (error 6): not a product bug but a stale build of the lab candidate. After the server protocol changed (the ALPC requests
sit in the middle of it), only the server and ntdll had been rebuilt; `win32u` still sent the old request numbers (the trace showed
`set_fd_eof_info` where a cursor request was meant). A full `make` of the candidate (about 45 minutes) fixed it:
**after a protocol change rebuild everything** (`tests/uwp/lab-helpers/full-build.sh`).

Then, in the order Notepad hit them: `api-ms-win-appmodel-unlock-l1-1-0` (`IsDeveloperModeEnabled`, imported by `dcompi.dll`; measured on Windows 11,
see STOCK_CHANGES), and now:

* `Windows.UI.Composition.Compositor` is not available (`Failed to find library`), then the C++/WinRT exception of the activation.
  `Microsoft.UI.Xaml.Hosting.DesktopWindowXamlSource` and the XAML controls (TabView ...) are created before it, `dcompi.dll` (the
  runtime's own DirectComposition) now loads. This is the hosting-route gap: WinUI 3 in a desktop window (and the Store's WebView2 visual
  hosting) needs the **operating system compositor**, `Windows.UI.Composition` (Compositor, visuals, brushes, animations, the
  `ICompositorInterop` and `ICompositionDrawingSurfaceInterop` that tie it to DirectComposition and D3D). Wine has `dcomp` and the staging
  dcomp port in the fork, no WinRT compositor.

Everything the application asks for before that point is now answered (CoreUI session, AppDataPaths, bcp47mrm, the developer-mode import).
The compositor is the next piece and a big one; the Microsoft.UI.Composition of the runtime (`dcompi.dll`) may carry most of the logic
(it is what WinUI 3 uses), so a first step is to see how much of `Windows.UI.Composition` it really needs from the system.

### What dcompi.dll needs from the system (scoped 2026-11-23)

Found with a `+relay` run whose `RelayInclude` is `combase.RoGetActivationFactory` (`wasdk-run.sh`, `RELAY_INCLUDE=...`; the `ret=` address
names the caller, `dcalls.py` lists the calls a module makes before a message), the PE's imports, and disassembly:

* The runtime package registers `Microsoft.UI.Composition.*` (the lifted compositor) with **`dcompi.dll`** as the server (manifest, about 60
  classes). `dcompi.dll` imports only `DCompositionCreateDevice3` (plus one by ordinal) from the system `dcomp.dll`, `d2d1`, `d3d11`, `dxgi`,
  and the runtime's own `wuceffectsi.dll`, `marshal.dll`, `Microsoft.UI.Composition.OSSupport.dll` (52 KB, only a thin OS-support layer).
  So the engine is the runtime's, not the system's. These all load in Wine already.
* The failing call is **inside dcompi's `DllGetActivationFactory`** (its C++/WinRT `get_activation_factory`, which falls back to loading a
  library named after the class when `RoGetActivationFactory` fails; that is the `Failed to find library` message). The factory of any
  class except three (`Compositor`, `CompositionCapabilities`, `CompositionEngine`; the first one asked for here was
  `CompositionEasingFunction`) first runs a lazy once-only initialisation (`InitOnce`, the first thing after a `HKLM\Software\Microsoft\Windows\Dwm`
  key probe): it activates the factory of a class of the system (the second failure of the same run,
  `WindowsUdk.Foundation.Metadata.ApiInformation`, belongs to the same chain), queries it for interface
  {5e03ab98-c7e0-4449-8518-568d62e9b396} and calls its first method with a BOOLEAN out parameter (a "supported?" probe), then activates
  `Windows.UI.Composition.Compositor` of the system (`IActivationFactory` slot 6, `ActivateInstance`) and queries the instance for two
  interfaces. A failure of that chain is the exception that ends Notepad.
* So the minimum that moves Notepad on is **not** a whole compositor: it is (1) `WindowsUdk.Foundation.Metadata.ApiInformation`
  answering the probe, and (2) a `Windows.UI.Composition.Compositor` whose instance answers the two queried interfaces. What dcompi calls
  on that instance afterwards is the open question; the experiment is a stub class that logs every slot called, then fills in what is
  needed (a real compositor is only needed if dcompi forwards drawing to it, which the imports of `dcomp`/`d2d1` suggest it does not).

Helper scripts (`tests/uwp/lab-helpers`): `wxref.py` (wide-string and RVA cross references with the containing function), `callers.py` (call
graph upwards), `dcalls.py` (calls from one module before a log line).

### Notepad runs on into the compositor's hosting (2026-11-23)

With logging stubs for what the start-up asked the system for, Notepad now goes much further. The chain, in the order it was hit (each found
with `+relay`, `+module`, a stack dump patch of `dispatch_exception`, `mapav.py`, `lastcalls.py`, `wasdk-report.sh`):

1. `Windows.UI.Composition.Compositor` and `WindowsUdk.Foundation.Metadata.ApiInformation`: logging stubs in `windows.ui`. The ApiInformation
   statics are asked one thing at start, slot 6 with (a pointer, 1, a pointer, 0xCA) (`IsApiContractPresent`-like); the Compositor instance gets
   slot 6 with an out pointer. Both succeed with the out values untouched, and the runtime goes on.
2. `dwmcorei.dll` (loaded by `Microsoft.UI.Input.dll`) did not load: its imports `NtQueryWnfStateData`, `RtlSubscribeWnfStateChangeNotification`
   and `RtlUnsubscribeWnfStateChangeNotification` were missing from ntdll (no WNF at all in Wine): stubs. Then its DllMain failed on
   `D2D1CreateFactory` followed by a QueryInterface of the factory for {6f72c0a2-6db7-46e9-9b62-b58a23f4928b}: a private interface that Windows 11
   answers with a separate object (`d2dqi-probe.c`) and Wine refused: an object with logging methods now (its slot 6 is called with 0).
3. A NULL read in `CoreMessagingXP.dll` at the call of `NtAlpcImpersonateClientOfPort` (our ALPC server side): the second connection request of
   the self-hosted registrar was received with a stale context attribute whose port context was a port handle. Cause: a datagram a server
   sends to its own connection port was delivered with the context attribute marked valid and the port handle as port context. Windows 11
   delivers it with ValidAttributes 0 and a null context (`alpc-selfdg.c`). Fixed in the server, the unix side and the 32-bit conversion (wow64
   copied the structure only when valid); test added (run on Wine only, both architectures).
4. `dcompi.dll` imports `dcomp.dll` by ordinal 1040, which is **`DCompositionCreateSharedVisualHandle`** (a named export of the real dcomp.dll,
   found with `dcomp-exports-probe.c`; Wine's dcomp had a stub by ordinal). It is the next wall: the lifted compositor creates a handle for a
   visual that is shared with the system compositor, called with (a stack pointer, a heap pointer, 0x2DB1C01, 0). Windows 11 answers E_INVALIDARG
   to the shape of `DCompositionCreateSurfaceHandle` (`dcomp-visualhandle-probe.c`), so the signature is something else. With E_NOTIMPL the
   XAML start-up fails fast (`RoFailFastWithErrorContextInternal2 0x80004001`). An earlier run also ended in a fail-fast of a CoreMessagingXP
   thread (c0000602) that did not come back in the next runs: a race, not followed.
   **Solved (2026-11-23):** the signature is `HRESULT DCompositionCreateSharedVisualHandle(HANDLE *out)`. The four logged values were stale
   registers: the only caller in `dcompi.dll` (RVA 0xe0170, found with `impcalls.py` and `callers.py`) passes just `&local`, initialised to
   NULL, and keeps the handle to set it as a property (0xb4) of a visual. Windows 11 (`dcomp-visualhandle-probe.c`, "shared(&h)"): NULL out is
   E_INVALIDARG, otherwise a handle to an object of type `Composition`. Wine returns an anonymous event.
5. The device of `DCompositionCreateDevice3` is then asked for {d14b6158-c3fa-4bce-9c1f-b61d8665eab0} (and the one of `CreateDevice2` for
   {28d6ad3d-ee2a-4bcd-9419-7d54800435b1}). Windows 11 answers both with the pointer and vtable of `IDCompositionDesktopDevice`
   (`dcomp-privqi-probe.c`); Wine does the same (aliases in `dcomp_device_QueryInterface`).
6. **The wall now** is a NULL read in `Microsoft.Internal.FrameworkUdk.dll`, `DCompPrivates_SetVisualRelativeSize(visual, float, float)`
   (export 0x18, RVA 0x16120 -> 0x1f180 -> the DComp-privates singleton's slot 22 at 0xd450): the function queries the visual for
   {2c4eef28-1bc0-4736-b7dd-b62692f9bd67}, an interface that `dcompi.dll` implements for its visuals, and the visual `Microsoft.UI.Input.dll`
   passes is NULL. The chain reaches it from `Microsoft.UI.Input.dll` after `DispatcherQueue` is activated, before any window is shown. The
   stubs of `Windows.UI.Composition.Compositor` log only the start-up call, so the NULL does not come from them: it is a visual that the lifted
   compositor (`dcompi.dll`) never created, most likely because its engine (command stream to the system compositor) is not there.
   Tools: `mapav2.py` (Nth stack dump of a thread, with the +loaddll bases), `readguid.py`, `findguid.py`, `impcalls.py`.

7. **Where the NULL visual comes from (found 2026-11-23).** It is not a `dcompi.dll` object that was never created, and not the Compositor
   stubs. `Microsoft.UI.Input.dll` creates its visual with `DCompPrivates_CreateSharedVisual`; FrameworkUdk implements the `DCompPrivates_*`
   functions itself (a table by id from `Microsoft.Internal.FrameworkUdk.System.dll`, a Windows system DLL that Wine does not have; without
   it FrameworkUdk uses its built-in default tables) and chooses its "UDK helper" class by the highest `Windows.Foundation.UniversalApiContract`
   major the system claims (`IsApiContractPresentByMajor`, from 17 down; Wine answers up to 10, Windows 11 up to 19, measured). The class for 10
   asks the CoreUI session (`CoreUICreate`, our `coreui.c`) for an item, registers a callback (session slot 12), then queries an object for
   {37a94c05-1aa6-4cea-9049-083bba055cec} (answered by the *generic* object of `coreui.c`) and calls its slot 5 with (a pointer, 0, 2, the
   object). **Correction (same day, after the Windows 11 probe `coreui-iface-probe.c`):** I first took that slot-5 call for the one that makes the
   visual. It is not: the FrameworkUdk function around it (RVA 0xbac0) copies a 35-byte record, asks the singleton's slot 4 for an object and
   then calls slot 5 of {37a94c05} with (the record, a value, kind 2): a registration. Making slot 5 fail only moves the fail-fast earlier (it
   hides the later NULL read), so it proves nothing about the visual. What the probe did establish on Windows 11:
   - {37a94c05} is answered by the object that `ICoreUISession` slot 3 returns (called with NULL arguments: S_OK, the same pointer for
     {37a94c05}, a vtable of at least 12 slots in CoreMessaging.dll, slot 9 empty); neither the CoreUICreate object, the session interface, the
     item of slot 8 nor its interfaces answer it. None of the other IIDs FrameworkUdk queries ({5d167ee3}, {53456138}, {4ba72ec1}, {2f292ff4},
     {1bf477b7}, {6083f66a}, {9cbd9312}, {5680c979}) is answered by those objects either.
   - {37a94c05} is also implemented by the lifted `CoreMessagingXP.dll` (twice in its data), so it is a CoreMessaging interface in general.
   - Slot 5 of the real object faults with a C++ exception for the shapes I tried (a zeroed buffer, NULL or small values): the record has to be
     valid (35 bytes of a layout not yet known).
   The NULL visual itself is still not located: `DCompPrivates_CreateSharedVisual` (default table id 6, slot 1 -> the singleton's slot 14)
   queries its first argument for {4ba72ec1}, which no object I can reach answers, and Wine's run shows no failure there, so the call may not
   be where Input's `visual` is set. Next: trace the stores to Input's `this+0x48` at run time (a data breakpoint through the exception
   handler, or the relay log of all `Microsoft.UI.Input.dll` calls into FrameworkUdk, which `RelayInclude` did not give for these names).
   Tools: `readguid.py`, `readstr.py`, `readptrs.py` (tables in .rdata), `impcalls.py`, `findguid.py`, `singleton-calls.py`.

The step after is design, not a stub: what the runtime's compositor hands to the system compositor (the command stream, shared visual handles,
the island and HWND hosting of `DesktopWindowXamlSource`) must be answered by Wine's own dcomp (about 5000 lines in the fork). Next: find which
call of `dcompi.dll` hands `Microsoft.UI.Input.dll` the NULL visual (relay of the `Microsoft.UI.Composition` activation factories and of the
visual creation calls), then what it needs from the engine.
