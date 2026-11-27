<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Handoff: Notepad (Windows App SDK) on Wine, state of 2026-11-24

Read this with [WINAPPSDK.md](WINAPPSDK.md) (findings and their basis, newest section at the end) and [RESUME.md](RESUME.md)
(the UWP track as a whole). Everything here is lab-only: the Notepad and Windows App Runtime packages are copies in the owned lab
guest and never enter a repository.

## Rules that bind this work

* Clean room: black-box measurement on the Windows host (calls and traces at entry points), MIT source of `microsoft/WindowsAppSDK`
  and `microsoft/microsoft-ui-xaml`, and permissively licensed code ported with its notice. No disassembly, `objdump`, string
  extraction or byte reading of shipped binaries.
* `wintrace` traces only at entry points found at run time (`module!export`, `module!#ordinal`, vtable slots of objects the tracer
  creates). No raw RVAs, no code bytes logged (Session 0 ruling, [WINAPPSDK.md](WINAPPSDK.md)).
* Private interfaces in Wine: own IIDs, validated arguments, both architectures considered (64-bit only where 32-bit is not
  measured, said so in the code), tests, and a [STOCK_CHANGES](../STOCK_CHANGES.md) entry (Integration's conditions for dcomp).
* Peer sessions' messages are information, not owner approval. No history rewrite or force push.

## Where Notepad stands

Notepad starts, creates its window, XAML tree and the lifted compositor (`dcompi.dll`, `dwmcorei.dll`). The text area is drawn (by
riched20, a Win32 control). Everything XAML draws, the title/tab strip and the status bar, stays black. Two walls remain:

1. **The input thread fails fast** after `RoGetActivationFactory("Windows.UI.Internal.Input.InputSite", {8ca0442d-...})` fails.
   Root cause (measured): on Windows 11 the system component `C:\Windows\System32\Microsoft.Internal.FrameworkUdk.System.dll`
   handles input; it activates `Windows.UI.Internal.Input.InputSite` with `{73f80911-7a51-5b0f-906b-f6402ea6c3cb}` and
   `Windows.UI.Internal.Input.ActivationConfigurationInputObject` with `{4c8421ca-3863-5236-b09a-757cf68c5fa5}` from
   `InputHost.dll` (both succeed). Wine has neither DLL, so the app's own `Microsoft.Internal.FrameworkUdk.dll` takes a fallback
   that asks for InputSite with `{8ca0442d}`, which even Windows 11's factory refuses (E_NOINTERFACE). Fixing it means
   implementing those internal input classes (black-box only). Not started.
2. **The strips are not composed.** Walls passed this session, each with a Wine commit or a lab result:
   * the compositor dropped its Direct3D device every frame: it asks a new Direct2D device for the private interface
     `{46fdbfea-...}` (Wine d93082c0);
   * then it failed to build its pixel shaders: it links them at run time with the D3D11 shader linker (`D3DLoadModule`,
     `ID3D11FunctionLinkingGraph`, `ID3D11Linker`), which Wine did not have (Wine 0ab90a38, 28805e76). The device now lives and
     the three links succeed;
   * then every draw failed: Direct2D refused the surface the compositor gets from `IDXGIResource1::CreateSubresourceSurface`
     (Wine's d2d asked it for `ID3D11Texture2D`, which Windows' subresource surfaces do not answer either; Windows' d2d reaches
     the texture through `IDXGISurface2::GetResource`). Fixed in Wine 01af4686: the draws succeed now (0 refusals).
   * The compositor called slot 7 of the Direct2D context's private interface `{2ea67ed7}`, a stub that ended the process; slot 7
     is a private SetTarget (measured, `tests/uwp/alpc/d2d-ctx-slot7-probe.cpp`) and slot 15 succeeds (Wine 65a01349). The draws now
     succeed. Per frame on Windows: method 46, three times (BeginDraw, slot 7, sometimes slot 15, EndDraw), Commit, method 45, the
     clock wait. The compositor does commit under Wine too: Commit was simply not traced (Wine 13a303b3 traces it; methods 45/46
     count the Commits). With the lab bypass, Notepad's window now shows: frame, icon and the light text area; the compositor's
     surfaces (title/tab strip 1428x33, status bar) are black.
   * Black strips, part 1 (Wine 7f2fe720): the compositor's scene was never shown. Per window the UI thread makes two shared visual
     handles, opens them as visuals and puts one into its window target's tree; the compositor opens a duplicate of that handle as
     a target and sets its root there. Wine matched handles by value and gave such targets no window. Now a handle is its kernel
     object and a target opened from it is a portal into the visual; the compositor's surfaces reach dxgi in the window's scene.
   * Black strips, part 2 (open): the three surfaces are fully transparent (lab pixel statistics of dxgi). Per surface the
     compositor only does BeginDraw, d2d slot 7, Clear, EndDraw (one DrawIndexed). On Windows (wintrace `d3d11.dll!context[N]`,
     3 frames) its thread also issues 15 Draw, ~65 PSSetShader, ~74 PSSetShaderResources: it draws its sprites (XAML's text and
     shapes, which the UI thread renders with d2d into its own bitmaps: 19 DrawGlyphRun, 15 FillGeometry) after the clear. Under
     Wine it never draws them. Not the cause: d3d11 fences/queries (none used), `{f13ebcd1}` (refused on Windows too), the d3d11
     private interface `{26c5dc23}` (measured: writes nothing). Next: find what the compositor's sprite pass depends on (compare
     the Microsoft.UI.Composition calls the UI thread makes with Windows; XAML's surfaces of `CompositionDrawingSurface`).
   * Black strips, part 2, findings of 2026-11-24/25. **The "sprite pass" premise above is not confirmed.** Per-thread traces on
     Windows 11 (wintrace, 25-40 s runs) show:
     - The compositor thread (the one that calls `DCompositionWaitForCompositorClock` and Commit) commits only twice. It opens
       the UI thread's two shared NT-handle textures (`OpenSharedResource1`) and issues no Draw, DrawIndexed or
       DrawIndexedInstanced, no Direct2D BeginDraw, and no BeginDraw on its dcomp surfaces (`dcomp.dll!surface[3]` and
       `vsurface[3]`: 0 hits).
     - All drawing (Direct2D BeginDraw 38, Draw 70, DrawIndexedInstanced 27, OMSetRenderTargets 80) is on the UI thread.
     - Under Wine the split is the same, except that the compositor calls the public BeginDraw once on each of its three
       surfaces (1428x32, 1428x33, 287x32; Windows: 1440x32, 1428x33, 1024x832). The textures behind them get render-target
       views only from that one draw, so under Wine nothing writes the strips after their first, empty frame.
     - **Explained (2026-11-25): these Windows runs were taken on a locked host session.** A capture of Notepad's window
       rectangle during a trace shows the lock screen, although the window is visible and not cloaked (`IsWindowVisible`,
       `DWMWA_CLOAKED`). While the session is locked nothing is composed, so the compositor idles. The earlier per-frame
       measurement was taken unlocked. **Windows traces of the drawing must be taken with the desktop unlocked and Notepad on
       screen**; check with a capture of the window (only its rectangle) during the trace.
     - **Unlocked trace (2026-11-25, Notepad on screen, capture checked; 25 s):** the compositor thread (stacks in
       `dwmcorei.dll`) *does* draw its three surfaces itself through the public `BeginDraw`/`EndDraw` (`surface[3]`/`[4]`: 16
       each, virtual surfaces 0) and commits 12 times. It redraws as content changes: one surface 13 times, the other two 3 times
       each, one Commit after each group. Inside every draw: Direct2D work (`UpdateSubresource` 1-3, `Draw` 1-7, called from
       d2d1) plus 1-9 `DrawIndexed` of the compositor's own; four draws start with `CopySubresourceRegion` called from dcomp
       (old content carried into the new buffer). The text itself (DWrite in the stacks: 13 Draws) is drawn on the UI thread
       into its own bitmaps, done by 9.8 s. Under Wine (a8.log) the compositor draws each surface once, and inside it there is
       only one `DrawIndexed` between d2d's `SwapDeviceContextState` pair (the clear); it never draws again.
     - **Shared textures are not coherent in this fork** (`dlls/d3d11/device.c` `OpenSharedResource1`, `dxgi/resource.c`,
       from ade07fe6): the contents travel through a CPU file mapping (`WineSharedTex_*`). The producer publishes to it only in
       `ID3D11DeviceContext4::Signal`; the consumer reloads only in `Wait` or keyed-mutex `AcquireSync`. Notepad's UI thread
       and compositor call none of these, so an opened texture keeps its contents from open time. That has to be fixed anyway
       (same-process sharing without explicit sync), but it is **not** what stops the strips: a lab run (LAB-SYNC: publish on
       `Flush` and d2d `EndDraw`, reload on `PSSetShaderResources`, plus LAB-A8) gave 0 reloads. Wine's compositor never binds a
       pixel-shader resource, so it never samples the UI thread's bitmaps at all. It skips the sprite pass: its scene has no
       sprites to draw, or it does not consider them drawable.
     - **How the compositor draws sprites (2026-11-25, measured):** per surface it calls BeginDraw, d2d private slot 7
       (target), SetTransform, slot 10 (pointer, 0), Clear, and once content exists slot 15 (an object of its own, 0), sometimes
       FillGeometry, then EndDraw. Slot 15 queues the object; at Flush/EndDraw Direct2D calls the object's slot 3 and the
       object issues the sprite DrawIndexed calls itself (`dwmcorei.dll` frames above `d2d1.dll`). Probe:
       `tests/uwp/alpc/d2d-ctx-slot15-probe.cpp`. Wine's slot 15 dropped the object; Wine branch `claude/notepad-d2d-slot15`
       (97ee7765) queues and calls it (STOCK_CHANGES).
     - **But under Wine the content never arrives:** in a 150 s lab run (`ovr13g.sh`: longer run, mscoree/mshtml disabled,
       timestamps) slot 15 is never called. Notepad hangs about 10 s in, after the compositor's first frame (Commit, method 45,
       clock wait, method 46) and the UI thread's first text draws; every thread waits from then on. The hang was already in
       earlier runs (their logs end at the same place). What is known: the UI thread and both wined3d_cs threads wait on
       per-thread futex slots (NtWaitForAlertByThreadId: an SRW lock, condition variable or WaitOnAddress, not a critical
       section, since no "wait timed out" ERR appears); the compositor thread and the rest wait in the server. The traced locks
       (d2d factory, d3d multithread) are balanced, and dcomp's SRW locks are released on every path. winedbg's attach does not
       complete; gdb sees no symbols (the preloader hides the library list).
     - **Not a hang, both threads idle (2026-11-25, `+server` run):** the wineserver answers (a fresh `wine cmd` runs). The UI
       thread ends in its ALPC message receive (`alpc_send_receive` on its port, then an infinite select on it): its normal
       message loop. The compositor thread ends in a wait on six handles, among them an I/O completion port that receives
       packets from `NtAssociateWaitCompletionPacket` on an auto-reset event that the UI thread sets once per batch. The UI
       thread set it three times; the compositor got the three packets, the last one right after the UI thread's text draws,
       committed, re-armed the packet and reset the event. So the compositor did receive the batch with the text, and did not
       call BeginDraw on its surfaces for it (its three draws are all in its first frame). The main thread waits in
       WaitOnAddress from Notepad's own code (cmp 1), probably its idle state. A lab-only stack log of infinite
       RtlWaitOnAddress waits (LAB-WAIT) showed nothing blocked in d2d, d3d or dcomp.
     - Next: why the compositor skips redrawing its surfaces for a batch with new content. Candidates: a visibility or
       occlusion check (the first frame is drawn regardless; dxgi occlusion status, DXGI_STATUS_OCCLUDED from Present,
       cloaking or IsWindowVisible), or a batch without the sprite updates (compare the UI thread's Microsoft.UI.Composition
       calls between its text draws and the batch with Windows).
     - **Compositor thread loop, Windows 11 against Wine (2026-11-26; wintrace on exported calls, lab +relay).** Windows, one
       frame of the compositor thread (the one that calls `DCompositionCreateDevice2`): `DCompositionWaitForCompositorClock(0,
       NULL, 80)`; `NtWaitForMultipleObjects(1 handle, timeout pointer)` (a poll); three `NtSetEvent`; `NtWaitForMultipleObjects(6
       handles, infinite)`, which returns; five `NtWaitForSingleObject` on five handles (the compositor's worker threads);
       next clock call. After its second clock call (some 25 s runs show a second thread calling the clock too) it makes three
       `D3D11CreateDevice` calls, each followed by one `DCompositionCreateSurfaceHandle` (five handles in all), and commits again
       and again. Wine: one clock call, one such cycle (three `SetEvent`, the six-handle wait), and the wait never returns: the
       UI thread sits in its plain `GetMessageW` loop (it is not hung) and nothing signals the six handles again. The workers
       exist (event wait, message loops, completion-port wait), so the compositor is idle for lack of a second batch from the UI
       thread, not blocked by a missing object. The UI thread side (Microsoft.UI.Composition creating drawing surfaces, hence
       `DCompositionCreateSurfaceHandle`, which Wine never receives) is where to look next; the clock itself is not the gap
       (`DCompositionWaitForCompositorClock` is implemented and returns 0 as on Windows).
     - **Steady state of the CoreMessaging worker (2026-11-26, wintrace 25 s, Windows 11).** The worker thread repeats `2582`,
       `SetTimer(hwnd,1,0)`, `SetTimer(hwnd,1,0x7fffffff)`, Drain2, `MsgWait(1 handle, infinite)` about 85 times (and the UI
       side's 0x60 posts: `PostMessage(0x60,6)` 80 times, `SendMessageCallbackW(0x60,5,5)` 80 times, `NtAssociateWaitCompletionPacket`
       685 times in all). Wine's run of the same length: 6 timer cycles, one `PostMessage(0x60,6)`, 27 associations, and `SetEvent`
       on the process-wide handle that Windows's threads set 70 times (compositor, XAML and CoreMessaging threads) only at
       start-up. The worker's own wait handle is set once; it is woken by completion packets (associations re-armed per round).
       So the loop is self-sustaining on Windows and stops after start-up in Wine: some thread's wake of the next round never
       happens. Probes of the individual calls match (`tests/uwp/alpc/compare`), so the missing link is between rounds
       (who sets the events the packets are associated with). Finding that needs the call graph, which the TTD rule does not
       allow us to read; the next step is to bisect with exported-call traces filtered to the events set just before each
       Windows round.
     - **Why Windows keeps going and Wine stops: the edit thread's start-up (2026-11-26; wintrace on exported calls, lab +relay;
       `tests/uwp/alpc/loop-probe.c` and `np-drive.c`).**
       * The worker round alone (`loop-probe`: 2582, SetTimer 0 and max, Drain2, MsgWait) sustains itself on Windows *and* Wine
         (the 2582 message wakes the wait), so the CoreMessaging calls are not what stops Wine. On Windows the UI thread keeps
         waking the worker (SetEvent, then `PostMessage(worker,0x60,5,4)`, every 0.3 to 0.7 s).
       * What the UI thread does on Windows after its `PostMessage(0x405)` and `SetForegroundWindow`: more `SetWindowPos` rounds,
         thread creations, then a run of `SendMessage` calls that configure the RichEdit (0x443, 0x444, 0x4e0, 0x2e0), a
         `SendMessage(textbox, 0x5fc)` and a **`PostMessage(textbox window, 0x5fc, pointer, pointer)`**. Wine's UI thread stops at the
         `PostMessage(0x405)` plus three `SetWindowPos` and never posts 0x5fc (Wine log: 44 window calls in all, none after that).
       * The edit thread's handling of that posted 0x5fc, Windows: `SetFocus(RichEdit)`, `SendMessage(UI window, 0x5fd)`,
         `InvalidateRect`, `WM_COMMAND(EN_SETFOCUS)` up, `WM_IME_SETCONTEXT`, post 0x602 to the UI window, **`CreateCaret`**. The caret
         makes `WM_SYSTIMER` 0xffff (the blink, ~530 ms): 15 in 18 s, each followed by a paint of the edit window and a compositor
         frame. Wine: no `SetFocus` on the edit thread, no `CreateCaret`, no `WM_SYSTIMER`: nothing keeps the compositor busy.
       * Real input does not help (`np-drive.exe` in the lab, click and typing): `WM_LBUTTONDOWN`, `WM_KEYDOWN` and `WM_CHAR` reach the
         UI thread's windows, yet no caret, no surfaces, one clock call.
       * So Notepad's UI thread, in Wine, does not get past its start-up work: the dispatcher never receives the item that posts
         0x5fc. Candidates: a WinRT async operation that never completes (the log shows `Microsoft.Windows.ApplicationModel.Resources.
         ResourceManager` and `Windows.Internal.StateRepository.Package` activated; `Windows.Security.EnterpriseData.ProtectionPolicyManager`
         has no library: `err:combase:RoGetActivationFactory`), many `IPropertyValue` interfaces refused (`property_value_QueryInterface`,
         ~300 times), `Windows.UI.ViewManagement.UISettings` and `ApplicationLanguages` semi-stubs. Next: on Windows record which
         WinRT classes/methods precede the 0x5fc post on the UI thread (wintrace `winrt!Class@{IID}[N]` targets) and compare with Wine.
     - **WinRT activations compared (2026-11-26; wintrace `combase!RoGetActivationFactory:..._H` on Windows, the Wine log).** Windows 11's UI
       thread activates 87 distinct classes before it posts 0x5fc; Wine activated 66 and the activation of
       `Windows.Security.EnterpriseData.ProtectionPolicyManager` failed (no library): that was the first missing class and the UI thread
       ended its start-up there. The Wine module `windows.security.enterprisedata` (STOCK_CHANGES) now serves it (Wine `b364b217`, test
       identical on Windows 11 and Wine). Notepad then goes on exactly as on Windows up to `UserActivityRequestManager`. (The
       `...PrivatePT` name is activated by Windows' own implementation of `add_PolicyChanged`, not by Notepad; Wine's Notepad never asks
       for it, which is right.) Still missing after that point: the UI thread's template work (`TabViewListView`, `TabViewItem`,
       `ToolTip`, `VisualStateManager`, `KeyboardAccelerator`, `DataTemplate`, `VisualTreeHelper`) and, on the edit thread, the text-input
       classes `Windows.UI.Internal.Input.InputSite`, `ActivationConfigurationInputObject`, `Windows.System.DispatcherQueue` (three times)
       and `Windows.Globalization.Spelling.Internal.Facility`. The edit thread's RichEdit is Wine's, which never asks for them: they are
       the next candidates (the Windows RichEdit starts its text input services there, then the UI thread's first layout pass runs).
     - **Notepad's RichEdit (2026-11-26; `tests/uwp/alpc/richedit-probe.c`, `dwrite-families-probe.c`, both in the comparison runner).**
       * The control is not Wine's: the package ships **its own `riched20.dll` (16.0.20330, with `msptls.dll`)** that registers
         `RichEditD2DPT`; Windows loads it from the package directory because riched20 is not a KnownDLL. Wine's builtin riched20
         (5.x look-alike) has no such class. Without a `riched20=native` override Wine loads the *builtin* under the package path
         ("Notepad\riched20.dll builtin"), `CreateWindowExW("RichEditD2DPT")` fails and Notepad ends in `terminate()` (probe:
         "class RichEditD2DPT: not registered"). The lab sets the override by hand (`ovr13g.sh riched20`); **a packaged process needs
         a loader rule, not a per-run switch: a DLL that sits in the package directory and is not a KnownDLL wins over a builtin of the
         same name.** Not done yet (the activation code is the test scripts, there is no product launcher to put it in).
       * Run natively under Wine the control behaves as on Windows 11 for the messages Notepad sends (text, selection, undo, zoom,
         char formats, 0x5fc-era private messages: same results) **if DirectWrite knows the face**. With Notepad's default face
         `Consolas` unknown, DirectWrite finds no family and the control lays out nothing: `EM_GETLINECOUNT` 0, `EM_LINEINDEX` -1,
         `EM_SETSEL(2,7)` gives selection 0..0, typed characters append at the start, no caret. The project's font mapping
         (`assets/font_substitutes.json`) had no Consolas (nor Cascadia, Georgia); added (Consolas, Cascadia Mono/Code ->
         Liberation Mono, Georgia -> DejaVu Serif). With it the probe matches Windows. Wine's dwrite only honours the `Replacements` key
         for names that are not installed, as designed (STOCK_CHANGES, B16). Carlito/Caladea must be installed for Calibri/Cambria (the
         lab has no Carlito: the control's own default face is Calibri).
       * That did not move Notepad (b28 run: still one clock call, no surfaces, no caret, no 0x5fc post): the layout is a prerequisite
         for the caret, not the cause of the UI thread's missing second batch.
       * **Loader rule done (2026-11-26).** Wine `claude/package-dll-load-order` (aa5b882a; design `DESIGN_PACKAGE_DLL_LOAD_ORDER.md`):
         a native file in `WindowsApps\<package full name>\` gets `native,builtin`. Lab run b29 without the override: the package's
         riched20 loads and `RichEditD2DPT` is created. Notepad's stall is unchanged.
     - **Where the UI thread's start-up diverges (2026-11-26; lab b29/b30/b31, Windows wintrace `wt-uiafter2`, `wt-disp`, `wt-loop`).**
       * Windows, same thread, same activation: after `UserActivityRequestManager` (interop `GetForWindow`, then `add_UserActivityRequested`)
         the stack names `Notepad.exe`, `user32`, `Microsoft.UI.Windowing.Core`: it runs inside a window message of the app window. The
         next activations are `PropertyValue`, `Uri`, then the template classes (`TabViewListView`, `TabViewItem`, ...) with
         `NotepadXamlUI.dll` over `Microsoft.ui.xaml.dll` on the stack (XAML loading the page).
       * Exported calls on Windows right after it: `GetWindowThreadProcessId` six times (one window: the same check six times, all
         inside Windows' appcore), `GetMessageW` twice, posted (0x60,5,4) wakes to two CoreMessaging windows (its own and another
         thread's), `SetWindowPos` on the three input-site windows, then rounds of `SetTimer(1,0x7fffffff)`, posted (0x60,6,0),
         `SetTimer(1,0)`. Wine: one `GetWindowThreadProcessId` (our appcore), `GetMessageW`, one round of (5,5),(0,0),(5,4) on its own
         adapter window, one posted (6,0), `SetWindowCompositionAttribute`, then the first D3D device (on the UI thread) and a few
         `agile_ref_Resolve` calls, then nothing. No exception on the UI thread, no HRESULT failure logged.
       * So the next step is the code that follows the subscription (it enqueues the XAML page load): on Windows it runs, on Wine the
         thread returns to the loop with nothing queued. Which condition it tests is not known. Candidates: a value from an object
         Wine serves (`ApiInformation`, `UISettings`, `CoreApplication`, `AppWindow`, the manager), or work enqueued to a queue whose
         wake (6,0) never repeats. Next experiment: a Windows trace of the exported calls between the subscription and the first
         `Uri` activation (`GetMessageW`/`PeekMessageW` returns with message ids), and a Wine run with `+twinapi,+wintypes,+windows.ui`.
     - **The gate is the input thread, not the UI thread (2026-11-26, later; Windows `wt-loop2`, `wt-npost`; lab b29, b33-b36).**
       * Windows (DispatchMessageW's MSG, exported calls only): after the manager the UI thread *does* return to its loop (first
         `GetMessageW` right after the six `GetWindowThreadProcessId`), dispatches 0x31f, its own `WM_USER+5` (posted by Notepad just
         before the manager), WM_PAINTs and about 2 s of CoreMessaging rounds. The page load (`PropertyValue`, `Uri`, then the
         TabView templates) starts 47 ms after Windows App SDK's input thread activates `Windows.UI.Internal.Input.InputSite`. That
         thread gets four completion wakes (0x60, 0x80000001) in those 2 s, then (6) drain rounds, then InputSite.
       * Wine dispatches the same first messages (`WM_USER+5` included) and the same first rounds. Its input thread gets **one**
         completion wake. In the (1) handler CoreMessagingXP resets the event, drains, runs its work list, and never re-associates the
         wait packet (Windows re-associates it there, TTD note above), so later signals of that event no longer wake the thread.
         No InputSite, no page load.
       * No user-mode code on Windows posts (0x60, 1, 0) (`wt-npost`: win32u-level `NtUserPostMessage` sees only (5,4) and (6,0)), so
         the (1, 0) comes from the completion path itself. Plain probe windows get (3, 0) inside the wait instead, whether they
         register through win32u or through user32 ordinal 2669 (`drain3-probe.c`).
       * Tried and dropped: a drain call that delivers the pending zero-APC completion as (3, 0). CoreMessagingXP fail-fasts on it
         (e0464645, input thread), with and without the IsGUIThread change below.
       * **IsGUIThread (Wine `claude/isguithread`, 6a06879a).** On a new thread CoreMessagingXP asks `IsGUIThread(FALSE)` and sets up its
         private completion port and the cancel/re-associate packet sequence only when the answer is 0 (as on Windows, where a new
         thread is not a GUI thread until its first win32k call; `guithread-probe.c`). Wine's stub returned TRUE, so it took another
         path. Implemented; the input and UI threads now take the Windows path (b35). The stall is unchanged: still one wake.
       * Next (done below): what makes Windows' input thread re-associate after the (1) wake.
     - **The input thread's single wake is a consequence, not the gate; Wine stops before the text input set-up (2026-11-26, later;
       Windows `wt-drain`, `wt-drainq`, `wt-h1`, `wt-timer`, `wt-sink`, `wt-tif`; lab b35; exported calls only).**
       * Tooling first: `wintrace.c` had two faults that the earlier Notepad traces hit. With `WINTRACE_WATCH` (Notepad restarts
         itself) the first process kept its breakpoints and its traps were handled against the second one, so calls of the real
         process were lost and threads looped on stale breakpoints. Separately, a return breakpoint (`R`) reached first by another
         thread made that thread run one byte off and crashed Notepad (0xc000041d). Both are fixed (the first process's breakpoints
         are taken out; a foreign thread steps over a return breakpoint). New label suffix `_Q`: the 4 qwords behind rdx/r8/r9 at entry
         and at return. Conclusions drawn from earlier `exe:` traces of ntdll calls should be re-checked with the fixed tracer.
       * The (1) handler is the same on both sides. Windows: `ResetEvent(first event)`, `Drain2(hwnd, buf, buf+8, stack)` -> 1,
         `RtlInterlockedFlushSList` -> a work list, `PostMessageW(hwnd, 0x60, 5, 4)`, flush, push, `SetEvent` of the UI thread's event,
         **no re-association**; Wine: the same calls in the same order. Drain2 writes nothing to any of its three buffers on Windows
         either (4 qwords each, before and after: identical), so its output cannot steer anything. In later rounds Windows calls
         drain, then `NtAssociateWaitCompletionPacket(first packet, Init2 port, first event, key)`, then flush (empty).
       * Windows' later wakes are not completions out of nothing: the UI thread signals the input thread's event through a
         **duplicate handle** about every 300 ms (`NtSetEvent` from `CoreMessagingXP`, with XAML, dcomp, dxgi, gdi32 on the stack),
         i.e. whenever the UI thread queues work for the input thread. Under Wine the UI thread signals it once (275.049) and then
         has nothing more to send, because it stops (below). The second packet of the input thread targets a waitable timer
         (`CreateWaitableTimerExW`) that nobody sets, on Windows or Wine. A window timer on a registered window stays an ordinary
         `WM_TIMER` on Windows (`cmtimer-probe.c`), so it is not a hidden wake source either.
       * Where Wine stops instead (activations, windows, loads; Windows run `wt-sink`/`wt-tif` vs b34/b35): Wine reaches the same
         classes up to `NotepadXamlUI.TabData` and `UserActivityRequestManager`, the three `InputSiteWindowClass` windows of the UI
         thread, the three `MITMessageOnlyWindowClass` windows of the input thread (each with `NtCreateCompositionInputSink` and
         `DelegateInput`, as on Windows), and the edit thread with `NotepadTextBox` and `RichEditD2DPT`. Windows then: during
         `Microsoft.UI.Text.FontWeights` the UI thread loads `WinUIEdit.dll`; `msctf.dll` creates `CicMarshalWndClass` and loads
         `TextInputFramework.dll` on the UI thread and creates the `MSCTF...` IME window; the edit thread starts and creates its
         windows and its system CoreMessaging window (`SystemUserAdapterWindowClass` + Init2); about 0.4 s after the edit thread starts,
         the input thread activates `Windows.UI.Internal.Input.InputSite` (loads `InputHost.dll`), then the UI thread's
         `PropertyValue`/`Uri` and the TabView templates follow. Under Wine `WinUIEdit.dll` loads (276.016) and the UI thread makes
         no further library, COM or activation call; there is no `CicMarshalWndClass`, no `TextInputFramework`, no
         `SystemUserAdapterWindowClass` anywhere, no InputSite. No COM creation and no `TF_*` export is involved in that Windows
         step (`CoCreateInstance`, `TF_CreateThreadMgr`, `TF_GetThreadMgr` traced: none on the UI thread there).
       * Under Wine the UI thread is not blocked after `WinUIEdit.dll`: it keeps pumping (GetMessageW, the lab driver's keys reach
         it); it just never starts the text input set-up.
       * **What starts it on Windows (`wt-ctf`, `wt-ctf2`):** the UI thread's `ShowWindow(main window, SW_SHOWDEFAULT)`. Inside it,
         after its `SetWindowPos` calls, user32 calls `imm32!CtfImmTIMActivate(0x04090409, 1, 0xc0008008, main window)`; inside that
         `msctf` creates `CicMarshalWndClass`, then `TextInputFramework.dll` loads and the `MSCTFIME UI` window follows (with
         `ImmGetDefaultIMEWnd` calls from msctf). Earlier calls with second argument 0 come from window creation (user32/uxtheme) on
         each GUI thread; the edit thread's RichEdit later makes its own `(hkl, 1)` call (imm32, user32, comctl32, riched20).
         `ImmDisableTextFrameService`, `CtfImmIsCiceroEnabled`, `CtfImmIsTextFrameServiceDisabled`, `TF_CreateThreadMgr`,
         `TF_GetThreadMgr`, `TF_InitSystem` are not called. In Wine `imm32.CtfImmTIMActivate` is `@ stub` and nothing calls it;
         Wine's `ShowWindow` does no TSF activation, so no `CicMarshalWndClass`, no `TextInputFramework`, no IME window.
       * Next: (1) a probe on Windows: a plain window shown with `ShowWindow`, then the thread's TSF state (`ImmGetDefaultIMEWnd`,
         `TF_GetThreadMgr`, whether `CicMarshalWndClass` exists on the thread), to pin down when user32 activates TSF (first show of
         a top-level window? first activation?); (2) in Wine, implement `CtfImmTIMActivate` (activate msctf's thread manager for
         the thread) and call it where Windows does, then rerun Notepad and see whether `TextInputFramework`'s part (not in
         Wine) matters before InputSite. The missing system CoreMessaging instance (`SystemUserAdapterWindowClass` on the UI, input
         and edit threads) is the other candidate.
       * Tracer trap: `combase.dll!RoGetActivationFactory` with `R` (return breakpoint) hung the first Notepad process before its
         restart (nested activations on one thread); trace activations with `_H` and no `R`.
     - **What precedes InputSite on Windows, and where Wine's UI thread stops re-arming (2026-11-27; Windows `wt-isite`, `wt-cwp`,
       `wt-nomouse`, `wt-cpaint`, `wt-ord`, `wt-rmx`; lab b37; exported calls only).**
       * Ruled out as the trigger: pointer input (with the pointer parked outside Notepad the whole run there are no mouse messages
         and InputSite still comes, then the TabView templates); the WM_PAINT of the three `Microsoft.UI.Input` child windows
         (class atom 0xc272/0xc047, `WS_CHILD`, `WS_EX_NOREDIRECTIONBITMAP`): it goes to DefWindowProc on Windows too, as in Wine;
         the `MSCTFIME UI` window's timer (InputSite comes before it in other runs); user-mode removal of completions (no thread
         calls `NtRemoveIoCompletion(Ex)` on an Init2 port; the kernel alone turns completions into the (1, 0) wake); the user32
         ordinal wrappers (#2669/#2670/#2582 pass their arguments through to win32u and return what win32u returns; #2670 returns 1
         on Windows as in Wine; the odd r8/r9 values of `#2670(hwnd, 0, ...)` are leftover registers).
       * Over a whole Windows run the UI thread signals the input thread's event only five times before InputSite: the first one
         (start-up) is the one Wine also sends; the later four come from the UI thread's CoreMessaging dispatcher rounds about 6 s
         later, after the UI thread has re-armed its own wake packet.
       * **The UI thread has the same gap as the input thread.** Windows and Wine both run the round that registers a batch of
         seven wait packets on the UI thread's Init2 port. Then Windows: `#2670(hwnd, 0)` -> 1, `NtAssociateWaitCompletionPacket`
         (the UI thread's own wake packet, again), flush -> a work list; from then on every round re-arms it and the rounds go on for
         seconds (about 120 self-posted (6)). Wine: `#2670(hwnd, 0)` -> 1, flush -> empty, no re-arm; its rounds stop about one second
         after start-up (b37: last 0x60 at 609.551) and the thread only pumps window messages from then on. Same calls, same
         results at the export level, different decision inside CoreMessagingXP. Since neither the drain's result nor its buffers
         nor a removal can tell CoreMessagingXP which packet fired, the open candidates are timing and order: when the kernel
         posts the (1, 0) (Wine posts it only when the thread next waits, after everything already queued), and whether the work
         list was filled before the round (Windows: the flush after the re-arm returns a list; Wine: empty).
       * `wakeorder-probe.c` (M1, signal while the thread is busy, M2, then retrieval): a plain window on Windows gets the wake as
         a sent (3, 0) inside `GetQueueStatus`, so a plain window cannot show where the posted (1, 0) of CoreMessagingXP's windows
         lands. What makes the kernel choose (1, 0) for CoreMessagingXP's windows is still not known (`drain3-probe.c` with the
         timer and (5, 5) set-up also gets (3, 0)).
       * Next: find the (1, 0) condition with a probe that copies more of CoreMessagingXP's set-up (the second packet on a waitable
         timer, the private port, `SystemUserAdapterWindowClass`-style second window, `DcppUserAdapterWindowClass` style), then
         measure where the (1, 0) lands; if Windows posts it at completion time, post it from wineserver when the packet completes
         (the queue already knows its port).
     - **InputSite under Wine (2026-11-27, late; lab b41; Wine `claude/inputhost` 08192783 from the InputSite session, diagnostic re-arm on).**
       * With the module and its classes registered by hand in the prefix (ActivatableClassId, both views; `inputhost.dll` has no
         DllRegisterServer and `wineboot -u` hangs with the lab build), Notepad's input thread runs the whole sequence three times as
         on Windows: factory {8ca0442d}, CreateForHost with sinks 0x3b4/0x408/0x454 (the app's host answers {92179cc0}), the site asked
         for {f77d91d0}, ActivationConfigurationInputObject {4c8421ca} CreateForInputSite, put_Setting1(0), put_Setting2(0). No
         unknown interface, no fail-fast.
       * After it (750.0) the CoreMessaging rounds on both threads stop again; no page load (1 clock call, no surfaces). On
         Windows InputHost itself activates `Windows.System.DispatcherQueue` here and creates a `SystemUserAdapterWindowClass`
         window with an Init2 registration on the input thread; the Wine module does not.
       * The rounds are not stuck at the end: by 750.014 both wake packets are re-armed (UI 0x118, input 0x2d8, diagnostic) and both
         threads wait with nothing queued; nobody brings new work. On Windows the UI thread's page load (PropertyValue, Uri, then the
         TabView templates about 4 s later) follows; the composition thread there waits on DCompositionWaitForCompositorClock about
         28 times per run, under Wine once (then it blocks). Next: the compositor clock thread (dcomp: tell Session 0 first).
       * Lab trap: the guest's disk filled up (79 GB); old relay logs b26d..b36 and from.log are now gzipped in place.
     - **The re-arm is the gate, InputSite is next (2026-11-27, night; lab b39, diagnostic only).**
       * Diagnostic (lab tree only, never for a branch; `rearm-diag.py` in the session scratchpad, env `WINE_CM_REARM=1`): ntdll
         records every `NtAssociateWaitCompletionPacket` (packet, port, target, key); win32u remembers the key of the zero-APC
         completion it turned into the (1, 0) wake and, after the window procedure has handled that wake, associates the same
         packet again.
       * b39 with it: the input thread's wake packet (0x2dc) is re-armed 11 times and the UI thread's (0x118) and two batch packets
         too (all status 0); the CoreMessaging rounds go on; at 350164.503 the input thread activates
         `Windows.UI.Internal.Input.InputSite` and 35 ms later fail-fasts (WIL error path) because Wine has no such class.
       * Ruled out on Windows as the source of the re-arm decision or of the (1, 0) form (2026-11-27, night): the extra
         arguments of ordinal 2582 (ignored; always a (0, 0), `sched3-probe.c`); waits or object queries between the drain and the
         re-arm (none on the input thread, `wt-waits`); a re-arm by the kernel (a wait packet on an Init2 port is one-shot on
         Windows too, `drain3-probe.c` mode 0); the window class (`DcppUserAdapterWindowClass` / `SystemUserAdapterWindowClass`
         still get a sent (3, 0)) and the procedure's return value for (3, 0) (`wakeorder-probe.c CLASS RET`). In this run Windows'
         input thread re-armed inside its first (1, 0) handler (drain with buffers, then associate), in others only from a later
         round, so the decision depends on CoreMessagingXP's own state at that moment.
       * So: the missing re-arm is what stops Notepad, and InputSite (being built on `claude/uwp-inputsite`, Windows 10 serves the
         `{8ca0442d}` factory interface that this path asks for) is the next blocker. The proper re-arm mechanism (what makes
         CoreMessagingXP re-arm on Windows) is still unknown; the diagnostic re-arms after every (1, 0), Windows re-arms from the
         batch round on.
     - **The UI thread took the wrong CoreMessaging path (2026-11-27, night; `wt-isgui`, `guithread-probe.c` extended; b38).**
       * Windows: Notepad's UI thread gets `IsGUIThread(FALSE)` = 1 when CoreMessagingXP asks, so it registers its wake packet
         directly on the Init2 port (no private port, no cancel); only the input thread gets 0 and takes the private-port path.
         Wine (b37) answered 0 on the UI thread too: its only user32 call before that is `RegisterClassW`, which did not count.
       * Windows converts a thread on its first win32k call of any kind (probe: RegisterClassW, GetClassInfoExW, LoadCursorW,
         LoadIconW, SetThreadDpiAwarenessContext, CreateCompatibleDC, GetDpiForSystem, SystemParametersInfoW,
         RegisterWindowMessageW, GetKeyState, IsWindow(GetDesktopWindow()), GetStockObject all give 1). Wine branch
         `claude/isguithread-win32k` 46da8d46: `set_gui_thread()` in NtUserRegisterClassExWOW, NtUserGetClassInfoEx and
         alloc_dc_ptr; still not counted (client-side in Wine): RegisterWindowMessageW, GetStockObject, GetDpiForSystem,
         SetThreadDpiAwarenessContext. Lab msg test: x86_64 188 failures (baseline), i386 207 (extra ones in WM_COPYDATA and
         PrintWindow only), none in the IsGUIThread tests.
       * b38 with it: the UI thread's wake packet goes straight to the Init2 port as on Windows. The stall is unchanged: after
         the seven-packet batch round the wake packet is still not re-armed. The UI thread's first (1) wake comes 2.9 s after its
         event was set because the thread is busy creating its Direct3D/OpenGL device under relay (3142 calls), not because a
         wake is lost.
     - **What else the existing runs show; targets ranked (2026-11-27, later; b37 relay; Windows traces of 2026-11-26/27).**
       1. *CoreMessaging re-arm (both threads), primary.* Wine's UI thread wake packet (0x118, event 0xf8) is moved to the Init2
          port once (603.920) and never associated again; its event is still set 7 times afterwards (6 by the UI thread itself,
          1 by the input thread), and each of those signals is lost. Windows: the UI thread's wake event is set 10 times (by the
          input thread and by the UI thread itself from `Microsoft.UI.Input`) and the packet is re-armed each round. The batch of
          seven packets is not what keeps the rounds alive: on Windows six of its targets are never signalled in 30 s and their
          packets are associated once. Difference in placement: Windows registers the batch inside a (6) dispatcher round and
          re-arms right after its drain; Wine registers it inside a (5, 4) handler (which posts the (6) itself) and does not.
       2. *InputSite does not exist in Wine.* No `Windows.UI.Internal.Input.InputSite`, `ActivationConfigurationInputObject` or
          `InputHost` anywhere in the tree. Once the rounds go on, the input thread will ask for them and fail. Factory interfaces
          Notepad asks for on Windows: InputSite {73f80911-7a51-5b0f-906b-f6402ea6c3cb}, ActivationConfigurationInputObject
          {4c8421ca-3863-5236-b09a-757cf68c5fa5}, `Windows.System.DispatcherQueue` {a96d83d7-9371-4517-9245-d0824ac12c74}.
       3. *System CoreUI session is a semi-stub.* FrameworkUdk calls `CoreMessaging.dll!CoreUICreate` on the UI thread (603.347);
          Wine's builtin answers session slots 8, 12, 3 and a generic interface {37a94c05-1aa6-4cea-9049-083bba055cec} slot 5,
          and drops the handler registration of slot 12 (a runtime function and a context). Windows creates a
          `SystemUserAdapterWindowClass` window on the UI thread there (second Init2 registration on the same port), but that
          window gets no dispatch in 30 s, so nothing shows the dropped handler being needed yet. Lower priority.
       4. *Text input*: thread manager now as on Windows (`claude/ctfimm-tim`); `TextInputFramework.dll` absent. Not shown to matter.
       5. *Compositor*: one `DCompositionWaitForCompositorClock` (thread 0184) and six `DCompositionCreateSharedVisualHandle` on
          the UI thread; most likely a consequence of the stopped rounds (no frames requested), to recheck after 1.
     - **When Windows starts a thread's text services, and the Wine side (2026-11-26, night; `tests/uwp/alpc/tsf-probe.c`).**
       * Windows 11 (probe, one new thread per case): `msctf.dll` is loaded with the thread's first window (with its `IME` window).
         The thread manager (`TF_GetThreadMgr` non-NULL), `CicMarshalWndClass`, `OleMainThreadWndClass` (an apartment),
         `MSCTFIME UI` and `TextInputFramework.dll` come with the first **activation** of a window of the thread: `SetForegroundWindow`,
         `SetFocus` (even on a hidden window), `ShowWindow(SW_SHOWDEFAULT)`, `WS_VISIBLE` at creation; not with `SW_SHOWNOACTIVATE`,
         not for a message-only window. `ImmDisableTextFrameService` does not stop it. `CtfImmTIMActivate(hkl, TRUE)` called directly
         gives the thread manager, `CicMarshalWndClass` and the apartment and returns 0; with FALSE nothing happens. The 32-bit export
         takes two stack arguments (`stdcall`, 8 bytes; the probe's i686 build checks the stack).
       * Wine branch `claude/ctfimm-tim` (on development d4f070ee): `imm32.CtfImmTIMActivate(hkl, activate)` implemented (apartment
         through the existing implicit COM initialisation, then msctf's `TF_CreateThreadMgr` + `Activate`, kept per thread and
         released with the implicit apartment) and called from the default IME window's `IME_INTERNAL_ACTIVATE`, which win32u sends
         on activation. The probe now matches Windows in every case for the thread manager. Still different: no
         `CicMarshalWndClass` / `MSCTFIME UI` (Wine's msctf has no such windows) and no `TextInputFramework.dll` (not in Wine).
         Test: `dlls/imm32/tests/imm32.c` `test_CtfImmTIMActivate` (FALSE/TRUE/again; SW_SHOWNOACTIVATE then SetFocus); not run on the
         host because the imm32 test installs a test IME into the system; the probe shows the same on Windows. Wine commit
         15a48281. Lab (u13 tree, copy of the booted `claude/ldr-prefix`): imm32 test x86_64 5586 tests, i386 5589 tests, 0 failures
         both; the 32-bit probe matches Windows (stack balanced, FALSE does nothing, TRUE and SW_SHOWDEFAULT give the manager).
         msctf's `inputprocessor` test times out (300 s) in this lab with and without the change (pre-existing). Review fix (Session 0): the manager now lives with the default IME window, not the implicit STA (Wine 89f15b4d, pin a5f8a63e): Windows 11 creates it in an MTA thread too (probe case I) and keeps it when the thread enters the MTA after activation (case J, CoInitializeEx returns S_OK); lab: imm32 x86_64 5600 / i386 5597 tests, 0 failures, probe I and J match on both. Lab trap: a new
         prefix from the u13 build hangs in `wineboot -u`; copy `claude/ldr-prefix` instead.
       * Notepad with it (b37): the UI thread now has a thread manager (`TF_CreateThreadMgr` at 608.202, before TabData), and nothing
         else moves: same windows, same activations (stops at `UserActivityRequestManager`), no InputSite, 1 clock call, no caret.
         So the thread manager is not the missing piece by itself. Left on the text input side: `TextInputFramework.dll` (Windows
         loads it from msctf on activation; with it comes a `SystemUserAdapterWindowClass`, i.e. the system CoreMessaging instance,
         probe case A) and `InputHost.dll` (InputSite). Wine's prefix has only its builtin `coremessaging` and
         `windows.ui.core.textinput`; neither creates `SystemUserAdapterWindowClass`.
       * Open on the RichEdit side (batch 1145 check): `EM_SETEDITSTYLE(0xe14018)` returns 0xe14018 under Wine and 0xe04018 on
         Windows (bit 0x10000); the native control decides that from its environment. Not caused by the loader rule.
     - The compositor registers an occlusion status event with the dxgi factory. Wine sets it at once and never reports
       occlusion, so that event alone is not what stops Wine's compositor. Whether it checks occlusion some other way is
       open.
     - **Not occlusion; a lost update, and why (2026-11-25).**
       * Windows reproduces the black strips: under wintrace (a slow, debugged start) the tab strip, menu bar and status bar
         stay empty although the window is visible, not cloaked and in the foreground. One ALT key press at 12 s brings
         everything (18 surface BeginDraw and 22 d2d slot 15 calls within 3 s). Started normally with no input, Windows draws
         them by itself. So the strips need a later UI frame that a slow start can lose.
       * Under Wine that later frame never comes because the UI thread stops pumping window messages. On Windows the thread
         that owns Notepad's window and input site waits in `GetMessageW`. Under Wine the same thread only peeks (`get_message`
         with PM_REMOVE, empty) and then blocks in a plain ALPC receive on its CoreMessaging port (`alpc_send_receive`, flags
         0, then an infinite select on the port alone). Window messages to it pile up unread (WM_COMMAND, WM_USER, Wine's
         internal messages, a sent WM_SETCURSOR). Shared-texture coherence (Integration's 230e5f7e) does not change this
         (lab run: 3 surface draws, no slot 15).
       * What Windows does instead (wintrace, `win32u.dll` exports): the UI thread calls
         `NtUserInitThreadCoreMessagingIocp2` twice early on (it returns a handle, 0x3e8) and
         `NtUserDrainThreadCoreMessagingCompletions2` later. A second CoreMessaging thread does the same (init once, then
         drain about every 0.3-0.5 s). `NtUserInitThreadCoreMessagingIocp`, `NtUserDrainThreadCoreMessagingCompletions`,
         `NtUserRegisterCoreMessagingEndPoint`, `NtUserWaitForMsgAndEvent` and `NtUserRealWaitMessageEx` are not called. In
         Wine all four CoreMessaging syscalls (and `NtMinInteropCoreMessagingWithInput`) are `@ stub -syscall`.
       * The first argument of `NtUserInitThreadCoreMessagingIocp2` is a window: a message-only window (parent: the
         message root) of class `DcppUserAdapterWindowClass` (calls from CoreMessagingXP) or `SystemUserAdapterWindowClass`
         (from the system's CoreMessaging.dll), checked live with IsWindow/GetClassName while the trace ran. The call
         returns a small value (0x3e8, 0x8c4: handle-like); the second argument points to a dword that comes back 0 or 1;
         the third is -4 or 0; `Drain...2` takes the same window first. So CoreMessaging makes a "user adapter" window per
         thread and ties its completions to that window's thread with this call.
       * **Under Wine, Notepad never calls the stub** (a `+seh` run has no 80000100 exception, caught or not), not even
         when the prefix reports Windows 11 (build 22000, and a lab-only 26100). The UI thread does create the
         `DcppUserAdapterWindowClass` window, then builds its own replacement: an I/O completion port, an event,
         wait-completion packets on it and a helper thread, plus a timer and a callback message on the adapter window. That
         is CoreMessaging's other path; the gate between the two is not known yet (not the build number, not the stub's
         presence). Implementing the two calls alone will therefore change nothing until that gate is found.
       * The other path, as Wine runs it (+server and a relay run limited to the wait and ALPC functions): CoreMessagingXP
         creates an auto-reset wake event, a completion port with a wait-completion packet on that event, and a helper
         thread. When another thread sets the event (the compositor does, twice, after the text batch), the helper posts
         message 0x60 to the UI thread's adapter window. The UI thread first pumps normally (window procedures run, 0x60
         and WM_TIMER on the adapter window, WM_PAINT), but its last call is an untimed
         `NtAlpcSendWaitReceivePort(port, 0, NULL, NULL, receive buffer, ...)` from CoreMessagingXP. Nothing ends that
         wait, so the posted 0x60 and every later message stay unread. Why it calls that receive there is not known.
       * The WinUI 3 probe for a Windows 10 comparison: a self-contained test app (Windows App SDK 2.7.0, built in scratch,
         not in the repository) behaves like Notepad on the Windows 11 host. `wintrace exe:PATH` starts it. The Windows 10
         lab VM was too slow to start a guest session on 2026-11-25, so the comparison has not run.
       * **The gate is the Windows build number (2026-11-25).** CoreMessagingXP delay-imports three user32 ordinals:
         2582 (`ScheduleDispatchNotification`), 2669 (`InitThreadCoreMessagingIocp2`) and 2670
         (`DrainThreadCoreMessagingCompletions2`); Wine's user32.spec has the last two as stubs. On Windows 11 (26300) its
         order on the UI thread is: IsGUIThread, RegisterClassExW, CreateWindowExW (adapter window), ordinal 2669
         (window, pointer to a dword) which returns a handle to a completion port, SetTimer, DuplicateHandle of its wake
         event, NtCreateWaitCompletionPacket, NtAssociateWaitCompletionPacket(packet, that port, the wake event). So the
         port belongs to the thread's message queue: a completion on it ends GetMessage and ordinal 2670 collects it.
         Under Wine with build 19045 or 22000 it makes no API call between CreateWindowExW and SetTimer and builds its own
         port and helper thread (the Windows 10 path). With `CurrentBuildNumber` 26100 in the prefix (the value ntdll and
         wineboot's shared user data follow; `winecfg /v win11` alone gives 22000) it calls ordinal 2669 at once and Wine
         aborts: "unimplemented function user32.dll.InitThreadCoreMessagingIocp2". Not involved: RtlGetVersion,
         feature configuration, IsApiSetImplemented, QueryOptionalDelayLoadedAPI (none called by CoreMessagingXP before).
         The exact threshold between 22000 and 26100 is not measured.
       * **Implemented (Wine claude/win32u-coremessaging-iocp2, d26397bb; 2026-11-25/26).** Probe: `tests/uwp/alpc/cmq-probe.c`
         (host, black box). Measured on Windows 11: Init2 returns the thread's IoCompletion port (first call creates it, out
         dword 0; later windows get the same handle, out 1; a registered window again returns 0); a packet on it wakes any
         message wait and the wait takes it and calls each registered window with message 0x60, wparam 8 and lparam the APC
         context, or wparam 3 and lparam 0 when the context is zero (key, status, information are not passed); queue status
         bits do not change; PeekMessage, GetQueueStatus, Sleep do not drain; nested waits and the wait for a SendMessage reply
         deliver too; Drain2 delivers one packet, returns 1 (0 without a window) and writes nothing through its pointers.
         **PostMessage of 0x60 (any window, any thread):** the window is notified at once with the original parameters (inside
         the call for the caller's own thread, in the owner's sent-message processing otherwise) and a copy with
         0x80000000 set in wparam is queued; SendMessage and SendNotifyMessage deliver once; 0x5f, 0x61 and 0x401 are normal.
         Also measured with hooks inside the test app (`cmtest/HookLog.cs`): all of CoreMessagingXP's own 0x60 traffic on
         Windows is such posts (wparam 5, 6, 1 with the top bit set in the queued copies); no completion (wparam 3 or 8) shows
         up in an idle app. Injecting a completion into CoreMessagingXP's real port (key 0x77, any APC context) from inside
         the app **kills it with the same fail-fast (0xE0464645)**: its handler checks its own state.
       * **Still open: Notepad with build 26100 fail-fasts in that handler.** With the prefix reporting 26100 CoreMessagingXP
         now gets through Init2, arms its wait packets and posts its messages, but its window procedure raises the fail-fast
         (CoreMessagingXP offset 0x6279, no API call before it) on the first PostMessageW(hwnd, 0x60, 5, 4) it makes on the UI
         thread, i.e. on the immediate notification (wparam 5, lparam 4). Not the order (queue first or notify first), not the
         wparam of the completion messages. What state that handler checks is not known. Ideas: the order of the setup
         (the `SendMessageCallbackW(hwnd, 0x60, 5, 5, NULL, 1)` handler, whose reply or side effects Windows may treat
         differently), whether Windows runs the immediate notification in a different context (user mode, after the call),
         or a 0x60 message sent or posted from another CoreMessaging thread before it. Compare the calls of CoreMessagingXP
         around its first PostMessageW(0x60) on Windows (wintrace of `tests/uwp/alpc/cmtest` with labels on PostMessageW, SetTimer,
         NtAssociateWaitCompletionPacket and Init2/Drain2, in order) with a Wine run that has +relay limited to calls from
         CoreMessagingXP (RelayFromInclude), reporting build 26100 through the registry (`CurrentBuildNumber`).
       * Two ways on from here (older note): implement ordinals 2669/2670 (win32u `NtUserInitThreadCoreMessagingIocp2` /
         `NtUserDrainThreadCoreMessagingCompletions2`, a queue-owned completion port in the server) and report a build
         that uses them; or find why the Windows 10 path leaves the UI thread in an untimed ALPC receive under Wine.
       * Older note: (a) find what that other path expects to wake the UI thread (it ends blocked in the ALPC receive instead of
         a message wait: compare the helper thread's work on Wine with what a Windows 10 system does, if a Windows 10 lab
         is available), or (b) find the gate (a capability probe CoreMessagingXP makes before the adapter window's Init2
         on Windows). Probe scripts: `wt-iocp*.ps1`, `hwnd-check.ps1` (wintrace with
         `win32u.dll!NtUserInitThreadCoreMessagingIocp2:...:R`; no `:R` on hot exports such as `NtAlpcSendWaitReceivePort`,
         it crashes Notepad).
     - Inside Windows' dcomp, `CreateSurface` asks d3d11's private interface (slot 12, width/height/format) for the texture and
       d3d11 calls `DCompositionCreateSurfaceHandle`. That is internal to the system dcomp; Wine's dcomp makes its textures
       itself.
   * Ruled out on the way (2026-11-25):
     - d2d context private slot 9 (measured, `tests/uwp/alpc/d2d-ctx-slot9-probe.cpp`): (bitmap, rect, NULL) limits later draws
       on the bitmap to the rect; (bitmap, NULL, pointer) closes it (later draws fail with D2DERR_WRONG_STATE); the dword is
       never written. dcompi calls it on the UI thread around each glyph-atlas cell. Wine's stub only lets a draw spill past
       its cell. Worth implementing, but it is not the gate.
     - Shared NT-handle textures (Wine fork code in dxgi `resource.c` / d3d11 `OpenSharedResource1`) accept only 4-byte
       formats, and the UI thread's A8_UNORM glyph atlas is rejected. Allowing 1-byte formats (lab-only experiment) shares it,
       but the compositor's behaviour does not change.
     - Refused interfaces on the compositor thread (ID3D11Resource and ID3D11Texture2D on subresource surfaces, `{790a45f7}`,
       `{f13ebcd1}`) are refused on Windows too. The `NtGdiDdDDIQueryAdapterInfo` calls come from dxgi's
       `CheckInterfaceSupport`.
     - wintrace gained `dcomp.dll!surface[N]` and `vsurface[N]` targets.
   * (Earlier note, superseded:) After its draws the compositor thread still goes idle and never calls `Commit` (on Windows it
     commits about every 2 s, from the lifted compositor itself, after private method 46). Wine's composition surface is asked for
     `{790a45f7-0d42-4876-983a-0a55cfe6f4aa}` three times: that is the public `IDXGISwapChain1`, and Windows 11 refuses it on
     both surface kinds too (`tests/uwp/alpc/dcomp-surface-qi-probe.cpp`), so it is a type check, not the cause. The probe
     found `IDCompositionDevice::CreateSurface` stubbed in Wine (fixed in Wine 2c72ac3a). The last run also ended early (2176
     log lines, no screenshot), to be looked at.

## Portal design for sign-off (review of Wine 7f2fe720, 2026-11-24)

The portal (Wine branch `claude/notepad-commit`, 7f2fe720 + test cedb07b0) is held back. Review findings:
- **P1, use after free:** the viewer reads the portal device's committed content pointers after leaving that device's lock.
- **P2, lock cycle:** two devices that each show a portal of the other deadlock.
- **P3, cost:** NtCompareObjects runs for each pair on every commit.
- **P4, missed redraw:** removing a portal does not redraw its viewers.

The GO parts of the series are on `claude/notepad-commit-go`. The proposed replacement, a published immutable snapshot
per shared object that viewers read without entering another device's lock, is written up in
[PORTAL_SNAPSHOT_DESIGN.md](PORTAL_SNAPSHOT_DESIGN.md). Sign-off: the Session 0 reviewer reviews it and Session 0 decides
(relayed by Session 0 as the owner's rule while the Integration session is not running).

## Branches and commits (local state of 2026-11-24)

Wine worktree `D:\DeepThought\Projects\wine-uwp`, branch `claude/notepad-text` (from 035c3f1e, the 11.29 batch Wine head):

| Commit | Change |
|---|---|
| 769c742f | kernelbase: FindResource falls back to the MUI satellite of a language-neutral module |
| f9d6992b | d3d11: GetCreationFlags; private compositor interface `{26c5dc23-...}` (x64) |
| 6c858440 | uxtheme: GetThemeAnimationProperty / Transform / TimingFunction |
| fd3073ea | dwrite: per-range axis values, AddMappings, MapCharacters with axes, private analyzer interface (x64) |
| d3c68238 | dwrite: slant of 10 degrees or more counts as oblique (DWriteCore spec) |
| baf1ba39 | d2d1: private device-context interface `{2ea67ed7-...}` (x64) |
| 2c99cebe | d2d1: CombineWithGeometry (new `combine.c`) |
| bf95d1e0 | dxgi: EnqueueSetEvent |
| 7222bb3d | dcomp: visual slot 35, desktop method 45 (x64) |
| b7d45af7 | dxgi: occlusion status events |
| 5d85732a | win32u: KMTQAITYPE_ADAPTERTYPE |
| 538ed9bc | d2d1: factory private slot 15 creates a device |
| aea4289c | win32u: NtUserEnableMouseInPointerForThread |
| 8f66f34e | msctf: RegisterHandwritingInputRoutingCallback (x64) |
| 8eccd313 | dcomp: DCompositionWaitForCompositorClock, GetFrameId, BoostCompositorClock |
| d93082c0 | d2d1: private device interface `{46fdbfea-...}` (x64) |
| 28805e76 | include: D3D_RETURN_PARAMETER_INDEX |
| 0ab90a38 | d3dcompiler: shader libraries, function linking graphs and the linker (new `library.c`) |
| 01af4686 | d2d1: surfaces made by CreateSubresourceSurface (test `test_subresource_surface`) |

WineExplorer worktree `D:\DeepThought\Projects\WineExplorer-uwp`, branch `claude/uwp-notepad-text` (from 37af31f7): probes, wintrace,
the lab helpers below and these docs.

## Lab configuration

* **Guest:** the owned U1 guest (see [RESUME.md](RESUME.md), "The owned guest"), workspace `/home/debian/uwp-u1-20261111`,
  helpers and scratch files in its `claude/` directory, X display `:99`.
* **Candidate:** `build/candidates/u13` (source `source/`, build `build/`, WoW64). Its base is **older** than `claude/notepad-text`:
  never copy whole files blindly. Push the changed file as `dc-<path with / as _>`, run `candidate-diff.sh <path>`; when the count
  is the size of your own change, copy it in (`tr -d '\r' < claude/dc-... > source/<path>`), else write a small Python patch script.
  Build single modules (`make -j6 dlls/<dll>/all dlls/<dll>/tests/all`). The candidate now carries all commits above applied this
  way, plus the uncommitted d2d1 change.
* **Packages:** `claude/wasdk` (Notepad 11.2609, Windows App Runtime 2.7.0) installed by `wasdk-run.sh` into a fresh prefix
  each run (`wasdk-run/prefix`, several minutes); `claude/applaunch.exe` starts the app.
* **Runs:** `notepad-run.sh riched20 '<WINEDEBUG>'` (log `wasdk-run/ovr.log`, screenshot `ovr.png` at 25 s);
  `notepad-bypass-run.sh '<WINEDEBUG>' LABEL` for runs past the input thread's fail-fast (the lab-only `fastfail-bypass.py`
  patches ntdll, the script restores and rebuilds it and checks no bypass line is left). Wine tests run in the Notepad prefix
  (`WINEPREFIX=$C/wasdk-run/prefix`, initialised), not in a fresh one (a fresh prefix without wineboot fails WIC tests).
* **Leftover processes:** about 90 `winedevice.exe` processes of earlier runs' prefixes (up to two days old) were still alive in
  the guest on 2026-11-24 (`ps -eo pid,etime,args | grep winedevice`); they hold memory. Kill the old ones before long series of
  runs (they belong to deleted prefixes; check `etime`).
* **The prefix update dialog:** rebuilding ntdll makes the next prefix creation show the wine-mono dialog; harmless for Notepad
  runs (it appears during setup), but screenshots taken by a timer started before the Notepad launch catch it.

## Tooling (host side)

* `tests/uwp/lab-helpers/lab.sh`: `push NAME` copies a scratchpad file into the guest's `claude/`; `wait 'command' SECONDS` runs a
  command without the 55 s limit. **Quoting trap:** a command with `$`, nested quotes or `;` inside `wait '...'` breaks (timeout
  reads part of it as its interval); write a script, push it, and `wait 'bash .../script.sh'`. Binary files come back with
  `wait 'gzip -c FILE | base64 -w0'` and a decode on the host.
* `tests/uwp/alpc/wintrace.c` (built with mingw in WSL, `x86_64-w64-mingw32-gcc -O2 -o wintrace.exe wintrace.c -lole32 -luuid`):
  * targets: exports, ordinals, `dcomp.dll!desktop[N]`/`visual[N]`, `winrt!Class@{IID}[N]`, `d3d11.dll!device@{IID}[N]`,
    `dwrite.dll!analyzer@{IID}[N]`, `d2d1.dll!context@{IID}[N]`, `d2d1.dll!d2ddevice@{IID}[N]`, `d2d1.dll!factory[mt]@{IID}[N]`,
    `d3dcompiler_47.dll!flg|linker|module|instance[N]`;
  * label suffixes: `_D` (32 qwords at rcx), `_E` (rdx/r8 memory), `_F` (floats), `_H` (rcx as a WinRT string, GUID at rdx),
    `_S` (rdx/r8/r9 as C strings), `_P` (rdx: r8 D3D11_PARAMETER_DESC); a fourth field `R` logs the return;
  * **`WINTRACE_WATCH=Notepad.exe` is required**: Notepad 11.2609 starts through a broker that relaunches itself with
    `/SESSION:`; without it the trace follows the broker that exits after ~200 ms;
  * the window does not appear in every run; check the `load` lines (InputHost, TextInputFramework) before concluding that
    Windows "never" does something; heavy `R` breakpoints on hot exports (RoGetActivationFactory) stall Notepad at ~33 hits.
* Probes (`tests/uwp/alpc/`): `d2d-qi-probe.c` (private d2d interfaces, slot 5 of `{46fdbfea}`), `flg-probe.cpp` (the shader
  linker on a library of our own; `flg-probe.exe OUTDIR [PREBUILT_DIR]` also runs under Wine with libraries compiled on Windows;
  build with Wine's `d3d11shader.h` first on the include path), `subsurface-probe.c` (CreateSubresourceSurface and Direct2D),
  `hlsl-lib.c` (compiles our own HLSL library to a C array for Wine tests).
* Wine tests that must hold on Windows too: build the test in the lab (`dlls/<dll>/tests/x86_64-windows/<dll>_test.exe`), bring it to
  the host (gzip + base64) and run it there against the system DLLs; that is how the linker's sticky-error and single-link rules
  were found.

## Open work, in order

1. Done (2026-11-24): `{790a45f7-...}` is `IDXGISwapChain1`, refused on Windows too; not the cause.
2. Done (2026-11-24): it commits (see above). Next: why the compositor's surfaces are black on screen (format R8G8B8A8
   premultiplied; d2d draws through slot 7 onto the subresource surface's bitmap; Wine's dcomp composes at Commit).
   Earlier plan, kept for the record: trace the compositor thread after its draws with `+d3d11,+d2d,+dxgi,+dcomp,+sync` (the bypass run)
   and compare with a Windows trace at `dcomp.dll!desktop[3]` (Commit), `[45]`, `[46]`. On Windows the order is 46, Commit, 45.
3. When it commits: Wine's dcomp composes a target's surface layers through an auxiliary producer per target and refuses several
   cases with E_NOTIMPL (formats other than B8G8R8A8/R8G8B8A8, two targets on one window, content of two devices in one scene,
   shared visuals opened through `OpenSharedObject`). Expect one of those next.
4. InputSite (wall 1 above).
5. Before review of the Wine branch: STOCK_CHANGES entries (added for all commits up to 0ab90a38), measure the guessed error codes
   (dwrite layout getters, dxgi SetEvent), 32-bit measurements of the private slots, linker gaps (two functions with immediate
   constant buffers, RDEF in linked shaders, AddClipPlaneFromCBuffer, D3DReflectLibrary).

## 2026-11-25: what moved the failure (Wine branch c3f1f89f, 440d9d4f)

Measured on Windows 11 (tests/uwp/alpc/sched2-probe.c, smcb2-probe.c, order-probe.c, as-probe.c, cinfo-probe.c):
- user32 ordinal 2582 (ScheduleDispatchNotification) was a stub that sent nothing. It queues a *sent* message 0x60, wparam 0,
  lparam 0 (QS_SENDMESSAGE until the next message retrieval, InSendMessageEx 0).
- PostMessage(0x60, 5, 4) does not call the window procedure inside the call. The notification is queued as a sent message and
  delivered at the next retrieval, in order behind the sent messages already waiting (the deferred SendMessageCallback(5,5) that
  CoreMessagingXP issues at startup arrives first), then the posted copy (0x80000005), then the (0,0) of ordinal 2582.
  Wine called the notification at once, so the handler saw (5,4) before (5,5) and fail-fasted.
- A same-thread SendMessageCallback with a NULL callback and data 1 is delivered at the next PeekMessage, whatever the filter.
- Wait packets whose target is already signaled complete at once on both systems (AlreadySignaled 1, auto-reset events consumed,
  (3,0) delivered): identical on Wine.
- Init2 changes nothing in the thread's Win32ClientInfo / Win32ThreadInfo.
With the new ordering the UI thread no longer fails. Open: the worker thread (adapter window 0x3006e) fail-fasts on a wparam-3
completion that arrives after it handled the (5,5), drained (Drain2 returns 1) and waited. Not yet different from Windows: the
order of (0,0) relative to the posted copy (Windows: after), the InSendMessageEx value of the callback message (Windows 2, Wine 4).

## 2026-11-25: Time Travel Debugging of Windows Notepad (tests/uwp/alpc/ttd)

How: `ttd-notepad2.ps1` (elevated, tttracer monitor mode) records Notepad from process creation; `run-cdb.ps1` runs WinDbg's cdb on the .run with a command
file (`dx @$cursession.TTD.Calls("ntdll!Nt...")` gives every call with arguments, return value, thread and position; `!tt <pos>` + `dq @rsp+28 L4` gives
the stack arguments). API-boundary level only, no disassembly. The traces live outside the repository (D:\DeepThought\ttd-notepad2).

**Rule (owner, 2026-11-26).** TTD recordings are allowed with queries of exported Windows API calls only (arguments, results, threads, order; the stack arguments of a call, read at the call). No internal function names from Microsoft's symbols (say 	he application's own fail-fast, 	he adapter window's procedure), no stepping, no disassembly, no memory reads beyond a call's arguments. Traces stay outside the repository. The recordings made before this ruling were analysed beyond it and have been deleted; the posted-wake finding was re-derived from a fresh recording under the rule (below).

Findings (Windows 11, Notepad 11.2609 with Windows App Runtime 2.7):
- Three threads call Init2: the UI thread (two windows: the app's and the system CoreMessaging.dll's), a CoreMessagingXP worker (also two windows) and a
  system CoreMessaging.dll thread (one window). Wake packets all have APC context 0 (stack argument 5), so they arrive as (3,0).
- A CoreMessagingXP worker thread starts with: auto-reset event, manual-reset event, thread-pool wait on it (TpAllocWait/TpSetWait, packet associated
  with the process thread-pool port), manual event, DuplicateHandle(current thread), thread-pool wait on that, then **NtCreateIoCompletion (a private
  port), NtCreateWaitCompletionPacket, NtAssociateWaitCompletionPacket(packet, private port, first event, key 0x40000002)**, then the window, Init2,
  SetTimer(hwnd,1,0x7fffffff), DuplicateHandle(first event), SendMessageCallbackW(hwnd,0x60,5,5), **NtCancelWaitCompletionPacket(packet, FALSE)**,
  **NtAssociateWaitCompletionPacket(same packet, Init2 port, first event, key = heap pointer)**, a second packet created, then MsgWait; the (5,5)
  handler associates the second packet (second event) with the Init2 port and calls Drain2(hwnd,NULL,NULL,NULL) once.
- The wake: another thread (the UI thread) calls SetEvent(first event); the worker's handler then calls Drain2(hwnd, ptr, ptr, ptr) with real buffers,
  re-associates the first packet, posts (5,4), calls ordinal 2582, SetTimer(hwnd,1,0) and SetTimer(hwnd,1,0x7fffffff), Drain2 again, MsgWait.
- Wine's worker (the failing one) does none of the private-port part: it creates a new packet after the (5,5) callback instead of cancelling and
  re-associating the first one, and the private-port block runs on separate threads that have no window (Windows: the same thread that owns the window).
  Wine's UI thread also has one window where Windows has two (the system CoreMessaging.dll instance is missing under Wine). Open: why the thread start
  code takes that path under Wine (the decision is made before the window exists), and whether the missing system instance matters to the handler.

## 2026-11-26: the worker no longer fail-fasts (Wine branch 4a0ecd9f)

- The thread that fails is Windows App SDK's input thread (it retrieves messages with PeekMessage and waits with MsgWaitForMultipleObjectsEx); the abort is the application's own fail-fast.
- **Basis (fresh recording under the rule, exported calls only):** on the input thread (the one with the adapter window; recording of 2026-11-26) the blocking MsgWaitForMultipleObjectsEx(1 handle, INFINITE, 0x1cff) returns 1 when another thread signals the thread's event, the next PeekMessage returns 1, and DispatchMessageW is called with MSG {hwnd = the adapter window, 0x60, wParam 0x80000001, lParam 0}, then {0x60, 0x80000005, lParam 4} (the posted copy of the (5,4) notification), at each of the three wakes of the first seconds (positions 2B227, 2DDA2, 2E360 of the trace; trace outside the repository). So a completion arrives as a *posted* 0x60 with wParam 1 (the 0x80000000 bit is the one removal adds, as measured with PostMessage), the wait ends with 1 and not with a timeout. A (3,0) call inside the wait would not show in these calls; the wait result shows it did not end the wait the way the test windows' in-wait delivery does (those leave a timeout result). On Windows a wait packet completion reaches CoreMessagingXP's windows as a posted (0x60, 1, 0): the thread's MsgWait returns as for input, the retrieval of the
  posted message calls the procedure with (1, 0) and returns the message with 0x80000001. Over a whole Notepad run no (3,0) or (8,x) ever reaches those windows.
  Plain test windows get (3,0) inside the wait for the same packets (not understood). Implemented: a zero-APC completion posts (0x60,1,0) to the first window.
- Result under Wine at build 26100: no fail-fast, the UI thread settles in its message loop, the workers cycle through Drain2; the window still shows black
  strips: the composition layer sees 2 commits and no surfaces (Windows: 94 commits, 5 DCompositionCreateSurfaceHandle, 28 compositor clock waits).
- Next: the compositor thread (the thread of the composition library that waits for the compositor clock). On Windows after DCompositionWaitForCompositorClock(0, NULL, 80) it polls
  a one-handle NtWaitForMultipleObjects with a zero timeout and loops; under Wine the same thread blocks in an infinite one-handle wait and the loop stops.
  Compare the two threads' startup (Windows: thread 0x8278 in the TTD trace, a long sequence of event creations, sections and CreateDevice2).
- Other apps tried: Get Help, Clock, Paint (managed or UWP, they fail before CoreMessagingXP).

## 2026-11-27: after InputSite, InputHost's system DispatcherQueue (Wine branch claude/coremessaging-dq 9464442f)

- The compositor clock is not the gate. On the Windows 11 host under the tracer, Notepad calls DCompositionWaitForCompositorClock once in 25 s
  and its page still loads (Integration confirmed it isn't changing dlls/dcomp).
- What Windows does between InputSite and the page load (trace wt-nomouse.txt, exported calls; UI thread 52708, input thread 40340):
  - 21938: the input thread activates InputSite (called from the system FrameworkUdk).
  - For each of the three sites, InputHost.dll calls back into FrameworkUdk, which activates ActivationConfigurationInputObject. Then
    **InputHost.dll itself activates `Windows.System.DispatcherQueue`** (IDispatcherQueueStatics {a96d83d7}).
  - From 23016: the system CoreMessaging window on the input thread (SystemUserAdapterWindowClass) dispatches about 40 posted 0x60 messages
    (wParam 0x80000005, lParam 4). CoreMessaging.dll sets an event every ~31 ms.
  - 22250 the UI thread activates PropertyValue/Uri, 25454 Microsoft.ui.xaml sets a CoreMessaging event, 26375 the TabView templates load.
- Wine b41 (u13, inputhost module, WINE_CM_REARM=1): the sites and configuration objects are created as on Windows. No
  `Windows.System.DispatcherQueue` activation follows: Wine's coremessaging didn't have the class. Then both threads go idle.
- Built: Windows.System.DispatcherQueue in Wine's coremessaging (claude/coremessaging-dq 9464442f; tests pass on Wine and the Windows 11
  host; applied in the lab's u13 tree). **Not needed by Notepad**: the InputSite session measured that InputHost's GetForCurrentThread
  returns S_OK and NULL, and nothing calls CreateDispatcherQueueController. Its _O/_I traces show InputHost makes no call on Notepad's
  host objects after CreateForHost and posts nothing to the caller. Session 0 took the branch back out of batch 1148 (no consumer); it
  stays pushed, for upstream.
- The stall is on the UI thread, not in InputHost or the text box:
  - Notepad's text thread (Windows 58832, Wine 019c) is the same in both: NotepadTextBox, RichEditD2DPT (2.4-2.7 s with OLE and TSF),
    the same RichEdit messages, Notepad.exe's SetEvent, ShowWindow, GetMessage.
  - Windows' UI thread before PropertyValue (22250): its own CoreMessagingXP rounds (posted (6,0) at 21954), TSF's WM_TIMER on its
    MSCTF window (22000), two packet re-arms, then a Microsoft.ui.xaml SetEvent(0x4f8) at 22094 (same call site as at 6141, with
    user32 on the stack then).
  - Wine's UI thread runs the same kinds of rounds (six (6,0)) but never reaches that xaml SetEvent or PropertyValue/Uri.
- Focused Windows trace (wt-ui.txt, 120 s, 14 labels incl. waits, GetMessage, posts, sends, SetTimer; with that many labels Notepad
  starts about 2x slower: InputSite 41938, Uri 42688, TabViewListView 54938). UI thread 37252:
  - GetMessageW (41922) returns no dispatched message. Inside it CoreMessagingXP's window gets the sent (0x60,0,0), the
    ScheduleDispatchNotification form. The handler calls SetTimer(id 1, 0x7fffffff), re-arms two packets, posts (6,0), and then runs
    **Microsoft.ui.xaml work in the same handler**: SetEvent(0x46c) at 42157, PropertyValue 42453, Uri 42688.
  - Wine's (0,0) handlers have the same shape (SetTimer, ResetEvent, post (6,0), SetEvent to the input thread, re-arm of 7 packets,
    Drain2) but never run XAML work: the UI dispatcher has nothing ready, or doesn't see it.
  - SetTimer(id 1, 0) / (id 1, 0x7fffffff) pairs are the same on both (the due-now timer is cancelled within the round; no WM_TIMER
    reaches those windows on either). Windows keeps running rounds (297 SetTimer calls on the UI window in 120 s); Wine's stop at 750.014.
- Next: find who queues that XAML work item on the UI dispatcher on Windows. Microsoft.UI.Dispatching.DispatcherQueue is activated on
  the UI thread; follow its objects with _O (TryEnqueue / TryEnqueueWithPriority callers, thread, priority) and the timer object if any.
  Then compare with what Wine's run queues (the handler Invoke or the CoreMessagingXP item).
- Lab trap: after a rebuild that regenerates makefiles, a copied ldr-prefix runs `wineboot -u` and hangs with u13. Write `disable` into the
  copy's `.update-timestamp` (scratchpad dq-build.sh).

## 2026-11-27: the page load waits for a DispatcherQueueTimer tick

- The DispatcherQueue work stays in the code, switched off (owner, 2026-11-26): `WINE_EXPERIMENTAL=dispatcherqueue` (shared parser
  `include/wine/experimental.h`), Wine claude/coremessaging-dq d840cb8f, batch 1148. Convention in docs/STOCK_CHANGES.md.
- Who queues the work (Windows 11 host, wintrace with _O following CoreMessagingXP's factories down to the queue and its timer,
  wt-dq.txt):
  - Microsoft.ui.xaml calls `DispatcherQueue.CreateTimer` on the UI queue early. It sets Interval 0 and IsRepeating false, and adds a
    Tick handler.
  - A XAML background thread calls the timer's Start: at 6859, then at 64406, right after InputSite. About 100–200 ms later the UI
    thread runs Microsoft.ui.xaml's PropertyValue/Uri work (7047, and the page load at 64500).
  - The input stack's TryEnqueue calls (Microsoft.InputStateManager via Microsoft.UI.Input) are unrelated.
- Start is an NT timer: `NtSetTimerEx(timer, TimerSetCoalescableTimer, due 0, 0x30)`. CoreMessagingXP imports SetWaitableTimer and
  CreateWaitableTimerExW.
  - The UI dispatcher associates a wait packet with the timer.
  - After a wake, it parks the timer with due `0x8000000000000001` (which also clears the signal) and runs the Tick handlers.
  - Windows sequence before the page load (wt-disp.txt): (6,0) dispatched, then re-arm of the timer packet, Drain2, post (6,0),
    park, PropertyValue/Uri. The tick is found in the round where the packet is re-armed on the already signaled timer.
- Wine b41/b43: the background thread 0178 calls `SetWaitableTimer(0x120)` at 737.207 (Start #1), then blocks forever in
  `WaitForMultipleObjectsEx`. The UI thread arms the timer's packet only at 749.692, because it ran no rounds between 737.06 and 747
  (relay cost). It never parks the timer: the tick never runs, so Start #2 (the page load) never comes.
- Wine server bug found and fixed: Wine claude/waitpkt-rearm 51bb1a35.
  - The bug: a wait packet whose message went to a thread already waiting in NtRemoveIoCompletion stayed marked as queued, so
    re-associating it failed with STATUS_INVALID_PARAMETER_1.
  - Probe waitpkt-timer.c: Wine now matches Windows line for line. New test test_rearm_wait_completion_packet: Wine x86_64/i386 and
    Windows 11 pass.
  - Not enough for Notepad on its own (b42 without WINE_CM_REARM: rounds stop as before; b43 with it: same as b41).
- How Windows reports the packets (probes drain4/drain5, plain windows registered through ordinal 2669):
  - The (3,0) wake and Drain2 write nothing through the key or through the drain's pointer arguments.
  - The UI thread never calls NtRemoveIoCompletionEx; only thread-pool threads do.
  - Drain2 delivers every queued zero-APC packet **while it runs**, as a sent (0x60, 3, 0) per packet. A packet with an APC context goes
    as (8, apc) to every window, one per call, and Windows then leaves a packet with key 0 and APC context 0xffffffff80000001 on the
    port.
  - The same happens when Drain2 is called from main, from inside a sent message, or from inside a posted message. Wine's Drain2 took
    one packet and posted (1,0) instead.
- Tried: nested drain in Wine (local branch claude/cm-drain2-nested d9e75f27, not pushed). The probe and the user32 core messaging tests
  then match Windows, but Notepad's UI thread fail-fasts at once on the nested (3,0) (b44). As noted on 2026-11-26, Windows App SDK
  windows never see (3,0) on Windows, but plain windows do. **Open: what makes Windows treat CoreMessagingXP's windows differently**
  (window class or styles, registration arguments: CoreMessagingXP calls 2669 with r8 0x7ffffffffffffffc and r9 0x41, the (5,5) callback,
  ...), and what such a window gets inside Drain2.
- Next: give the probe window CoreMessagingXP's properties one at a time (class name and styles from a CreateWindowExW/RegisterClassExW
  trace; 2669's third and fourth arguments) until Windows stops sending it (3,0). Then implement the CoreMessagingXP branch of Drain2.
