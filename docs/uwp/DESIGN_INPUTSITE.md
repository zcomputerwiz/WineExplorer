<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Design note: the input-site classes for Notepad (Windows App SDK), 2026-11-26

Read with [HANDOFF_NOTEPAD.md](HANDOFF_NOTEPAD.md) (wall 1 and target 2 of the ranked list). This note records what was
measured on Windows 11 (build 26300) and Windows 10 (build 19045, the lab machine), what follows for Wine, and what is
still unknown. Nothing is implemented in Wine yet.

## Summary

* On Windows 11 the two classes live in `InputHost.dll`. Their caller there is not the application but the system's own
  `Microsoft.Internal.FrameworkUdk.System.dll`. Wine has neither library, so under Wine the application's own
  `Microsoft.Internal.FrameworkUdk.dll` asks instead, for another interface (`{8ca0442d-...}`). Windows 11 refuses that
  interface; **Windows 10 serves it** (and refuses Windows 11's). Windows 10 has no system copy of the component either, so
  it is the reference for Wine's path.
* The creation call has the same shape on both: slot 6 of the factory interface, `(host, out site)`. The site asks the
  host for its composition input sink. With a host of our own that hands out a real sink, a site and a configuration
  object are created on both systems (`inputsite-create-probe.exe sink`), so a Wine test can cover creation on Windows.
* The surface the caller uses at start-up is small: one creation method per class and two calls on the configuration
  object. The work is in what the site does afterwards: it has to deliver input for the sink's windows.
* `Windows.System.DispatcherQueue` is asked for by `InputHost.dll` itself (Windows 11), not by Notepad. A Wine
  implementation of the input site does not need it. Wine's `coremessaging` does not serve that class.
* Next step: the Wine module with the Windows 10 interface ids (below), and a run of Notepad in the lab, with the logging
  stand-in (`tests/uwp/alpc/inputhost-logstub.c`) for any call that is not covered yet.

## State (2026-11-26, later)

The module exists: Wine branch `claude/inputhost` (08192783), pinned and listed on WineExplorer branch
`claude/inputhost-pin`. Its test passes on Windows 10 and 11 (32 and 64 bit) and under Wine (x86_64 in the lab tree; the
i386 run there found the classes unregistered in the 32-bit view of a prefix that was not updated, which is to be checked
in a normally updated prefix). Lab run b41 of Notepad with it (and the lab's CoreMessaging re-arm diagnostic): the input
thread asks for `IActivationFactory` and `{8ca0442d}`, creates three sites (its host answers `{92179cc0}` slot 9 with
three sink handles), asks each site for `IUnknown` and `{f77d91d0}`, creates a configuration object per site and writes 0
to both settings. Nothing else is asked of either object, and there is no fail fast any more. The page still does not
load: the CoreMessaging rounds stop again afterwards. On Windows the system's library makes a system CoreMessaging
registration on the input thread at this point (it activates `Windows.System.DispatcherQueue` itself); whether Wine's
module has to do the same is the open question (unknown 4 below).

## Rules followed

Black box only: exported calls and calls at COM interface boundaries, with their arguments and results. No disassembly, no
symbol names, no code bytes (the tracer now refuses to dump memory of a module). The traces are outside the repository
(`D:\DeepThought\inputsite-probe`). New in `wintrace.c`: `_O` and `_I` trace the interfaces of objects that a traced call
hands out or receives, counted only for calls that come from another module. That goes one step past "objects the tracer
creates"; it stays at interface boundaries, and it should get the same review as the earlier tracer rules.

## Measured on Windows 11

### The factories (`inputsite-factory-probe.c`, golden file `compare/expected/inputsite-factory-probe.windows.txt`)

Same result in a multithreaded and a single-threaded apartment, in 32 and 64 bit.

| class | library | factory interfaces (`GetIids`) | other answers |
|---|---|---|---|
| `Windows.UI.Internal.Input.InputSite` | `InputHost.dll` | `IActivationFactory`, `{73f80911-7a51-5b0f-906b-f6402ea6c3cb}`, `{24701c1a-88f5-4598-b3e4-855085cf2ef1}` | `IAgileObject`, `IMarshal` yes; `{8ca0442d-ae80-47b0-af23-813fc1da9cd0}` no |
| `Windows.UI.Internal.Input.ActivationConfigurationInputObject` | `InputHost.dll` | `IActivationFactory`, `{4c8421ca-3863-5236-b09a-757cf68c5fa5}` | `IAgileObject`, `IMarshal` yes |
| `Windows.System.DispatcherQueue` | `CoreMessaging.dll` | `IActivationFactory`, `{a96d83d7-9371-4517-9245-d0824ac12c74}` (`IDispatcherQueueStatics`) | `IAgileObject`, `IMarshal` yes |

For all three: registration under `ActivatableClassId` with `ActivationType` 0, `Threading` 0, `TrustLevel` 0;
`ActivateInstance` returns `E_NOTIMPL`; `GetRuntimeClassName` on a factory returns `0x8000000e`; `GetTrustLevel` gives 0.
No metadata file names the two internal classes (`RoGetMetaDataFile`: `0x8000000f`), so there are no interface names to take
from metadata. What `{24701c1a}` is for is not known (Notepad does not ask for it).

### What Notepad's input thread does (wintrace, three runs, the same each time)

Three times, once per input window (the three `MITMessageOnlyWindowClass` windows, each with its composition input sink):

1. `RoGetActivationFactory("...InputSite", {73f80911})`, then slot 6 of that interface: `(this, host, out site)`. `host` is an
   object of the caller. Result `S_OK` and a site.
2. On the site: `QueryInterface(IUnknown)`, and on that `QueryInterface({cfb32fa7-5399-5f3d-82ba-9641f106ffcd})`, which
   gives back the pointer the factory returned. So `{cfb32fa7}` is the site's default interface.
3. `RoGetActivationFactory("...ActivationConfigurationInputObject", {4c8421ca})`, then slot 6: `(this, site, out object)`.
   `S_OK`. (Inside this call `InputHost.dll` itself asks for `Windows.System.DispatcherQueue`.)
4. On that object: slot 7 and slot 9, each with a second argument of 0, each `S_OK`. The object's interface has at most
   four methods (slots 6 to 9). Two setters called with 0 is the likely reading; the names are not known.

In 45 seconds without input, no other method of the site or of the configuration object is called from outside
`InputHost.dll`.

### What the site asks of its caller (wintrace `_I`, and `inputsite-create-probe.c`)

During step 1, `InputHost.dll` calls the host object:

* `QueryInterface({01dc794b-4ff5-4491-9942-b9e7b8893be4})`, then slot 9 of it with one out argument: an object comes back
  (another interface of the host object). No call on that object was seen while no input arrived.
* `QueryInterface({92179cc0-ae1f-48bc-a354-8f7a8800f2fb})`, then slot 9 of it with one out argument: a handle comes back.
  It is the composition input sink that the same thread made earlier with `NtCreateCompositionInputSink`: the three
  values match the three sinks in order (trace with both calls).

With a host of our own that refuses (`inputsite-create-probe.exe none` and `all`) the system ends the process with a fail
fast (`0xc0000409`) as soon as a question is refused or its slot 9 call fails. With a host that answers them
(`inputsite-create-probe.exe sink`: a sink made by this program for a window of its own, and the host object itself for
the object question) the whole sequence succeeds; results in `compare/expected/inputsite-create-probe-sink.windows11.txt`:

* the site: class name `Windows.UI.Internal.Input.InputSite`, `GetIids` `{cfb32fa7-5399-5f3d-82ba-9641f106ffcd}` (default),
  `{48c0724a-1768-406a-bb9f-a9e99a2016ba}`, `{2cc67fbf-f13c-42fd-a89c-48ab226b12b8}`,
  `{46cae093-bf9a-4b61-970f-8d9cfaff944e}`, `IClosable`; `IAgileObject` yes, `IMarshal` and weak references no;
* the configuration object: class name `...ActivationConfigurationInputObject`, `GetIids`
  `{773ebb88-99de-4a39-b9fa-02adaec25d4d}` (default), `{9b822734-a3c1-542a-b2f4-0e32b773fb07}`,
  `{29500e25-06ae-536c-af23-47682f700580}`, `{46cae093-...}`, `IClosable`; slots 7 and 9 with 0 return `S_OK`.

## Measured on Windows 10 (build 19045.6456, the lab machine; the reference for Wine)

The factory probe and the create probe, run through guest control (the probes are in `C:\Users\lab\probes`; nothing else
in the guest was changed). Results: `compare/expected/inputsite-factory-probe.windows10.txt` and
`inputsite-create-probe-sink.windows10.txt`.

* `InputSite` factory (`inputhost.dll`): `GetIids` gives `IActivationFactory` and
  `{8ca0442d-ae80-47b0-af23-813fc1da9cd0}`; `{73f80911}` is refused. The reverse of Windows 11.
  `ActivationConfigurationInputObject` (`{4c8421ca}`) and `Windows.System.DispatcherQueue` are as on Windows 11.
* Slot 6 of `{8ca0442d}` takes the same arguments, `(host, out site)`. This shape was tried, not traced: the call works and
  asks the host exactly one question, `QueryInterface({92179cc0-ae1f-48bc-a354-8f7a8800f2fb})` and its slot 9 (the input
  sink). There is no `{01dc794b}` question on Windows 10. A host that refuses ends the process, as on Windows 11.
* The site: class name `Windows.UI.Internal.Input.InputSite`, `GetIids` `{f77d91d0-46ca-4464-99c1-8902aed17680}` (default,
  the pointer the factory returns), `{e6414e4b-1385-554a-b430-ca796b7305f2}`, `{2cc67fbf-f13c-42fd-a89c-48ab226b12b8}`,
  `{46cae093-bf9a-4b61-970f-8d9cfaff944e}`, `IClosable`; `IAgileObject` yes, `IMarshal` and weak references no.
* Slot 6 of `{4c8421ca}` with that site gives a configuration object: `GetIids` `{773ebb88-...}` (default, the same id as
  on Windows 11), `{9b822734-...}`, `{69741938-9de5-57b6-98e7-8895bf9eb420}`, `{29500e25-...}`,
  `{6852a9f9-70a7-5cd1-8b14-eedc412eb722}`, `{46cae093-...}`, `IClosable`. Slots 7 and 9 with 0 return `S_OK`.
* The same with a dispatcher queue on the thread (`sink dq`) and a message loop of two seconds: no call on the host.

Not measured on Windows 10: what an application's own caller does after creation (the lab user has no Windows App
Runtime package there; installing a test application needs the orchestrator's agreement, the machine is read-only so far).

## What Wine will be asked

Under Wine the caller is the application's own copy of the component, and the earlier lab runs show its request:
`InputSite` with `{8ca0442d-ae80-47b0-af23-813fc1da9cd0}` (HANDOFF, wall 1). That is the Windows 10 interface above, so
Wine should behave as Windows 10 does: serve `{8ca0442d}`, refuse `{73f80911}`, and ask the host for its sink through
`{92179cc0}` slot 9.

What that caller does with the site afterwards is still open. Two ways to find out, in order of cost:

1. **From our side under Wine (preferred).** The Wine module logs unknown `QueryInterface` requests as every Wine module
   does; for method calls on interfaces whose shape is not known, `inputhost-logstub.dll` in the lab prefix answers every
   activation and `QueryInterface` and logs each call with its arguments, and `INPUTHOST_STUB_PLAN` sets what a call
   returns, one step at a time, without a rebuild. It was tried on the host with the calls of the Windows 11 caller
   (`inputsite-create-probe.exe all sta PATH\inputhost-logstub.dll`). 64-bit only. It needs the CoreMessaging rounds to
   run first, or the earlier lab state in which the input thread reached this request.
2. **A small Windows App SDK test application on the Windows 10 lab machine**, traced with `wintrace` and `_O`/`_I`
   (the calls of the application's own caller on the real site). Needs the orchestrator's agreement to install it there.

## Design for Wine

**Module.** A new builtin `inputhost.dll` (the registration on Windows names that library, and the classes are found
through `ActivatableClassId`, so the name is free; using Windows' name keeps load traces comparable). Not inside
`coremessaging` or `windows.ui`: the classes belong to neither on Windows, and the module will need win32u's input sinks,
which those do not. New files LGPL-2.1-or-later, "Copyright 2026 WineExplorer contributors". Private interfaces get names
of our own in a module-local IDL with the measured interface ids; both architectures; arguments checked;
[STOCK_CHANGES](../STOCK_CHANGES.md) entry.

**Minimum interface set** (the Windows 10 ids):

* `InputSite` factory: `IActivationFactory` (`ActivateInstance`: `E_NOTIMPL`), `IAgileObject`, `IMarshal`, and
  `{8ca0442d}` with one method `(host, out site)`; `GetIids` as measured; `{73f80911}` refused.
* The site: runtime class name, `GetIids` as on Windows 10 (`{f77d91d0}` default, `{e6414e4b}`, `{2cc67fbf}`,
  `{46cae093}`, `IClosable`), `IAgileObject`. The methods of the four private interfaces are unknown; they start as
  slots that log and fail, and get filled in as the lab run shows which are called.
* `ActivationConfigurationInputObject` factory: `IActivationFactory`, `IAgileObject`, `IMarshal`, `{4c8421ca}` with one
  method `(site, out object)`.
* The configuration object: runtime class name, default interface `{773ebb88}` with four methods (slots 6 to 9), of
  which 7 and 9 are called with 0 at start-up and must succeed; the other listed interfaces as for the site.

**What the objects must do.**

* The site keeps a reference to the host and reads the input sink handle from it (`{92179cc0}` slot 9); creation fails
  when the host refuses. The sink is Wine's own object (`NtCreateCompositionInputSink` in win32u, with the windows it
  names), so the site can find the input window and the application window from the handle. That lookup needs a win32u
  entry point that does not exist yet (sink to windows); it would be a second stock change, needed only once the site
  delivers input.
* How input for those windows reaches the application is unknown (unknown 3 below). On Windows 11 the host also hands out
  an object, which nothing calls while no input arrives; on Windows 10 there is no such object. This is the part to
  measure next: on Windows with input sent to the window while tracing with `_O`/`_I`, and under Wine by seeing what the
  application asks of the site.
* The configuration object holds two 32-bit settings (measured on both systems with `inputsite-create-probe.exe sink`):
  slots 6 and 8 read them through an out pointer, slots 7 and 9 write them; both start as 0 and read back what was
  written, each on its own. They are stored and otherwise ignored until their meaning is known.
* The sink handle is not checked by Windows: an event handle in its place (`sinkbad`) gives a site as well. With a NULL
  handle (`sink0`) the system calls slot 10 of the host's `{92179cc0}` with a handle of its own and then asks slot 9
  again; the site is created even when that call fails. An application's host always has a sink, so Wine only notes it.
* First goal: creation succeeds and the page load that follows on Windows (47 ms after the first activation) starts.
  Input delivery through the site comes second; the lab drives Notepad with window messages today.

**Test.** A Wine test with a host of the test's own that hands out a sink made with `NtCreateCompositionInputSink` for a
window of the test, as `inputsite-create-probe.exe sink` does: factories (registration, `GetIids`, `ActivateInstance`,
`GetRuntimeClassName`), site creation, the site's and the configuration object's class names and interface lists, slots 7
and 9. It passes on Windows 10 as it stands; on Windows 11 the factory interface and two interface ids differ, so the test
takes either set there (the Windows 10 set is what Wine implements).

## Still unknown

1. What the application's own caller does with the site after creation: which interfaces it asks for and which methods it
   calls (the lab run, or a test application on Windows 10).
2. The methods of the site's interfaces (`{f77d91d0}`, `{e6414e4b}`, `{2cc67fbf}`, `{46cae093}`), of `{24701c1a}` on the
   Windows 11 factory, and the names and types of the configuration object's four methods and of its other interfaces.
3. How input reaches the caller. On Windows 10 the host is asked for nothing but the sink, so input most likely arrives
   through the sink's windows or through handlers that the caller registers on the site; neither was seen yet.
4. Whether the application's own caller also creates the configuration object, and whether it needs
   `Windows.System.DispatcherQueue` from Wine (on Windows the request comes from inside `InputHost.dll`). If it does:
   `IDispatcherQueueStatics::GetForCurrentThread` is public and missing in Wine's `coremessaging`.
5. 32-bit: the factory results are the same; the call shapes were measured in 64 bit only.

## Files

* `tests/uwp/alpc/inputsite-factory-probe.c`: the factories; in `compare/probes.txt` with its Windows golden file, so the
  comparison runner will show the Wine side once a module exists.
* `tests/uwp/alpc/inputsite-create-probe.c`: the creation calls with a logging host (`none`, `all`, `sink`); also drives
  the stand-in. Outputs of `sink` on both systems are in `compare/expected/`.
* `tests/uwp/alpc/compare/normalize.py`: the last group of a GUID is no longer taken for an address.
* `tests/uwp/alpc/inputhost-logstub.c`, `.def`: the logging stand-in for the lab prefix (header: how to install and steer).
* `tests/uwp/alpc/wintrace.c`: `_O`, `_I`, `_G`, `_T`, `WINTRACE_FOLLOW`, `WINTRACE_SKIP_MISSING`; `winrt!` targets check
  that a slot leads to code and count only objects of that vtable.

Trace command used (host, Notepad closed before and after):

```
set WINTRACE_WATCH=Notepad.exe
set WINTRACE_FOLLOW=InputHost.dll,Microsoft.Internal.FrameworkUdk.System.dll
wintrace.exe Microsoft.WindowsNotepad_8wekyb3d8bbwe!App 45 OUT.txt ^
  winrt!Windows.UI.Internal.Input.InputSite@{73f80911-7a51-5b0f-906b-f6402ea6c3cb}[6]:isf6_O_I_Q_K_T:10:R ^
  winrt!Windows.UI.Internal.Input.ActivationConfigurationInputObject@{4c8421ca-3863-5236-b09a-757cf68c5fa5}[6]:acf6_O_Q_K_T:10:R ^
  win32u.dll!NtCreateCompositionInputSink:sink_Q:10:R
```
