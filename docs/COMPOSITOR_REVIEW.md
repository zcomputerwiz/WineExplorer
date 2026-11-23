<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->

# Compositor, DComp and DWM: design review (2026-11-23)

A review of the composition work for duplication, complexity and design: DirectComposition and the DXGI/WineD3D layer
path, the DWM broker chain, the shell's X11/Wayland backends (wexhost) and the Wayland compositor (wexcomp). Static
review of the code at Wine `97ee37a3`, wexcomp `1f8e5d3a` and this repository's `src/`; the three bugs marked **fixed**
were built and tested, nothing else here was run. Line numbers are those of that code.

DirectDraw has no project changes: the five `dlls/ddraw` commits since wine-11.19 are upstream Wine's.

Overlap with the UWP session (asked 2026-11-23): its only change here is `DCompositionCreateSharedVisualHandle`, a
logging stub appended to `dlls/dcomp/device.c` as export ordinal 1040 (Wine `claude/staterepository-11.22`, `acd1d672`).
Any reshaping of dcomp keeps a named export for ordinal 1040 and leaves that appended function alone. The next step
there, Wine's dcomp accepting the Windows App Runtime's shared visuals (WinUI hosting an HWND), has no owner yet; the
UWP session expects to take it after its package-manager work (`docs/uwp/WINAPPSDK.md` on its branch).

## Bugs

| # | Where | What | State |
| --- | --- | --- | --- |
| 1 | `src/dlls/wexshell/dwm_bridge.c` `retry_pending` | Since identities replaced the scale comparison, a Wayland scale change no longer re-applied effects (corner radius and blur region stayed in the old surface units). | **fixed**: `WEX_WM_IDENTITY` carries the scale; a completed window whose identity or scale changes is re-applied |
| 2 | same | A window without an identity got its full retry budget back on every pass, so it was retried every 50 ms forever and the bridge never went back to waiting without a timeout. | **fixed**: only completed windows are re-armed; a waiting window keeps the retries it has left |
| 3 | same, `apply_change` | The table of windows with effects had 64 slots, and applied windows stay in it until destroyed: the 65th window's effects were silently dropped. | **fixed**: the table grows |
| 4 | `wine/dlls/dcomp/device.c` commit | Stack array overflow with more than 32 targets holding an auxiliary producer (failed commits keep theirs). | **fixed**, Wine `df606ddd` |
| 5 | `dlls/dwmapi/dwm_client.c:118-289`, `uxtheme/window.c:598` | Every caption paint makes several synchronous broker calls under one process-wide lock with a 2 s timeout; with `DwmService=0` each call still costs three server requests whenever the 50 ms failure cache runs out. | open: skip the broker when the gate is off; answer from the process table first |
| 6 | `dwmapi_main.c:103-123` | After a broker restart a process's attributes come back only when that process reads them again; `cached_epoch` is stored, never used. | open: re-send the process table when the epoch changes |
| 7 | `dwmapi_main.c:440-491` | `DwmSetWindowAttribute` runs side effects (`set_glass`) before writing the process table, so they see the old value; on a full table the broker keeps a value the call reported as failed. | open |
| 8 | `dwm_bridge.c:180-201` vs `dwmapi_main.c:201` | The materials gate differs: the bridge also honours high contrast and `EnableTransparency=0`, dwmapi does not, so a window can be made transparent with no backdrop drawn behind it. | open |
| 9 | `src/programs/dwm/dwm.c:133` | Snapshot paging uses an array index while `sweep_attrs` reorders the array: entries can be skipped or repeated. | open (goes away with refactor W4) |
| 10 | `src/dlls/wexhost/x11wm.c:955-960`; `waylandwm.c:470-487` | `end_call` writes the wake pipe after unlocking while stop closes it; `wayland_start` sets `running/manager/display` without `wl_mutex`. | open |
| 11 | `dcomp/device.c:916-967` | `dcomp_surface_resize` swaps `physical_surface` outside the lock while the committing thread reads it. | open |
| 12 | `wexcomp`: `seat.c:423`, `xx-zones.c:27`, `wex-window.c:381,1365,2144` | Every session advertises pointer and keyboard (headless backend always present); zone handles fall back to `rand()`; unchecked `wl_resource_create`; NULL dereference in `wex_window_update_effects(NULL)`; unbounded stack array of thumbnail layouts. | open |
| 13 | `programs/explorer/dwmhost.c:162` | `stop_dwm_broker` closes the supervisor thread handle after a timeout although the thread may still run. | open |

## Dead code

- **dcomp/DXGI, the old window-message target path (~200 lines).** `dcomp_bind_content` is only ever called with no
  target, and nothing sends `WM_WINE_DCOMP_SET_TARGET` (its one sender reads `last_comp_wnd`, which is never set).
  Removable: `set_target` in `winedxgi.idl`, `dcomp_binding_set_target`, `dcomp_layer_compute_placement`,
  `dcomp_swapchain_apply_target` and the message case in `dxgi/factory.c`, the placement fields in `dxgi_private.h`,
  the `LEGACY/PARAMS/PARAMS_OWNED` modes, `WM_WINE_DCOMP_SET_CHILD_MODE`, `WM_WINE_DCOMP_PRESENT_FLUSH`,
  `WINE_DCOMP_TARGET_SCENE_PROP` and the write-only `WINE_DCOMP_WND_PROP_FMT`. Then check whether WineD3D's
  non-scene `PRESENT_CLEAR` branches are still reachable. Also unused: `offset_x/offset_y/has_transform` of the layer
  description, the `opacity` parameter of `dcomp_collect_*`, and most of the visual's `active` state (only
  `active.content` and the active child links are read).
- **wexhost (~180 lines).** Exports `WexWmSetEffects`, `WexWmSetThumbnails`, `WexWmSetPeek`, `WexWmGetEffectScale`
  have no callers; ten Unix call slots are never called (`wm_start`, the seven `composition_*` corner/backdrop/thumbs/
  peek/clock/clock_wait, and the three behind the exports above). `wexhost.dll` and `wexhost.so` are always built
  together, so the "no slot is deleted" rule of `DWM_UNIFICATION.md` buys nothing. The clock event handle passed to
  the Unix side is never used.
- **DWM.** `DWM_OP_ECHO` and `WexDwmpChannelEcho` are used by tests only; the closed-client branches of
  `abort_dwm_message` and `dwm_reply` in `server/dwm.c` cannot run; message header fields `request_id`,
  `connection_epoch` (in the header), `capabilities` and `revision` are overwritten or never read.
- **wexcomp.** `wex_window_apply_corner_radius` has no callers; per-request privilege checks can never fail (the
  global filter already hides the manager); protocol branches below version 11 serve only the test clients (Wine binds
  11, wexhost 15); the test scripts export `LABWC_WEX_SHELL_SOCKET`, but the compositor reads `WEX_SHELL_SOCKET`.

## Refactors, by value

DComp and DXGI (`wine/dlls/dcomp`, `dxgi`, `wined3d`):

- **C1. One animatable-float type (~280 lines).** Rectangle clip, transforms, offsets and effect opacity each store
  "value plus optional animation" their own way; one `dcomp_animated_float` with a setter macro generates the 44 thunks.
- **C2. Commit does the GPU readback once.** `dcomp_binding_target_scene` rebuilds every layer (a staging copy and Map
  per surface) and WineD3D then publishes the scene prepared earlier: a wasted readback per surface per Commit. Split
  the scene interface into prepare / activate / revoke and drop its redundant `HWND` argument.
- **C3. One walk for validate and collect (~120 lines)**, with a `dcomp_matrix_multiply` helper and the content kind
  looked up once instead of four times.
- **C4. One surface vtable (~110 lines)** instead of three plus two sets of forwarding methods.
- **C5. Inline the nine single-caller `*_locked` functions (~100 lines).**
- **C6. One layer geometry struct** shared by `dcomp_layer.h` and `wined3d.h` (they are field-for-field copies).

DWM broker chain (`wine/server/dwm.c`, `dlls/dwmapi`, `src/programs/dwm`, `src/dlls/wexshell/dwm_bridge.c`):

- **W1. Client channels survive a broker restart** (~100 lines, ~60 of them stock): the endpoint already outlives the
  broker; clients see the restart in the epoch (which also fixes bug 6).
- **W2. Shorter server handlers** (~120 stock lines): one exit path in `dwm_send/recv/reply`, one helper for the
  caller-desktop/endpoint/revoked checks.
- **W3. A 16-byte message header** instead of 40 and the unread reply fields in `protocol.def` (~50 stock lines;
  protocol change, all parts rebuilt together).
- **W4. A revision per broker entry instead of the change ring** (~80 lines): removes `DWM_POLL_SNAPSHOT`, the cursor
  and bug 9. The bridge is already event driven (change event plus WinEvent hook); `DWM_ARCHITECTURE.md`'s "250 ms poll"
  is out of date.
- **W5. One reader for the `HKCU\Software\WineExplorer\Shell` gates** (`DwmFrameEffects` is read in four places,
  `DwmFrameStyle` in three with different caching) and one helper for the per-desktop object names (built four times).
- **Owner decision:** the token broker's transport (a per-user named pipe and `GetNamedPipeClientProcessId`) applied to
  the DWM channel would remove about 1,350 stock lines (`server/dwm.c`, its requests, `ProcessWineDwmChannel`). It was
  rejected in `DWM_ARCHITECTURE.md` section 2 because a pid does not name the caller's desktop; the data is cosmetic
  and the frame clock and change event already use per-desktop named objects, so the decision may be worth reopening.
  The two brokers should not be merged.

wexhost (`src/dlls/wexhost`):

- **H1. One X-macro list for the Unix call table (~190 lines):** the enum and the native and WoW64 tables are the same
  list three times.
- **H2. One thumbnail/Peek record** holding an X window id or a Wayland handle (~60 lines) instead of 8 KiB of Wayland
  records plus a borrowed X11 pointer.
- **H3. A real identity record** for thumbnails and Peek instead of the 112-byte effects parameters; with H2 the
  duplicated rounding helper (`wexhost.c:986` = `wm.c:170`) goes too.
- **H4. One clock wait** (the backend wait slot only forwards) and one "publish without the composed flag" helper.
- **H5. One lock for X11 effects**, and helpers in `compose.c` for the window-id guard, the shadow/backdrop property
  writes and `paint_window`'s three clip-and-composite blocks.
- **H6. The protocol XML.** `wex-window-v1.xml` exists three times: wexhost's and wexcomp's are identical (version 15),
  Wine's is a version-11 subset. Make wexcomp's canonical, keep Wine's byte-identical (a stock change), and check it
  with a script.
- Already clean: the `wm.c` start/stop state machine, `wayland_zones.c`, `wloutput.c`. Three "left open" items of
  `DWM_UNIFICATION.md` are resolved in the code (stop keeps the thread handle on timeout, `wm_loop` reads `running`
  under the lock, same-backend surface replacement re-applies effects).

wexcomp (`wexcomp`, labwc fork):

- **K1. SceneFX as a pinned wrap** instead of a 10.8k-line copy: it is SceneFX 0.3 with two small changes (surfaceless
  EGL fallback, a blur warning fix) and a `meson.build` trim. The patch file the notes name is in no repository and has
  to be regenerated from the copy. Needs meson 0.63 and a package cache for offline builds.
- **K2. One `wex-surface.c`** for the buffer lookup (three copies), the source-box/opaque crop (three) and the input
  callbacks (~110 lines).
- **K3. Split `wex_window_update_effects`** (290 lines) into corners, backdrop and shadow (~120 lines); it also rebuilds
  the backdrop and shadow trees on every surface commit, which damages the window each frame.
- **K4. State kept twice:** `view->wex_layer/exclusive_edge/exclusive_size/corner_radius` duplicate the wex window's;
  the wex blocks in `layers.c`, `keyboard.c`, `desktop.c` and `view.c` can become one-line hook calls, which keeps
  upstream labwc merges small.
- **K5. Test scaffolding:** a window helper, barrier, handle listener and signal setup in `shm-common` (~650 lines over
  the C clients) and a `tests/wex/lib.sh` for the three runners (~200 lines).

## Suggested order

1. The open bugs 5-8 (DWM client and gate consistency) and 10-13: small, behaviour fixes, each with its own test.
2. Dead code: dcomp/DXGI target path, wexhost exports and slots, DWM echo/header fields. Pure removals; dcomp, dxgi,
   dwmapi tests and the wexhost probes cover them.
3. C2 (performance), H1, H6, W1+W2, then the larger C1, C3, K1-K5.
4. The transport decision (owner).

Each step: WoW64 build, the module's tests on both architectures, and for wexhost/wexcomp the visual probes
(`scripts/compose-probe.c`, `scripts/check-wayland-dwm.sh`, `tests/wex/run*.sh`).
