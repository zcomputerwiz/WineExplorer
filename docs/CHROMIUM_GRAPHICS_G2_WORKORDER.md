<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work Order: Phase G2 Minimal DirectComposition Vertical Slice

**Date:** 2026-10-18  
**Branch:** `gemini/chromium-graphics`  
**Reference Plan:** `docs/CHROMIUM_GRAPHICS_PLAN.md`  
**Parent Acceptance:** `docs/CHROMIUM_GRAPHICS_ACCEPTANCE.md`  

---

## 1. Scope & Objective

Phase G2 implements and validates the minimal DirectComposition and DXGI vertical slice required by modern Chromium-derived runtimes (Chromium/Edge WebView2 155+).

As demonstrated in Phase G0 and G1:
- Modern Chromium in default Windows 10/11 mode requires `DCompositionCreateDevice` (or `DCompositionCreateDevice3`) to succeed.
- Returning `E_NOTIMPL` induces a fatal `STATUS_BREAKPOINT` abort loop in `OutputDeviceBacking::GetOrCreateDXObjects()`.
- While Phase G1 proved that non-DComp presentation is achievable via GDI `CopyHDC` under Windows 7 mode, true modern browser execution under default Windows reporting requires the DirectComposition retained-tree presentation route.

---

## 2. Dependencies & giang17/wine Evaluation

In accordance with Section 4 (Phase G2) of `CHROMIUM_GRAPHICS_PLAN.md`:
1. **Source Evaluation:**
   - Evaluated `giang17/wine` commit `eb72ef008555348bf114e5da64c0d8868c46f49a` (`d2d1-dcomp-11.0`).
2. **Reused & Adapted Subsets:**
   - COM object implementation for `IDCompositionDevice`, `IDCompositionDevice2`, `IDCompositionDevice3`, `IDCompositionDesktopDevice`.
   - Visual tree lifecycle: `CreateVisual`, `AddVisual`, `RemoveVisual`, `SetContent`, `SetOffsetX`, `SetOffsetY`.
   - Target binding: `CreateTargetForHwnd`, `SetRoot`.
   - Atomic tree publishing: `Commit()`.
   - Composition swapchain bridge: `IDXGIFactory2::CreateSwapChainForComposition` allocating DXGI swapchains and binding them to the target HWND via `WM_WINE_DCOMP_SET_TARGET`.
3. **Rejected Components:**
   - Unrelated patches (audio, memory allocator, font configuration, MSI installer fixes).
   - CPU-timeline fake fences (deferred to Phase G3).
   - Forced GDI DIB readback for accelerated paths.

---

## 3. Deliverables

1. **`wine/dlls/dcomp`:**
   - Export and implement `DCompositionCreateDevice`, `DCompositionCreateDevice2`, `DCompositionCreateDevice3`.
   - Implement `IDCompositionDevice` (1, 2, 3) and `IDCompositionDesktopDevice`.
   - Implement `IDCompositionTarget` and `IDCompositionVisual` (1, 2).
   - Retained-tree traversal and swapchain target notification on `Commit()`.

2. **`wine/dlls/dxgi`:**
   - Implement `IDXGIFactory2::CreateSwapChainForComposition`.
   - Handle `WM_WINE_DCOMP_SET_TARGET` to bind swapchain frames to the target HWND.
   - Present composition frames directly to the target window via `wined3d_swapchain_present`.

3. **Diagnostic Test Seams:**
   - `scripts/test_g2_dcomp_probe.cpp`: Win32 PE diagnostic probe testing single-commit retained tree presentation over 60+ continuous frames and dynamic buffer resize.
   - `scripts/test_graphics_probes.py`: Updated harness validating 100% PASS on both `x86_64` and `i686` WoW64 targets.

4. **Consumer Runtime Verification:**
   - Test Microsoft Edge WebView2 `155.0.4283.62` under default Windows 10/11 mode.
   - Verify Viz compositor initialization without crash loops.

5. **Acceptance Matrix:**
   - Update `docs/CHROMIUM_GRAPHICS_ACCEPTANCE.md` with complete empirical results.
