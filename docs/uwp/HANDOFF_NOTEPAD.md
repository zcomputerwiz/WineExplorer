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
       * Two ways on from here: implement ordinals 2669/2670 (win32u `NtUserInitThreadCoreMessagingIocp2` /
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
