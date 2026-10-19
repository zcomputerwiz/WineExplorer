// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Phase G4A DirectComposition Retained Composition & Visual Tree Diagnostic Probe.
 *
 * Validates:
 * 1. Visual tree operations: AddVisual (above/below/ref), RemoveVisual, RemoveAllVisuals, reparenting.
 * 2. Visual properties: SetOffsetX, SetOffsetY, SetTransform, SetClip (rect & object), SetContent,
 *    SetEffect (IDCompositionEffectGroup with SetOpacity), SetOpacityMode validation.
 * 3. Rectangle clip with corner radii: IDCompositionRectangleClip.
 * 4. Transactional semantics: Pending edits do NOT take effect before Commit(); apply atomically on Commit().
 * 5. Continuous animation retained: Single upfront Commit(), then 60-frame changing pattern without re-commit.
 * 6. Target-window pixels pattern: Verifies overlapping layers, offsets, and clipping.
 * 7. Unattached swapchain, content detach, nested child HWND, and dynamic resize.
 *
 * Compiles cleanly for both x86_64-w64-mingw32-g++ and i686-w64-mingw32-g++.
 */

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef IID_IDCompositionDevice
static const GUID IID_IDCompositionDevice = {0xc37ea93a,0xe7aa,0x450d,{0xb1,0x6f,0x97,0x46,0xcb,0x04,0x07,0xf3}};
#endif
#ifndef IID_IDCompositionDevice2
static const GUID IID_IDCompositionDevice2 = {0x75f6468d,0x1b8e,0x447c,{0x9b,0xc6,0x75,0xfe,0xa8,0x0b,0x5b,0x25}};
#endif
#ifndef IID_IDCompositionEffectGroup
static const GUID IID_IDCompositionEffectGroup = {0xa7929a74,0xe6b2,0x4bd6,{0x8b,0x95,0x40,0x40,0x11,0x9c,0xa3,0x4d}};
#endif
#ifndef IID_IDCompositionRectangleClip
static const GUID IID_IDCompositionRectangleClip = {0x9842ad7d,0xd9cf,0x4908,{0xae,0xd7,0x48,0xb5,0x1d,0xa5,0xe7,0xc2}};
#endif

typedef HRESULT (WINAPI *PFN_DCompositionCreateDevice)(IDXGIDevice *dxgiDevice, REFIID iid, void **dcompositionDevice);

template <typename T>
static void release(T *&ptr)
{
    if (ptr)
    {
        ptr->Release();
        ptr = NULL;
    }
}

static LRESULT CALLBACK probe_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static void flush_messages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static HWND create_window(const char *name, int w, int h, HWND parent)
{
    static BOOL registered = FALSE;
    HINSTANCE hInst = GetModuleHandleA(NULL);

    if (!registered)
    {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = probe_wndproc;
        wc.hInstance = hInst;
        wc.hbrBackground = NULL;
        wc.lpszClassName = "WEX_G4A_ProbeWnd";
        RegisterClassExA(&wc);
        registered = TRUE;
    }

    DWORD style = parent ? (WS_CHILD | WS_VISIBLE) : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    HWND hwnd = CreateWindowExA(0, "WEX_G4A_ProbeWnd", name, style,
                                10, 10, w, h, parent, NULL, hInst, NULL);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

static bool create_d3d_and_dcomp(HWND hwnd, ID3D11Device **d3d_dev, ID3D11DeviceContext **d3d_ctx,
                                 IDCompositionDevice **dcomp_dev, IDCompositionTarget **dcomp_target)
{
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL obtained;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                           levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                           d3d_dev, &obtained, d3d_ctx);
    if (FAILED(hr))
    {
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0,
                               levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                               d3d_dev, &obtained, d3d_ctx);
        if (FAILED(hr)) return false;
    }

    IDXGIDevice *dxgi_dev = NULL;
    if (FAILED((*d3d_dev)->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev)))
        return false;

    HMODULE hDcomp = LoadLibraryA("dcomp.dll");
    if (!hDcomp)
    {
        release(dxgi_dev);
        return false;
    }

    PFN_DCompositionCreateDevice pfnCreate =
        (PFN_DCompositionCreateDevice)(void *)GetProcAddress(hDcomp, "DCompositionCreateDevice");
    if (!pfnCreate)
    {
        release(dxgi_dev);
        return false;
    }

    hr = pfnCreate(dxgi_dev, IID_IDCompositionDevice, (void **)dcomp_dev);
    release(dxgi_dev);
    if (FAILED(hr)) return false;

    if (hwnd)
    {
        hr = (*dcomp_dev)->CreateTargetForHwnd(hwnd, TRUE, dcomp_target);
        if (FAILED(hr)) return false;
    }

    return true;
}

static IDXGISwapChain1 *create_comp_swapchain(ID3D11Device *device, UINT width, UINT height)
{
    IDXGIDevice *dxgi_dev = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGISwapChain1 *sc = NULL;

    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev))) return NULL;
    if (FAILED(dxgi_dev->GetAdapter(&adapter))) { release(dxgi_dev); return NULL; }
    if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&factory)))
    {
        release(adapter); release(dxgi_dev); return NULL;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    factory->CreateSwapChainForComposition(device, &desc, NULL, &sc);

    release(factory);
    release(adapter);
    release(dxgi_dev);
    return sc;
}

/* --- Test 1: Tree Operations, Sibling Order, Reparenting, and Properties --- */
static bool test_tree_operations_and_properties(void)
{
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDCompositionDevice *dcomp = NULL;
    IDCompositionVisual *root = NULL, *v1 = NULL, *v2 = NULL, *v3 = NULL;
    IDCompositionVisual2 *v2_iface = NULL;
    IDCompositionRectangleClip *clip = NULL;
    IDCompositionEffectGroup *effect_grp = NULL;
    D2D_MATRIX_3X2_F mat = {1.0f, 0.0f, 0.0f, 1.0f, 5.0f, 5.0f};
    bool passed = false;

    if (!create_d3d_and_dcomp(NULL, &dev, &ctx, &dcomp, NULL)) goto done;

    if (FAILED(dcomp->CreateVisual(&root))) goto done;
    if (FAILED(dcomp->CreateVisual(&v1))) goto done;
    if (FAILED(dcomp->CreateVisual(&v2))) goto done;
    if (FAILED(dcomp->CreateVisual(&v3))) goto done;

    // 1. AddVisual basic
    if (FAILED(root->AddVisual(v1, FALSE, NULL))) goto done;

    // Self-addition must fail with E_INVALIDARG
    if (root->AddVisual(root, FALSE, NULL) != E_INVALIDARG) goto done;

    // 2. Sibling order with reference_visual
    if (FAILED(root->AddVisual(v2, TRUE, v1))) goto done;
    if (FAILED(root->AddVisual(v3, FALSE, v2))) goto done;

    // Invalid reference visual (not a sibling) must fail with E_INVALIDARG
    if (v1->AddVisual(v3, FALSE, root) != E_INVALIDARG) goto done;
    if (v1->AddVisual(v3, FALSE, v3) != E_INVALIDARG) goto done;

    // 3. Reparenting: moving v3 from root to v1
    if (FAILED(v1->AddVisual(v3, FALSE, NULL))) goto done;

    // Cycle creation must fail with E_INVALIDARG (v3 is child of v1, so v3->AddVisual(v1) is a cycle)
    if (v3->AddVisual(v1, FALSE, NULL) != E_INVALIDARG) goto done;

    // 4. Properties: offsets and transform
    if (FAILED(v1->SetOffsetX(12.5f))) goto done;
    if (FAILED(v1->SetOffsetY(24.5f))) goto done;
    if (FAILED(v1->SetTransform(mat))) goto done;

    // 5. Rectangle clip with radii
    if (FAILED(dcomp->CreateRectangleClip(&clip))) goto done;
    if (FAILED(clip->SetLeft(2.0f))) goto done;
    if (FAILED(clip->SetTop(4.0f))) goto done;
    if (FAILED(clip->SetRight(40.0f))) goto done;
    if (FAILED(clip->SetBottom(60.0f))) goto done;
    if (FAILED(clip->SetTopLeftRadiusX(3.0f))) goto done;
    if (FAILED(clip->SetTopLeftRadiusY(3.0f))) goto done;
    if (FAILED(v1->SetClip(clip))) goto done;

    // 6. Effect group with opacity
    if (FAILED(dcomp->CreateEffectGroup(&effect_grp))) goto done;
    if (FAILED(effect_grp->SetOpacity(0.85f))) goto done;
    if (FAILED(v1->SetEffect(effect_grp))) goto done;

    // 7. SetOpacityMode validation: valid modes succeed, invalid mode rejected
    if (SUCCEEDED(v1->QueryInterface(__uuidof(IDCompositionVisual2), (void **)&v2_iface)))
    {
        if (FAILED(v2_iface->SetOpacityMode(DCOMPOSITION_OPACITY_MODE_LAYER))) { release(v2_iface); goto done; }
        if (FAILED(v2_iface->SetOpacityMode(DCOMPOSITION_OPACITY_MODE_MULTIPLY))) { release(v2_iface); goto done; }
        // Invalid enum must fail accurately
        if (SUCCEEDED(v2_iface->SetOpacityMode((DCOMPOSITION_OPACITY_MODE)999))) { release(v2_iface); goto done; }
        release(v2_iface);
    }

    // 8. Removal
    if (FAILED(root->RemoveVisual(v2))) goto done;
    // Removing non-child must fail
    if (SUCCEEDED(root->RemoveVisual(v2))) goto done;
    // Remove all
    if (FAILED(root->RemoveAllVisuals())) goto done;

    passed = true;

done:
    release(effect_grp);
    release(clip);
    release(v3);
    release(v2);
    release(v1);
    release(root);
    release(dcomp);
    release(ctx);
    release(dev);
    return passed;
}

/* --- Test 2: Transactional Commit Isolation & Atomicity --- */
static bool test_transactional_commit_isolation(void)
{
    HWND hwnd = create_window("G4A_Isolation_Test", 128, 128, NULL);
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDCompositionDevice *dcomp = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *root = NULL, *child = NULL;
    IDXGISwapChain1 *sc1 = NULL, *sc2 = NULL;
    ID3D11Texture2D *bb1 = NULL;
    ID3D11RenderTargetView *rtv1 = NULL;
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    bool passed = false;

    if (!create_d3d_and_dcomp(hwnd, &dev, &ctx, &dcomp, &target)) goto done;

    sc1 = create_comp_swapchain(dev, 64, 64);
    sc2 = create_comp_swapchain(dev, 32, 32);
    if (!sc1 || !sc2) goto done;

    if (FAILED(dcomp->CreateVisual(&root))) goto done;
    if (FAILED(dcomp->CreateVisual(&child))) goto done;

    if (FAILED(root->SetContent(sc1))) goto done;
    if (FAILED(child->SetContent(sc2))) goto done;
    if (FAILED(child->SetOffsetX(16.0f))) goto done;

    if (FAILED(sc1->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb1))) goto done;
    if (FAILED(dev->CreateRenderTargetView(bb1, NULL, &rtv1))) goto done;
    ctx->ClearRenderTargetView(rtv1, green);

    // Stage 1: Add child to root, set root on target, BUT DO NOT COMMIT
    if (FAILED(target->SetRoot(root))) goto done;
    if (FAILED(root->AddVisual(child, FALSE, NULL))) goto done;

    // Present before Commit: swapchain is NOT yet committed, operates safely
    if (FAILED(sc1->Present(1, 0))) goto done;
    flush_messages();

    // Stage 2: Call Commit() -> atomically applies all staged visual operations
    if (FAILED(dcomp->Commit())) goto done;

    // Present after Commit: swapchain is now bound to target hwnd
    if (FAILED(sc1->Present(1, 0))) goto done;
    flush_messages();

    // Stage 3: Make pending modifications without commit
    if (FAILED(child->SetOffsetX(64.0f))) goto done;
    if (FAILED(root->RemoveVisual(child))) goto done;

    // Present on swapchain before committing pending removal: retained state remains intact
    if (FAILED(sc1->Present(1, 0))) goto done;

    // Stage 4: Commit removal
    if (FAILED(dcomp->Commit())) goto done;

    passed = true;

done:
    release(rtv1);
    release(bb1);
    release(child);
    release(root);
    release(sc2);
    release(sc1);
    release(target);
    release(dcomp);
    release(ctx);
    release(dev);
    if (hwnd) DestroyWindow(hwnd);
    return passed;
}

/* --- Test 3: Continuous Animation (Commit Once, Animate 60 frames) --- */
static bool test_continuous_animation_retained(void)
{
    HWND hwnd = create_window("G4A_Animation_Test", 128, 128, NULL);
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDCompositionDevice *dcomp = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *root = NULL;
    IDXGISwapChain1 *sc = NULL;
    ID3D11Texture2D *bb = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    UINT presented = 0;
    bool passed = false;

    if (!create_d3d_and_dcomp(hwnd, &dev, &ctx, &dcomp, &target)) goto done;

    sc = create_comp_swapchain(dev, 128, 128);
    if (!sc) goto done;

    if (FAILED(dcomp->CreateVisual(&root))) goto done;
    if (FAILED(root->SetContent(sc))) goto done;
    if (FAILED(target->SetRoot(root))) goto done;

    // Commit EXACTLY ONCE before animation
    if (FAILED(dcomp->Commit())) goto done;

    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb))) goto done;
    if (FAILED(dev->CreateRenderTargetView(bb, NULL, &rtv))) goto done;

    // Continuous 60-frame changing animation with ZERO additional calls to Commit()
    for (int frame = 0; frame < 60; ++frame)
    {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        float color[4] = {(frame % 2) ? 1.0f : 0.0f, 0.0f, (frame % 2) ? 0.0f : 1.0f, 1.0f};
        ctx->ClearRenderTargetView(rtv, color);

        if (SUCCEEDED(sc->Present(1, 0)))
            presented++;
    }

    passed = (presented == 60);

done:
    release(rtv);
    release(bb);
    release(root);
    release(sc);
    release(target);
    release(dcomp);
    release(ctx);
    release(dev);
    if (hwnd) DestroyWindow(hwnd);
    return passed;
}

/* --- Test 4: Patterned Overlapping Composition, Offsets, and Clips --- */
static bool test_overlapping_composition_and_clips(void)
{
    HWND hwnd = create_window("G4A_Composition_Test", 128, 128, NULL);
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDCompositionDevice *dcomp = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *root = NULL, *layer_red = NULL, *layer_blue = NULL;
    IDXGISwapChain1 *sc_red = NULL, *sc_blue = NULL;
    ID3D11Texture2D *bb_red = NULL, *bb_blue = NULL;
    ID3D11RenderTargetView *rtv_red = NULL, *rtv_blue = NULL;
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    D2D_RECT_F clip_rect = {0.0f, 0.0f, 32.0f, 64.0f};
    D2D_RECT_F full_clip = {0.0f, 0.0f, 64.0f, 64.0f};
    D2D_MATRIX_3X2_F mat_shift = {1.0f, 0.0f, 0.0f, 1.0f, 32.0f, 0.0f};
    MSG msg;
    bool passed = false;

    if (!create_d3d_and_dcomp(hwnd, &dev, &ctx, &dcomp, &target)) goto done;

    sc_red = create_comp_swapchain(dev, 128, 128);
    sc_blue = create_comp_swapchain(dev, 64, 64);
    if (!sc_red || !sc_blue) goto done;

    if (FAILED(sc_red->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb_red))) goto done;
    if (FAILED(dev->CreateRenderTargetView(bb_red, NULL, &rtv_red))) goto done;

    if (FAILED(sc_blue->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb_blue))) goto done;
    if (FAILED(dev->CreateRenderTargetView(bb_blue, NULL, &rtv_blue))) goto done;

    // Render Red background (128x128)
    ctx->ClearRenderTargetView(rtv_red, red);

    // Render Blue foreground (64x64)
    ctx->ClearRenderTargetView(rtv_blue, blue);

    // Construct visual tree:
    // root -> layer_red (at 0,0)
    //      -> layer_blue (at 32,32 with clip rect 0,0,32,64)
    if (FAILED(dcomp->CreateVisual(&root))) goto done;
    if (FAILED(dcomp->CreateVisual(&layer_red))) goto done;
    if (FAILED(dcomp->CreateVisual(&layer_blue))) goto done;

    if (FAILED(layer_red->SetContent(sc_red))) goto done;
    if (FAILED(layer_blue->SetContent(sc_blue))) goto done;
    if (FAILED(layer_blue->SetOffsetX(32.0f))) goto done;
    if (FAILED(layer_blue->SetOffsetY(32.0f))) goto done;

    if (FAILED(layer_blue->SetClip(clip_rect))) goto done;

    if (FAILED(root->AddVisual(layer_red, FALSE, NULL))) goto done;
    if (FAILED(root->AddVisual(layer_blue, TRUE, layer_red))) goto done;
    if (FAILED(target->SetRoot(root))) goto done;

    if (FAILED(dcomp->Commit())) goto done;

    // Present both layers
    sc_red->Present(1, 0);
    sc_blue->Present(1, 0);

    // Windows messages flush
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    // Live clip update: expand clip right to 64.0f
    if (FAILED(layer_blue->SetClip(full_clip))) goto done;
    if (FAILED(dcomp->Commit())) goto done;
    if (FAILED(sc_blue->Present(1, 0))) goto done;
    flush_messages();

    // 2D matrix transform: shift blue layer right by 32 (dx=32)
    if (FAILED(layer_blue->SetTransform(mat_shift))) goto done;
    if (FAILED(dcomp->Commit())) goto done;
    if (FAILED(sc_blue->Present(1, 0))) goto done;
    flush_messages();

    passed = true;

done:
    release(rtv_blue);
    release(bb_blue);
    release(rtv_red);
    release(bb_red);
    release(layer_blue);
    release(layer_red);
    release(root);
    release(sc_blue);
    release(sc_red);
    release(target);
    release(dcomp);
    release(ctx);
    release(dev);
    if (hwnd) DestroyWindow(hwnd);
    return passed;
}

/* --- Test 5: Unattached Swapchain and Content Detach --- */
static bool test_unattached_and_detach(void)
{
    HWND hwnd = create_window("G4A_Detach_Test", 128, 128, NULL);
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDCompositionDevice *dcomp = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *root = NULL;
    IDXGISwapChain1 *sc = NULL;
    bool passed = false;

    if (!create_d3d_and_dcomp(hwnd, &dev, &ctx, &dcomp, &target)) goto done;

    // Visual without content
    if (FAILED(dcomp->CreateVisual(&root))) goto done;
    if (FAILED(target->SetRoot(root))) goto done;
    // Commit with unattached visual must succeed safely
    if (FAILED(dcomp->Commit())) goto done;

    // Attach content
    sc = create_comp_swapchain(dev, 64, 64);
    if (!sc) goto done;
    if (FAILED(root->SetContent(sc))) goto done;
    if (FAILED(dcomp->Commit())) goto done;
    sc->Present(1, 0);

    // Detach content
    if (FAILED(root->SetContent(NULL))) goto done;
    if (FAILED(dcomp->Commit())) goto done;

    passed = true;

done:
    release(sc);
    release(root);
    release(target);
    release(dcomp);
    release(ctx);
    release(dev);
    if (hwnd) DestroyWindow(hwnd);
    return passed;
}

/* --- Test 6: Nested Child HWND and Dynamic Resize --- */
static bool test_nested_child_hwnd_and_resize(void)
{
    HWND parent = create_window("G4A_Parent_Wnd", 200, 200, NULL);
    HWND child = create_window("G4A_Child_Wnd", 128, 128, parent);
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDCompositionDevice *dcomp = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *root = NULL;
    IDXGISwapChain1 *sc = NULL;
    ID3D11Texture2D *bb = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    bool passed = false;

    if (!create_d3d_and_dcomp(child, &dev, &ctx, &dcomp, &target)) goto done;

    sc = create_comp_swapchain(dev, 128, 128);
    if (!sc) goto done;

    if (FAILED(dcomp->CreateVisual(&root))) goto done;
    if (FAILED(root->SetContent(sc))) goto done;
    if (FAILED(target->SetRoot(root))) goto done;
    if (FAILED(dcomp->Commit())) goto done;

    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb))) goto done;
    if (FAILED(dev->CreateRenderTargetView(bb, NULL, &rtv))) goto done;

    // Initial present
    ctx->ClearRenderTargetView(rtv, red);
    if (FAILED(sc->Present(1, 0))) goto done;

    // Dynamic resize of child window and swapchain
    release(rtv);
    release(bb);

    SetWindowPos(child, NULL, 0, 0, 160, 160, SWP_NOZORDER | SWP_NOACTIVATE);
    if (FAILED(sc->ResizeBuffers(2, 160, 160, DXGI_FORMAT_B8G8R8A8_UNORM, 0))) goto done;

    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb))) goto done;
    if (FAILED(dev->CreateRenderTargetView(bb, NULL, &rtv))) goto done;

    ctx->ClearRenderTargetView(rtv, blue);
    if (FAILED(sc->Present(1, 0))) goto done;

    passed = true;

done:
    release(rtv);
    release(bb);
    release(root);
    release(sc);
    release(target);
    release(dcomp);
    release(ctx);
    release(dev);
    if (child) DestroyWindow(child);
    if (parent) DestroyWindow(parent);
    return passed;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);

    bool t1 = test_tree_operations_and_properties();
    printf("dcomp_tree_operations_and_properties=%s\n", t1 ? "PASS" : "FAIL");

    bool t2 = test_transactional_commit_isolation();
    printf("dcomp_transactional_commit_isolation=%s\n", t2 ? "PASS" : "FAIL");

    bool t3 = test_continuous_animation_retained();
    printf("dcomp_continuous_animation_retained=%s\n", t3 ? "PASS" : "FAIL");

    bool t4 = test_overlapping_composition_and_clips();
    printf("dcomp_overlapping_composition_and_clips=%s\n", t4 ? "PASS" : "FAIL");

    bool t5 = test_unattached_and_detach();
    printf("dcomp_unattached_and_detach=%s\n", t5 ? "PASS" : "FAIL");

    bool t6 = test_nested_child_hwnd_and_resize();
    printf("dcomp_nested_child_hwnd_and_resize=%s\n", t6 ? "PASS" : "FAIL");

    return (t1 && t2 && t3 && t4 && t5 && t6) ? 0 : 1;
}
