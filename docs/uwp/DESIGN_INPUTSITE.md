<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Design note: the input-site classes for Notepad (Windows App SDK), 2026-11-26

Read with [HANDOFF_NOTEPAD.md](HANDOFF_NOTEPAD.md) (wall 1 and target 2 of the ranked list). This note records what was
measured on Windows 11 (build 26300), what follows for Wine, and what is still unknown. Nothing is implemented in Wine yet;
the reason is in "What Wine will be asked" below.

## Summary

* On Windows 11 the two classes live in `InputHost.dll`. Their caller is not the application but the system's own
  `Microsoft.Internal.FrameworkUdk.System.dll`. Wine has neither library, so under Wine the application's own
  `Microsoft.Internal.FrameworkUdk.dll` asks instead, and it asks for another interface (`{8ca0442d-...}`), which this
  Windows build refuses. **The interface Wine must answer cannot be measured on this host.**
* The surface the caller uses is small: one creation method per class and two calls on the configuration object. The work
  is in what the site does afterwards: it takes the caller's composition input sink and has to deliver input through it.
* `Windows.System.DispatcherQueue` is asked for by `InputHost.dll` itself, not by Notepad. A Wine implementation of the
  input site does not need it. Wine's `coremessaging` does not serve that class (it has `DispatcherQueueController` only).
* Next step: run Notepad under Wine with the logging stand-in (`tests/uwp/alpc/inputhost-logstub.c`) once the
  CoreMessaging rounds run, to read the calls the application makes on `{8ca0442d}` from our side.

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

With a host of our own (`inputsite-create-probe.exe none` and `all`) the system ends the process with a fail fast
(`0xc0000409`) as soon as the host refuses `{01dc794b}` or one of the two slot 9 calls fails. A site cannot be made
without a caller that implements both interfaces, so a conformance test on Windows can cover the factories but not a site.

## What Wine will be asked

Under Wine the caller is the application's own copy of the component, and the earlier lab runs show its request:
`InputSite` with `{8ca0442d-ae80-47b0-af23-813fc1da9cd0}` (HANDOFF, wall 1). Windows 11 build 26300 refuses that interface,
so its methods, the interfaces of the site it expects and what that caller's host object offers are not measurable here.
Whether the Windows 10 lab machine has these classes at all is not known: it could not be asked this time (guest control
did not start a session, as on 2026-11-25; the machine was left as it was).

Two ways to find out, in order of cost:

1. **From our side under Wine (preferred).** `inputhost-logstub.dll` in the lab prefix answers every activation and
   `QueryInterface` and logs each call with its arguments; `INPUTHOST_STUB_PLAN` sets what a call returns, one step at a
   time, without a rebuild (first run: which slot of `{8ca0442d}` is called and with what; then hand out a logging site;
   then see what is asked of the site). It was tried on the host with the calls of the Windows 11 caller
   (`inputsite-create-probe.exe all sta PATH\inputhost-logstub.dll`). 64-bit only. It needs the CoreMessaging rounds to
   run first, or the earlier lab state in which the input thread reached this request.
2. **A Windows 11 release without the system component** (an evaluation image in a new lab machine), to trace the same
   application there with `wintrace` and `_O`/`_I`. Only worth it if step 1 leaves the meaning of a call open.

The Windows 11 measurements above are the best guide to the shape meanwhile: the older interface most likely has the same
creation call, a host with the same two questions (an object, the input sink handle) under older interface ids.

## Design for Wine

**Module.** A new builtin `inputhost.dll` (the registration on Windows names that library, and the classes are found
through `ActivatableClassId`, so the name is free; using Windows' name keeps load traces comparable). Not inside
`coremessaging` or `windows.ui`: the classes belong to neither on Windows, and the module will need win32u's input sinks,
which those do not. New files LGPL-2.1-or-later, "Copyright 2026 WineExplorer contributors". Private interfaces get names
of our own in a module-local IDL with the measured interface ids; both architectures; arguments checked;
[STOCK_CHANGES](../STOCK_CHANGES.md) entry.

**Minimum interface set** (to be confirmed by step 1 above for the interface ids the application's own caller uses):

* `InputSite` factory: `IActivationFactory` (`ActivateInstance`: `E_NOTIMPL`), `IAgileObject`, and the creation
  interface the caller asks for, with one method `(host, out site)`.
* The site: `IInspectable`, its default interface (`{cfb32fa7}` on this Windows build; methods unknown, none called at
  start-up), `IAgileObject`.
* `ActivationConfigurationInputObject` factory: `IActivationFactory`, `IAgileObject`, a creation interface with one
  method `(site, out object)`.
* The configuration object: an interface with four methods, of which two are called with 0 at start-up and must succeed.

**What the objects must do.**

* The site keeps a reference to the host and reads the input sink handle from it (the second question above). The sink is
  Wine's own object (`NtCreateCompositionInputSink` in win32u, with the windows it names), so the site can find the
  input window and the application window from the handle. That lookup needs a win32u entry point that does not exist yet
  (sink to windows); it would be a second stock change.
* Input for those windows has to reach the application through the object the host hands out (the first question). Which
  methods of it carry pointer, key and focus events is unknown: nothing calls it while no input arrives. This is the part
  to measure next on Windows with input sent to Notepad (a trace with `_I` and `WINTRACE_FOLLOW` naming the caller's
  module, while keys and pointer moves are sent to the window), and under Wine by seeing what the application does when
  the site calls nothing.
* The configuration object's two calls can be stored and otherwise ignored until their meaning is known.
* First goal: creation succeeds and the page load that follows on Windows (47 ms after the first activation) starts.
  Input delivery through the site comes second; the lab drives Notepad with window messages today.

**Test.** A Wine test for the factories as in the golden file (registration, `GetIids`, `ActivateInstance`,
`GetRuntimeClassName`), identical on Windows. Site creation cannot be tested on Windows without a full host (fail fast), so
it gets a Wine-only test with a host of the test's own, marked as such.

## Still unknown

1. The methods of `{8ca0442d}` and the interfaces the application's own caller expects of the site and offers on its host
   (step 1 above).
2. The methods of the site's default interface and of `{24701c1a}`; the names and types of the configuration object's four
   methods.
3. How input reaches the caller: the methods of the object from `{01dc794b}` slot 9, and what the site does with the sink.
4. Whether the application's own caller also creates the configuration object, and whether it needs
   `Windows.System.DispatcherQueue` from Wine (on Windows the request comes from inside `InputHost.dll`). If it does:
   `IDispatcherQueueStatics::GetForCurrentThread` is public and missing in Wine's `coremessaging`.
5. 32-bit: the factory results are the same; the call shapes were measured in 64 bit only.

## Files

* `tests/uwp/alpc/inputsite-factory-probe.c`: the factories; in `compare/probes.txt` with its Windows golden file, so the
  comparison runner will show the Wine side once a module exists.
* `tests/uwp/alpc/inputsite-create-probe.c`: the creation calls with a logging host; also drives the stand-in.
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
