<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: Chromium Graphics Phase G0 Baseline & Diagnostic Probes

**Date:** 2026-10-18  
**Branch:** `gemini/chromium-graphics`  
**Base Commit:** `12ca0aa6ff879f03dd1e7ed09452a49fe15884da` (`origin/codex/chromium-graphics-plan` on `origin/master` `1fa6323385e9b069fafae60afdb42ee68e8e72bf`)  
**Submodules:** Wine `ed5fe967ed471ca524666631ad70dabbed6af076`, wexcomp `acc46fe619ef1dd917ffd0b0aaf66ec13083290b`  
**Reference Plan:** `docs/CHROMIUM_GRAPHICS_PLAN.md`  
**Reference Research:** `docs/CHROMIUM_GRAPHICS_RESEARCH.md`  
**Acceptance Document:** `docs/CHROMIUM_GRAPHICS_ACCEPTANCE.md`  

---

## 1. Objectives & Boundaries

Phase G0 establishes the reproducible baseline, test matrix, hardware inventory, and low-level Win32/PE test probes required before any DirectComposition or graphics driver modifications are introduced.

### In Scope
1. **Repository & Acceptance Baseline:**
   - Establish `docs/CHROMIUM_GRAPHICS_ACCEPTANCE.md` recording pinned dependencies, test matrix, hardware/backend inventory, and phase exit gates (G0 through G6).
   - Document known baseline status and exact stub behavior in current Wine (`dcomp.dll`, `dxgi.dll`, `d3d11.dll`).
2. **Deterministic Low-Level PE Probes (`scripts/test_graphics_probes.cpp`):**
   - Implement standalone Win32/PE probe executables built for both `x86_64` (64-bit) and `i686` (32-bit WoW64) with zero proprietary third-party dependencies.
   - **Probe 1 (D3D11 / HWND Swapchain):** Verify `D3D11CreateDevice`, query maximum supported Feature Level, create DXGI HWND swapchain, and present test frames.
   - **Probe 2 (DirectComposition & Composition Swapchain):** Test dynamic entry points for `DCompositionCreateDevice`, `DCompositionCreateDevice2`, and `DCompositionCreateDevice3` in `dcomp.dll`; test `IDXGIFactory2::CreateSwapChainForComposition`; verify truthful `E_NOTIMPL` / failure reporting without masking.
   - **Probe 3 (Resource Sharing & Synchronization):** Test `CreateSharedHandle` on D3D11 textures, test `ID3D11Device1::OpenSharedResource1`, test `IDXGIKeyedMutex` acquisition/release, and test `ID3D11Device5::CreateFence`.
3. **Deterministic Test Runner (`scripts/test_graphics_probes.py`):**
   - Automate cross-compilation with MinGW and headless execution under Wine (`WINEDEBUG=-all`, `WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"`).
   - Parse and assert structured JSON output from the probes for CI and automated regression.
4. **Hardware & Backend Inventory:**
   - Classify target hardware tiers: physical GPU (discrete / integrated), virtual GPU (VirtualBox SVGA), and software rasterizers (lavapipe, llvmpipe).
   - Document the baseline behavior under X11 (desktop composition off vs on) and Wayland (wexcomp).

### Out of Scope for Phase G0
- Implementing new DComp rendering logic or surface presentation in Wine (deferred to G1/G2).
- Merging external forks (`giang17/wine`, Proton-GE, DXVK master) into the main Wine submodule.
- Making modern Vulkan 1.3 or D3D12/WebGPU a baseline requirement for desktop or browser execution.
- Global registry or version spoofing (e.g. forcing global Windows 7/8 mode).
- Modifying VM passthrough or destroying occupied test VM states.

---

## 2. Pinned Dependencies & Artifacts

| Component | Pinned Identifier / SHA | Purpose |
|---|---|---|
| **Wine Submodule** | `ed5fe967ed471ca524666631ad70dabbed6af076` | Base WineExplorer runtime (`wineexplorer/11.19`) |
| **wexcomp Submodule** | `acc46fe619ef1dd917ffd0b0aaf66ec13083290b` | Wayland compositor and native frame manager |
| **Bundled DXVK** | `2.4.1` | Pinned runtime package in manifest |
| **Bundled vkd3d-proton** | `2.13` | Pinned D3D12 runtime package in manifest |
| **Chromium Inspection Base** | `2aa482d1e15bf9d2c382df5cfcb474fb6c0acbd1` | Pinned Chromium main for graphics contract reference |
| **WebView2 Host & Runtime** | SDK `1.0.4258.31` / Runtime `155.0.4283.45` | Established diagnostic consumer host in `scripts/webview2-probe.c` |

---

## 3. Work Breakdown & Exit Criteria

1. **Step 1: Scoped Work Order & Acceptance Plan:** Document G0 scope in `docs/CHROMIUM_GRAPHICS_G0_WORKORDER.md` and initialize `docs/CHROMIUM_GRAPHICS_ACCEPTANCE.md`.
2. **Step 2: Low-Level PE Probes Implementation:** Create `scripts/test_graphics_probes.cpp` implementing Probes 1, 2, and 3 with structured JSON and human-readable output.
3. **Step 3: Test Harness & Automated Runner:** Create `scripts/test_graphics_probes.py` compiling both PE32 and PE32+ binaries and validating executions under Wine.
4. **Step 4: Execute Baseline Probes:** Run probes across available architectures and capture verified baseline logs into `docs/CHROMIUM_GRAPHICS_ACCEPTANCE.md`.
5. **Step 5: Review Gate:** Submit G0 results for project review prior to embarking on G1 (non-DComp accelerated path) or G2 (DirectComposition implementation).
