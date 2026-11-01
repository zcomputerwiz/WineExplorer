<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work Order: Phase G3 Required SharedImage Resources and Cross-Process Synchronization

**Date:** 2026-10-18  
**Branch:** `gemini/chromium-graphics`  
**Reference Plan:** `docs/CHROMIUM_GRAPHICS_PLAN.md` (Section 5)  
**Parent Acceptance:** `docs/CHROMIUM_GRAPHICS_ACCEPTANCE.md`  

---

## 1. Scope & Objective

Phase G3 implements and validates the cross-process resource sharing and synchronization primitives required by modern multi-process Chromium-derived runtimes (Chromium and Edge WebView2 155+).

As demonstrated in Phases G0 through G2:
- Phase G2 proved that single-process DirectComposition and DXGI composition swapchains operate cleanly under default Windows 10/11 reporting mode.
- In multi-process production execution, Chromium's renderer processes allocate textures via `SharedImage` and pass them to the Viz display compositor across process boundaries.
- Cross-process sharing requires Windows NT object handles (`D3D11_RESOURCE_MISC_SHARED_NTHANDLE`) queryable as `DxgkSharedResource`, and synchronization requires `IDXGIKeyedMutex` or `ID3D11Fence`.
- Returning `E_NOTIMPL` on `OpenSharedResource1` or `CreateFence` forces Chromium into `STATUS_BREAKPOINT` abort loops when running full multi-process Viz.

Phase G3 delivers the complete vertical slice enabling cross-process GPU texture sharing and monotonic fence synchronization.

---

## 2. Deliverables & Components

1. **`wine/dlls/dxgi`:**
   - Implement `IDXGIResource1::CreateSharedHandle` for `D3D11_RESOURCE_MISC_SHARED_NTHANDLE` returning genuine `DxgkSharedResource` NT handles via `NtGdiDdDDIShareObjects`.
   - Implement `IDXGIKeyedMutex` interface on `struct dxgi_resource` with `AcquireSync` and `ReleaseSync`.
   - Support `IID_IDXGIKeyedMutex` in `dxgi_resource_inner_QueryInterface`.

2. **`wine/dlls/d3d11`:**
   - Implement `ID3D11Device1::OpenSharedResource1` and `OpenSharedResourceByName` importing cross-process NT texture handles.
   - Implement `ID3D11Device5::CreateFence` and `OpenSharedFence` creating and importing `ID3D11Fence` objects.
   - Implement `ID3D11Fence` methods: `GetCompletedValue`, `SetEventOnCompletion`, and `CreateSharedHandle`.
   - Implement `ID3D11DeviceContext4::Signal` and `Wait` updating fence timeline values and triggering CPU/GPU event waiters.

3. **Diagnostic Test Seams:**
   - `scripts/test_g3_sharing_probe.cpp`: Win32 PE multi-process diagnostic probe testing:
     - Parent process creates shared texture + fence, duplicates/passes NT handle to child process.
     - Child process imports texture via `OpenSharedResource1`, writes frame data, and signals `ID3D11Fence`.
     - Parent waits for fence completion (`SetEventOnCompletion` + `WaitForSingleObject`) and validates texture state.
   - `scripts/test_graphics_probes.py`: Updated harness validating 100% PASS on both `x86_64` and `i686` WoW64 targets.

4. **Consumer Runtime Verification:**
   - Execute Edge WebView2 `155.0.4283.62` in default `baseline` mode without `nodcomp` flag under default Windows 10/11 reporting mode.
   - Verify multi-process Viz compositor initialization without crash loops.

5. **Acceptance Matrix:**
   - Update `docs/CHROMIUM_GRAPHICS_ACCEPTANCE.md` with complete empirical results, marking Phase G3 as **Passed**.

---

## 3. Implementation Rules & Constraints

1. Stay strictly on branch `gemini/chromium-graphics`; do not merge to `master`, do not force push.
2. Maintain clean LGPL-2.1+ licensing and attribution inside the `wine` submodule.
3. Keep dirty checkout `/root/WineExplorer` and `WineExplorer-VM` completely untouched; all work takes place in `/mnt/d/DeepThought/Projects/WineExplorer`.
4. Honest capability reporting: no fake no-op stubs masking missing synchronization.
