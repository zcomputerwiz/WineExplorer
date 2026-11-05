<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Wine-Staging overlap audit (2026-11-04)

Static audit of Wine-Staging (checkout 605e214, 119 patchsets; patches under `build/review/wine-staging/`, index in
`build/review/wine-staging-index.txt`, dcomp series in `build/review/staging-dcomp/`) against `docs/STOCK_CHANGES.md`.
Nothing was run. Adopting staging code keeps its LGPL-2.1+ notices and its authorship (dcomp: Zhiyi Zhang, CodeWeavers).

| Area | Overlap | Recommend | Effort | Risk |
|---|---|---|---|---|
| dcomp core/surfaces | staging has Commit, surfaces, a composition thread and tests; ours has a retained scene and CPU compositor | rebase the API and tests on staging, keep our compositor | L | ABI and threading conflicts |
| dcomp 0061/0062/0063 | context race, shared visual handles, `IDCompositionSurfaceUnknown` | adopt 0062 and 0063, borrow the 0061 state-isolation idea | M/L | wineserver and device-model coupling |
| DXGI frame statistics | staging is a one-patch semi-stub, ours is the DWM clock | keep ours, borrow validation and tests | S | the two clocks can diverge |
| uxtheme GTK theming | disabled GTK3 backend | keep ours | S | XP/colour/geometry conflicts |
| user32 / win32u | narrow text, metrics and message fixes | borrow tests and edge cases selectively | M | non-client behaviour, message ordering |
| winex11 / wined3d | window-style and discard-rotation patches touch adjacent paths | rebase selectively after DwmAlpha tests | M | DwmAlpha and retained-frame regressions |
| shell / system | mostly unrelated or disabled | keep ours, adopt fixes only with a reproducer | M | shell UX, licence churn |
| other index matches | `comctl32-rebar-capture`, `server-PeekMessage`, `user32-DM_SETDEFID`, ... | audit when a symptom matches | S/M | scope creep |

## dcomp
- Staging 0017 composes pending-to-active with a composition thread and D2D/GDI `AlphaBlend`; 0055 composes
  `IDCompositionSurface` through it. Ours validates a visual tree and builds target scenes at Commit
  (`dlls/dcomp/device.c`).
- Retained single-Commit redraw: sits above staging's API and state model; replacing it with 0017 would lose our
  transaction and scene semantics.
- DwmAlpha / NOREDIRECTIONBITMAP glass through GDI to an ARGB surface: orthogonal transport; staging's target compositor
  would need a target-output adapter.
- Child-HWND Chromium targets: compatible in principle; cross-process and foreign-window policy stays ours.
- Thumbnail source frames: orthogonal, staging has none. CPU reference compositor and narrow GL path: keep ours.
- Aux 1x1 producer and the staging copy of surface textures: 0055 is prior art for surface composition; the producer
  indirection and CPU frame ownership are extra design.
- 0061 saves and restores D3D11 context state; ours serializes `CopyResource`/`Map`/`Unmap` with `ID3D11Multithread`.
  Adopt the state-isolation principle, not a wholesale replacement.
- 0062 (wineserver shared visual handles) is the right implementation of our todo item: rebase on that ABI.
- 0063/0064 (`IDCompositionSurfaceUnknown`, resize): adopt after reconciling with our virtual-surface code.

## Other areas
- DXGI: our `GetFrameStatistics` is still `E_NOTIMPL`; `DwmGetCompositionTimingInfo` uses the compositor clock. Later map
  present counters and clock generations into DXGI semantics; borrow staging's argument and error tests.
- uxtheme GTK: nothing to adopt.
- user32/win32u: our `__wine_nc_buttons`, Aero hit test and hot tracking are custom contracts with no staging duplicate;
  borrow the activation and message-order tests.
- winex11: `Window_Style` may affect DwmAlpha windows (not our frame flag change); `WM_WINDOWPOSCHANGING` is disabled
  (unclear rebase), do not adopt. `wined3d-rotate-WINED3D_SWAP_EFFECT_DISCARD` could conflict with our retained
  producer: keep ours unless a regression proves otherwise.
- shell: no own progress dialog, icon cache or copy engine was found in `STOCK_CHANGES.md` (a duplicate is unverified).
