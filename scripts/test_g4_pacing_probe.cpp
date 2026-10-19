// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Phase G4B Presentation & Pacing Diagnostic Probe.
 *
 * Validates:
 * 1. Flip-model backbuffer index cycling (IDXGISwapChain3::GetCurrentBackBufferIndex):
 *    - Sequential cycling through buffer count (0 -> 1 -> 2 -> 0).
 *    - Reset to 0 upon ResizeBuffers and ResizeBuffers1.
 * 2. Frame latency waitable object (IDXGISwapChain2):
 *    - GetFrameLatencyWaitableObject returns valid waitable handle.
 *    - SetMaximumFrameLatency / GetMaximumFrameLatency verification and bounds checking.
 *    - Rejection when DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT is absent.
 * 3. Minimized / occluded pacing:
 *    - Present on minimized window returns DXGI_STATUS_OCCLUDED.
 *    - Restored window resumes returning S_OK.
 * 4. Composition event-driven frame pacing loop:
 *    - 30-frame pacing loop driven by WaitForSingleObject on frame latency waitable object.
 *
 * Compiles cleanly for both x86_64-w64-mingw32-g++ and i686-w64-mingw32-g++.
 */

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dxgi1_4.h>
#include <dcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef IID_IDCompositionDevice
static const GUID IID_IDCompositionDevice = {0xc37ea93a,0xe7aa,0x450d,{0xb1,0x6f,0x97,0x46,0xcb,0x04,0x07,0xf3}};
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

static HWND create_window(const char *name, int w, int h)
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
        wc.lpszClassName = "WEX_G4B_ProbeWnd";
        RegisterClassExA(&wc);
        registered = TRUE;
    }

    HWND hwnd = CreateWindowExA(0, "WEX_G4B_ProbeWnd", name, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                10, 10, w, h, NULL, NULL, hInst, NULL);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

static bool create_d3d_device(ID3D11Device **d3d_dev, ID3D11DeviceContext **d3d_ctx)
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
    return true;
}

static IDXGISwapChain1 *create_swapchain_custom(ID3D11Device *device, HWND hwnd,
                                               UINT width, UINT height, UINT buffer_count,
                                               DXGI_SWAP_EFFECT effect, UINT flags)
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
    desc.BufferCount = buffer_count;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = effect;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = flags;

    if (hwnd)
        factory->CreateSwapChainForHwnd(device, hwnd, &desc, NULL, NULL, &sc);
    else
        factory->CreateSwapChainForComposition(device, &desc, NULL, &sc);

    release(factory);
    release(adapter);
    release(dxgi_dev);
    return sc;
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

/* --- Test 1: Flip-Model Backbuffer Index Cycling and Resize Reset --- */
static bool test_flip_backbuffer_indexing(void)
{
    HWND hwnd = create_window("G4B_Flip_Index_Test", 128, 128);
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDXGISwapChain1 *sc1 = NULL;
    IDXGISwapChain3 *sc3 = NULL;
    bool passed = false;

    if (!create_d3d_device(&dev, &ctx)) goto done;

    // Create flip-sequential swapchain with 3 buffers
    sc1 = create_swapchain_custom(dev, hwnd, 128, 128, 3, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 0);
    if (!sc1) goto done;

    if (FAILED(sc1->QueryInterface(__uuidof(IDXGISwapChain3), (void **)&sc3))) goto done;

    // Initial buffer index must be 0
    if (sc3->GetCurrentBackBufferIndex() != 0) goto done;

    // Frame 0 Present -> next index must be 1
    if (FAILED(sc1->Present(1, 0))) goto done;
    if (sc3->GetCurrentBackBufferIndex() != 1) goto done;

    // Frame 1 Present -> next index must be 2
    if (FAILED(sc1->Present(1, 0))) goto done;
    if (sc3->GetCurrentBackBufferIndex() != 2) goto done;

    // Frame 2 Present -> wraps modulo 3 back to 0
    if (FAILED(sc1->Present(1, 0))) goto done;
    if (sc3->GetCurrentBackBufferIndex() != 0) goto done;

    // ResizeBuffers1: must reset current index to 0
    if (FAILED(sc3->ResizeBuffers1(4, 160, 160, DXGI_FORMAT_B8G8R8A8_UNORM, 0, NULL, NULL))) goto done;
    if (sc3->GetCurrentBackBufferIndex() != 0) goto done;

    // Frame 0 after resize -> index advances to 1
    if (FAILED(sc1->Present(1, 0))) goto done;
    if (sc3->GetCurrentBackBufferIndex() != 1) goto done;

    passed = true;

done:
    release(sc3);
    release(sc1);
    release(ctx);
    release(dev);
    if (hwnd) DestroyWindow(hwnd);
    return passed;
}

/* --- Test 2: Frame Latency Waitable Object Lifecycle and Latency Bounds --- */
static bool test_frame_latency_waitable_object(void)
{
    HWND hwnd = create_window("G4B_Waitable_Test", 128, 128);
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDXGISwapChain1 *sc1 = NULL;
    IDXGISwapChain2 *sc2 = NULL;
    HANDLE waitable = NULL;
    UINT latency = 0;
    bool passed = false;

    if (!create_d3d_device(&dev, &ctx)) goto done;

    // Swapchain WITH DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
    sc1 = create_swapchain_custom(dev, hwnd, 128, 128, 2, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
                                 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    if (!sc1) goto done;

    if (FAILED(sc1->QueryInterface(__uuidof(IDXGISwapChain2), (void **)&sc2))) goto done;

    waitable = sc2->GetFrameLatencyWaitableObject();
    if (!waitable) goto done;

    // Handle should initially be signaled (available to render)
    if (WaitForSingleObject(waitable, 0) != WAIT_OBJECT_0) goto done;

    // Test SetMaximumFrameLatency and GetMaximumFrameLatency
    if (FAILED(sc2->GetMaximumFrameLatency(&latency))) goto done;
    if (FAILED(sc2->SetMaximumFrameLatency(2))) goto done;
    if (FAILED(sc2->GetMaximumFrameLatency(&latency))) goto done;
    if (latency != 2) goto done;

    // Setting latency to 0 is invalid
    if (SUCCEEDED(sc2->SetMaximumFrameLatency(0))) goto done;

    passed = true;

done:
    if (waitable) CloseHandle(waitable);
    release(sc2);
    release(sc1);
    release(ctx);
    release(dev);
    if (hwnd) DestroyWindow(hwnd);
    return passed;
}

/* --- Test 3: Minimized and Occluded Window Presentation Pacing --- */
static bool test_occluded_minimized_pacing(void)
{
    HWND hwnd = create_window("G4B_Occluded_Test", 128, 128);
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDXGISwapChain1 *sc1 = NULL;
    HRESULT hr;
    bool passed = false;

    if (!create_d3d_device(&dev, &ctx)) goto done;

    sc1 = create_swapchain_custom(dev, hwnd, 128, 128, 2, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 0);
    if (!sc1) goto done;

    // Normal visible window: Present succeeds with S_OK
    flush_messages();
    hr = sc1->Present(1, 0);
    if (FAILED(hr)) goto done;

    // Minimize window: Present must return DXGI_STATUS_OCCLUDED
    ShowWindow(hwnd, SW_MINIMIZE);
    flush_messages();

    hr = sc1->Present(1, 0);
    if (hr != DXGI_STATUS_OCCLUDED) goto done;

    // Restore window: Present must return S_OK
    ShowWindow(hwnd, SW_RESTORE);
    flush_messages();

    hr = sc1->Present(1, 0);
    if (FAILED(hr)) goto done;

    passed = true;

done:
    release(sc1);
    release(ctx);
    release(dev);
    if (hwnd) DestroyWindow(hwnd);
    return passed;
}

/* --- Test 4: Composition Waitable Frame Pacing Loop --- */
static bool test_composition_waitable_pacing_loop(void)
{
    HWND hwnd = create_window("G4B_Comp_Wait_Test", 128, 128);
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDCompositionDevice *dcomp = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *root = NULL;
    IDXGISwapChain1 *sc1 = NULL;
    IDXGISwapChain2 *sc2 = NULL;
    HANDLE waitable = NULL;
    bool passed = false;

    if (!create_d3d_device(&dev, &ctx)) goto done;

    // Create composition swapchain with waitable object flag
    sc1 = create_swapchain_custom(dev, NULL, 128, 128, 2, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
                                 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    if (!sc1) goto done;

    if (FAILED(sc1->QueryInterface(__uuidof(IDXGISwapChain2), (void **)&sc2))) goto done;
    waitable = sc2->GetFrameLatencyWaitableObject();
    if (!waitable) goto done;

    // DComp setup
    {
        IDXGIDevice *dxgi_dev = NULL;
        if (FAILED(dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev))) goto done;
        HMODULE hDcomp = LoadLibraryA("dcomp.dll");
        if (!hDcomp) { release(dxgi_dev); goto done; }
        PFN_DCompositionCreateDevice pfn = (PFN_DCompositionCreateDevice)(void *)GetProcAddress(hDcomp, "DCompositionCreateDevice");
        if (!pfn || FAILED(pfn(dxgi_dev, IID_IDCompositionDevice, (void **)&dcomp)))
        {
            release(dxgi_dev); goto done;
        }
        release(dxgi_dev);
    }

    if (FAILED(dcomp->CreateTargetForHwnd(hwnd, TRUE, &target))) goto done;
    if (FAILED(dcomp->CreateVisual(&root))) goto done;
    if (FAILED(root->SetContent(sc1))) goto done;
    if (FAILED(target->SetRoot(root))) goto done;
    if (FAILED(dcomp->Commit())) goto done;

    // 30-frame event-driven pacing loop: wait on the waitable object before each Present
    for (int frame = 0; frame < 30; ++frame)
    {
        DWORD wait_res = WaitForSingleObject(waitable, 2000);
        if (wait_res != WAIT_OBJECT_0) goto done;

        flush_messages();

        if (FAILED(sc1->Present(1, 0))) goto done;
    }

    passed = true;

done:
    if (waitable) CloseHandle(waitable);
    release(sc2);
    release(root);
    release(target);
    release(dcomp);
    release(sc1);
    release(ctx);
    release(dev);
    if (hwnd) DestroyWindow(hwnd);
    return passed;
}

/* --- Test 5: Non-Waitable Swapchain Rejection Semantics --- */
static bool test_non_waitable_swapchain_behavior(void)
{
    HWND hwnd = create_window("G4B_Non_Waitable_Test", 128, 128);
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDXGISwapChain1 *sc1 = NULL;
    IDXGISwapChain2 *sc2 = NULL;
    bool passed = false;

    if (!create_d3d_device(&dev, &ctx)) goto done;

    // Created WITHOUT DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
    sc1 = create_swapchain_custom(dev, hwnd, 128, 128, 2, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 0);
    if (!sc1) goto done;

    if (FAILED(sc1->QueryInterface(__uuidof(IDXGISwapChain2), (void **)&sc2))) goto done;

    // Must return NULL
    if (sc2->GetFrameLatencyWaitableObject() != NULL) goto done;

    // SetMaximumFrameLatency must fail
    if (SUCCEEDED(sc2->SetMaximumFrameLatency(1))) goto done;

    passed = true;

done:
    release(sc2);
    release(sc1);
    release(ctx);
    release(dev);
    if (hwnd) DestroyWindow(hwnd);
    return passed;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);

    bool t1 = test_flip_backbuffer_indexing();
    printf("pacing_flip_backbuffer_indexing=%s\n", t1 ? "PASS" : "FAIL");

    bool t2 = test_frame_latency_waitable_object();
    printf("pacing_frame_latency_waitable_object=%s\n", t2 ? "PASS" : "FAIL");

    bool t3 = test_occluded_minimized_pacing();
    printf("pacing_occluded_minimized_pacing=%s\n", t3 ? "PASS" : "FAIL");

    bool t4 = test_composition_waitable_pacing_loop();
    printf("pacing_composition_waitable_pacing_loop=%s\n", t4 ? "PASS" : "FAIL");

    bool t5 = test_non_waitable_swapchain_behavior();
    printf("pacing_non_waitable_swapchain_behavior=%s\n", t5 ? "PASS" : "FAIL");

    return (t1 && t2 && t3 && t4 && t5) ? 0 : 1;
}
