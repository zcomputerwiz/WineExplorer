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
     surfaces (title/tab strip 1428x33, status bar) are black — the next problem.
   * (Earlier note, superseded:) After its draws the compositor thread still goes idle and never calls `Commit` (on Windows it
     commits about every 2 s, from `dwmcorei.dll`, after private method 46). Wine's composition surface is asked for
     `{790a45f7-0d42-4876-983a-0a55cfe6f4aa}` three times: that is the public `IDXGISwapChain1`, and Windows 11 refuses it on
     both surface kinds too (`tests/uwp/alpc/dcomp-surface-qi-probe.cpp`), so it is a type check, not the cause. The probe
     found `IDCompositionDevice::CreateSurface` stubbed in Wine (fixed in Wine 2c72ac3a). The last run also ended early (2176
     log lines, no screenshot), to be looked at.

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
