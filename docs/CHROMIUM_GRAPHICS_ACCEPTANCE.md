<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium Graphics Acceptance Matrix & Verification Report

**Date:** 2026-10-18  
**Branch:** `gemini/chromium-graphics`  
**Base Commit:** `12ca0aa6ff879f03dd1e7ed09452a49fe15884da` on `origin/master` (`1fa6323385e9b069fafae60afdb42ee68e8e72bf`)  
**Work Order:** `docs/CHROMIUM_GRAPHICS_G0_WORKORDER.md`  
**Reference Plan:** `docs/CHROMIUM_GRAPHICS_PLAN.md`  
**Status:** **Phase G0 Initial Baseline & Diagnostic Seams Established**

---

## 1. Executive Summary

This report establishes the baseline tracking matrix, hardware inventory, and low-level diagnostic test seams for the Chromium Graphics implementation on WineExplorer.

In accordance with `CHROMIUM_GRAPHICS_PLAN.md`:
- Hardware acceleration claims must be backed by physical GPU evidence; software fallback is a recognized compatibility result, not acceleration.
- DirectComposition is not assumed to be the sole browser presentation route; non-DComp paths (ANGLE HWND/EGL) are evaluated in G1.
- Low-level Win32/PE diagnostic probes test both 32-bit (`i686`) and 64-bit (`x86_64`) WoW64 targets without relying on proprietary third-party libraries.
- All current Wine stub behaviors (`dcomp.dll`, `dxgi.dll`, `d3d11.dll`) are documented honestly without masked return codes.

---

## 2. Phased Acceptance Gate Matrix

| Phase | Description | Status | Key Criteria / Deliverables |
|---|---|---|---|
| **G0** | Reproducible baseline & hardware inventory | **In Progress** | 32/64 PE probes for D3D11, DComp, and shared resources; hardware classification; exact baseline first-failure traces. |
| **G1** | Accelerated non-DComp path | Pending G0 | Trace default ANGLE D3D11 selection with DComp disabled; prove WineD3D/OpenGL and DXVK; continuous on-screen UI/WebGL. |
| **G2** | Minimal real DirectComposition slice | Pending G1 | DirectComposition device creation, visual tree, composition swapchain present to target window, deterministic 2-color animation. |
| **G3** | Required SharedImage resources & sync | Pending G2 | `CreateSharedHandle`, `OpenSharedResource1`, keyed mutex, cross-process fences, GPU completion validation. |
| **G4** | Full basic composition & efficient GPU delivery | Pending G3 | Visual transforms/clips/alpha, GPU composition replacing CPU readback, display pacing, zero continuous polling loops. |
| **G5** | Optional feature tracks (video, overlays, WebGPU) | Pending G4 | Software video decode, NV12/P010 views, hardware decode profiles, WebGPU/Dawn D3D12 interop. |
| **G6** | Token integration, packaging & release | Pending G5 | Ordinary unelevated browser with restricted children; per-user graphics views; fresh ISO installation acceptance. |

---

## 3. Pinned Dependency Inventory

| Component | Pinned Version / SHA | Provenance / Role |
|---|---|---|
| **Wine Submodule** | `ed5fe967ed471ca524666631ad70dabbed6af076` | `wineexplorer/11.19` tree with prefix-split and Wayland L2 |
| **wexcomp Submodule** | `acc46fe619ef1dd917ffd0b0aaf66ec13083290b` | `codex/wayland-polish` compositor |
| **Bundled DXVK** | `2.4.1` | Packaged DXGI/D3D11 Vulkan translation layer |
| **Bundled vkd3d-proton** | `2.13` | Packaged D3D12 Vulkan translation layer |
| **Chromium Inspection Base** | `2aa482d1e15bf9d2c382df5cfcb474fb6c0acbd1` | Pinned Chromium main for graphics pipeline contracts |
| **WebView2 Host & Runtime** | SDK `1.0.4258.31` / Runtime `155.0.4283.45` | Established C test host in `scripts/webview2-probe.c` |

---

## 4. Hardware & Backend Inventory

| Target Environment | Graphics Controller / Adapter | OpenGL Backend | Vulkan Backend | Classification / Role |
|---|---|---|---|---|
| **Host Workstation** | NVIDIA GeForce RTX 3090 (x2) + Intel UHD Graphics 750 | Host drivers (OpenGL 4.6, Vulkan 1.3) | Physical GPU positive control |
| **WineExplorer-VM** | VirtualBox VMSVGA (128 MB VRAM) | Mesa SVGA3D (OpenGL 3.3 / 4.1 partial) | Software lavapipe only | Virtualized guest baseline; known GL 4.4 ceiling |
| **Mesa Software Fallback** | CPU Software Rasterizer | llvmpipe (OpenGL 4.5) | lavapipe (Vulkan 1.3) | Clean CPU software control; no hardware acceleration claimed |
| **Target Older iGPU** | Intel HD Graphics (Gen 7–9) | Mesa iris / i965 (OpenGL 4.5/4.6) | Vulkan partial or unsupported | Primary required hardware tier for WineD3D/OpenGL Path A |

---

## 5. Desktop Composition & Window Delivery Baseline

| Display Backend | Compositor State | Layered Alpha / Effects | Behavior with Chromium Child Windows |
|---|---|---|---|
| **X11 (Default)** | D1/D2 Effects OFF (default) | Standard GDI/X11 surface blitting | Ordinary HWND child windows present directly; no double redirection. |
| **X11 (Composited)** | D1/D2 Effects ON (`wexhost/compose.c`) | Window alpha, drop shadows, blend fades | Wine GL/Vulkan child surfaces redirected via XComposite; requires single compositor owner. |
| **Wayland** | wexcomp (`acc46fe6`) | Native Wayland surface composition | Native-frame groups manage child window geometry and input; EGL/DMA-BUF surface delivery. |

---

## 6. Low-Level PE Probes (`scripts/test_graphics_probes.cpp`)

The test probe suite exercises the three primary graphics seams required by Chromium-derived runtimes:

### Probe 1: Direct3D 11 & DXGI HWND SwapChain
- **Entry Point:** `D3D11CreateDevice` with `D3D_DRIVER_TYPE_HARDWARE` (falling back to `D3D_DRIVER_TYPE_WARP`).
- **Feature Level Detection:** Evaluates `11_1`, `11_0`, `10_1`, `10_0`, `9_3`.
- **SwapChain Creation:** `IDXGIFactory::CreateSwapChain` targeting an active test `HWND`.
- **Presentation:** Executes `IDXGISwapChain::Present(1, 0)` and validates buffer swapping.

### Probe 2: DirectComposition & Composition SwapChain Stubs
- **Dynamic Entry Points:** `dcomp.dll` -> `DCompositionCreateDevice`, `DCompositionCreateDevice2`, `DCompositionCreateDevice3`.
- **Target Creation:** `IDCompositionDesktopDevice::CreateTargetForHwnd`.
- **Composition SwapChain:** `IDXGIFactory2::CreateSwapChainForComposition`.
- **Baseline Expectation:** Wine stock returns `E_NOTIMPL` (0x80004001) for all three device creation functions and `CreateSwapChainForComposition`.

### Probe 3: Resource Sharing & Synchronization
- **Shared NT Handles:** Creates a D3D11 texture with `D3D11_RESOURCE_MISC_SHARED_NTHANDLE` and queries `IDXGIResource1::CreateSharedHandle`.
- **Resource Import:** Tests `ID3D11Device1::OpenSharedResource1`.
- **Keyed Mutex:** Creates texture with `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX` and queries `IDXGIKeyedMutex`.
- **Fences:** Tests `ID3D11Device5::CreateFence` and queries `ID3D11Fence`.
- **Baseline Expectation:** Wine stock currently lacks `OpenSharedResource1` (`E_NOTIMPL`) and `CreateFence` (`E_NOTIMPL`).

---

## 7. Baseline Verification Evidence

Executed via `scripts/test_graphics_probes.py` against WoW64 Wine (`wine-11.19-204-g199dbcdabb1`):

| Test Case | Architecture | Driver / Backend | Exit Code | Result Summary |
|---|---|---|---|---|
| **Probe 1: D3D11 SwapChain** | x86_64 | WineD3D / OpenGL | 0 | `D3D11CreateDevice` passed (`S_OK`), Feature Level `11_1`, HWND SwapChain `S_OK`, `Present` `S_OK`. |
| **Probe 1: D3D11 SwapChain** | i686 | WineD3D / OpenGL | 0 | `D3D11CreateDevice` passed (`S_OK`), Feature Level `11_1`, HWND SwapChain `S_OK`, `Present` `S_OK`. |
| **Probe 2: DComp Entry Points** | x86_64 | Wine `dcomp.dll` | 0 | `dcomp.dll` loaded, symbols exported, all return `E_NOTIMPL` (0x80004001). `CreateSwapChainForComposition` returns `E_NOTIMPL` (0x80004001). |
| **Probe 2: DComp Entry Points** | i686 | Wine `dcomp.dll` | 0 | `dcomp.dll` loaded, symbols exported, all return `E_NOTIMPL` (0x80004001). `CreateSwapChainForComposition` returns `E_NOTIMPL` (0x80004001). |
| **Probe 3: Shared Resources** | x86_64 | Wine `d3d11.dll` | 0 | `NT_HANDLE` returns `E_INVALIDARG` (0x80070057); `KeyedMutex` texture created, `AcquireSync` returns `E_NOINTERFACE`; `CreateFence` returns `E_NOTIMPL` (0x80004001). |
| **Probe 3: Shared Resources** | i686 | Wine `d3d11.dll` | 0 | `NT_HANDLE` returns `E_INVALIDARG` (0x80070057); `KeyedMutex` texture created, `AcquireSync` returns `E_NOINTERFACE`; `CreateFence` returns `E_NOTIMPL` (0x80004001). |

Aggregated JSON evidence saved to `build/graphics_baseline_summary.json`.

---

## 8. Verified First-Failure Traces

1. **DirectComposition Creation Failure:**
   - Call: `DCompositionCreateDevice(pDXGIDevice, IID_IDCompositionDevice, &pDev)`
   - Call: `DCompositionCreateDevice2(pDXGIDevice, IID_IDCompositionDesktopDevice, &pDev)`
   - Call: `DCompositionCreateDevice3(pDXGIDevice, IID_IDCompositionDesktopDevice, &pDev)`
   - Location: `wine/dlls/dcomp/device.c:28-47`
   - Trace: `FIXME("%p, %s, %p.\n", ...); return E_NOTIMPL;`
   - Consequence: Chromium's `direct_composition_support.cc` falls back or aborts presenter initialization when DComp device creation returns `E_NOTIMPL`.

2. **Composition SwapChain Creation Failure:**
   - Call: `IDXGIFactory2::CreateSwapChainForComposition(pDevice, &desc, NULL, &pSwapChain)`
   - Location: `wine/dlls/dxgi/factory.c:390-397`
   - Trace: `FIXME("iface %p, device %p, desc %p, output %p, swapchain %p stub!\n", ...); return E_NOTIMPL;`
   - Consequence: DComp presenter cannot allocate composition-backed frame buffers.

3. **D3D11 Shared Resource / Fence Failure:**
   - Call: `ID3D11Device5::CreateFence(...)`
   - Location: `wine/dlls/d3d11/device.c:5435-5440`
   - Trace: `FIXME("iface %p, initial_value %I64x, flags %d, iid %s, fence %p stub!\n", ...); return E_NOTIMPL;`
   - Call: `ID3D11Device1::OpenSharedResource1(...)`
   - Location: `wine/dlls/d3d11/device.c:5256-5261`
   - Trace: `FIXME("iface %p, handle %p, iid %s, resource %p stub!\n", ...); return E_NOTIMPL;`
   - Consequence: Cross-process Viz / GPU synchronization cannot exchange fences or shared NT handles.

