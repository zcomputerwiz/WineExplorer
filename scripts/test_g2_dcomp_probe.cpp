// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Phase G2 DirectComposition Animated Diagnostic Probe.
 *
 * Simulates Chromium's DirectComposition presentation architecture:
 * - A parent top-level application window.
 * - An intermediate child window (matching Chromium's child_window_win.cc).
 * - A DXGI composition swapchain created via IDXGIFactory2::CreateSwapChainForComposition.
 * - DirectComposition visual tree: Device -> Target(child_hwnd) -> Root Visual -> SetContent(swapchain).
 * - Single upfront Commit() establishing the retained visual pipeline.
 * - Continuous 60-frame 2-color retained visual animation (solid Red / solid Blue)
 *   via swapchain->Present(1, 0) with ZERO additional calls to Commit().
 * - Dynamic mid-stream resize (SetWindowPos + ResizeBuffers) without destroying the DComp tree.
 * - Comprehensive JSON metrics emission verifying retained tree behavior and timing.
 *
 * Compiles cleanly for both x86_64 and i686 MinGW without proprietary dependencies.
 */

#include <windows.h>
#include <d3d11.h>
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
#ifndef IID_IDCompositionDevice3
static const GUID IID_IDCompositionDevice3 = {0x0987cb06,0xf916,0x48bf,{0x8d,0x35,0xce,0x76,0x41,0x78,0x1b,0xd9}};
#endif
#ifndef IID_IDCompositionDesktopDevice
static const GUID IID_IDCompositionDesktopDevice = {0x5f4633fe,0x1e08,0x4cb8,{0x8c,0x75,0xce,0x24,0x33,0x3f,0x56,0x02}};
#endif

typedef HRESULT (WINAPI *PFN_DCompositionCreateDevice)(IDXGIDevice *dxgiDevice, REFIID iid, void **dcompositionDevice);
typedef HRESULT (WINAPI *PFN_DCompositionCreateDevice2)(IUnknown *renderingDevice, REFIID iid, void **dcompositionDevice);
typedef HRESULT (WINAPI *PFN_DCompositionCreateDevice3)(IUnknown *renderingDevice, REFIID iid, void **dcompositionDevice);

struct G2ProbeConfig {
    UINT frame_count;
    UINT width;
    UINT height;
    BOOL headless;
    BOOL json_output;
    BOOL test_resize;
};

struct G2ProbeMetrics {
    BOOL d3d11_device_created;
    HRESULT d3d11_hr;
    char driver_type[32];
    char feature_level[32];
    char adapter_description[128];
    UINT vendor_id;
    UINT device_id;
    BOOL parent_hwnd_created;
    BOOL child_hwnd_created;
    BOOL dcomp_dll_loaded;
    BOOL dcomp_create_device_called;
    HRESULT dcomp_create_device_hr;
    char dcomp_entry_point_used[32];
    BOOL dcomp_target_created;
    HRESULT dcomp_target_hr;
    BOOL dcomp_visual_created;
    HRESULT dcomp_visual_hr;
    BOOL composition_swapchain_created;
    HRESULT composition_swapchain_hr;
    HRESULT set_content_hr;
    HRESULT set_root_hr;
    HRESULT commit_hr;
    UINT commit_count;
    UINT frames_rendered;
    UINT frames_presented;
    HRESULT last_present_hr;
    BOOL retained_tree_verified;
    BOOL resize_attempted;
    BOOL resize_succeeded;
    HRESULT resize_hr;
    double elapsed_seconds;
    double fps;
};

static HWND g_hParent = NULL;
static HWND g_hChild = NULL;

static LRESULT CALLBACK ParentWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_SIZE: {
            if (g_hChild && wp != SIZE_MINIMIZED) {
                UINT new_w = LOWORD(lp);
                UINT new_h = HIWORD(lp);
                SetWindowPos(g_hChild, NULL, 0, 0, new_w, new_h,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static LRESULT CALLBACK ChildWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static const char *feature_level_to_str(D3D_FEATURE_LEVEL fl)
{
    switch (fl) {
        case D3D_FEATURE_LEVEL_12_1: return "12_1";
        case D3D_FEATURE_LEVEL_12_0: return "12_0";
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        case D3D_FEATURE_LEVEL_10_1: return "10_1";
        case D3D_FEATURE_LEVEL_10_0: return "10_0";
        case D3D_FEATURE_LEVEL_9_3:  return "9_3";
        case D3D_FEATURE_LEVEL_9_2:  return "9_2";
        case D3D_FEATURE_LEVEL_9_1:  return "9_1";
        default: return "unknown";
    }
}

static void register_classes(HINSTANCE hInstance)
{
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ParentWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "WEX_G2_ParentWnd";
    RegisterClassExA(&wc);

    WNDCLASSEXA child_wc;
    memset(&child_wc, 0, sizeof(child_wc));
    child_wc.cbSize = sizeof(child_wc);
    child_wc.lpfnWndProc = ChildWndProc;
    child_wc.hInstance = hInstance;
    child_wc.lpszClassName = "WEX_G2_ChildWnd";
    RegisterClassExA(&child_wc);
}

static void run_g2_probe(const G2ProbeConfig *cfg, G2ProbeMetrics *m)
{
    HINSTANCE hInst = GetModuleHandleA(NULL);
    register_classes(hInst);

    DWORD parent_style = cfg->headless ? WS_POPUP : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    g_hParent = CreateWindowExA(0, "WEX_G2_ParentWnd",
                                "Chromium G2 DirectComposition Animated Diagnostic Probe",
                                parent_style, CW_USEDEFAULT, CW_USEDEFAULT,
                                cfg->width, cfg->height, NULL, NULL, hInst, NULL);
    m->parent_hwnd_created = (g_hParent != NULL);
    if (!g_hParent) return;

    RECT rc;
    GetClientRect(g_hParent, &rc);
    UINT client_w = rc.right - rc.left;
    UINT client_h = rc.bottom - rc.top;
    if (client_w == 0) client_w = cfg->width;
    if (client_h == 0) client_h = cfg->height;

    // Create intermediate child window (Chromium child_window_win.cc)
    g_hChild = CreateWindowExA(0, "WEX_G2_ChildWnd", NULL,
                               WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                               0, 0, client_w, client_h,
                               g_hParent, NULL, hInst, NULL);
    m->child_hwnd_created = (g_hChild != NULL);
    if (!g_hChild) {
        DestroyWindow(g_hParent);
        return;
    }

    if (!cfg->headless) {
        ShowWindow(g_hParent, SW_SHOW);
        UpdateWindow(g_hParent);
    }

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3
    };
    D3D_FEATURE_LEVEL obtained_level = D3D_FEATURE_LEVEL_9_1;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;

    // Step 1: Create D3D11 rendering device
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                   0, levels, sizeof(levels) / sizeof(levels[0]),
                                   D3D11_SDK_VERSION, &dev, &obtained_level, &ctx);
    if (SUCCEEDED(hr)) {
        m->d3d11_device_created = TRUE;
        m->d3d11_hr = hr;
        strncpy(m->driver_type, "HARDWARE", sizeof(m->driver_type) - 1);
        strncpy(m->feature_level, feature_level_to_str(obtained_level), sizeof(m->feature_level) - 1);
    } else {
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL,
                               0, levels, sizeof(levels) / sizeof(levels[0]),
                               D3D11_SDK_VERSION, &dev, &obtained_level, &ctx);
        if (SUCCEEDED(hr)) {
            m->d3d11_device_created = TRUE;
            m->d3d11_hr = hr;
            strncpy(m->driver_type, "WARP", sizeof(m->driver_type) - 1);
            strncpy(m->feature_level, feature_level_to_str(obtained_level), sizeof(m->feature_level) - 1);
        } else {
            m->d3d11_device_created = FALSE;
            m->d3d11_hr = hr;
            DestroyWindow(g_hChild);
            DestroyWindow(g_hParent);
            return;
        }
    }

    // Step 2: Query IDXGIDevice and IDXGIFactory2
    IDXGIDevice *dxgi_dev = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory2 = NULL;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev))) {
        if (SUCCEEDED(dxgi_dev->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC ad;
            if (SUCCEEDED(adapter->GetDesc(&ad))) {
                WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1,
                                    m->adapter_description, sizeof(m->adapter_description) - 1,
                                    NULL, NULL);
                m->vendor_id = ad.VendorId;
                m->device_id = ad.DeviceId;
            }
            adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&factory2);
            adapter->Release();
        }
    }

    if (!factory2 || !dxgi_dev) {
        if (dxgi_dev) dxgi_dev->Release();
        if (ctx) ctx->Release();
        dev->Release();
        DestroyWindow(g_hChild);
        DestroyWindow(g_hParent);
        return;
    }

    // Step 3: Create DXGI Composition SwapChain via IDXGIFactory2::CreateSwapChainForComposition
    DXGI_SWAP_CHAIN_DESC1 sc_desc;
    memset(&sc_desc, 0, sizeof(sc_desc));
    sc_desc.Width = client_w;
    sc_desc.Height = client_h;
    sc_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc_desc.Stereo = FALSE;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.SampleDesc.Quality = 0;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.BufferCount = 2;
    sc_desc.Scaling = DXGI_SCALING_STRETCH;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sc_desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    sc_desc.Flags = 0;

    IDXGISwapChain1 *swapchain = NULL;
    hr = factory2->CreateSwapChainForComposition(dev, &sc_desc, NULL, &swapchain);
    m->composition_swapchain_hr = hr;
    m->composition_swapchain_created = SUCCEEDED(hr);
    factory2->Release();

    if (!SUCCEEDED(hr) || !swapchain) {
        dxgi_dev->Release();
        if (ctx) ctx->Release();
        dev->Release();
        DestroyWindow(g_hChild);
        DestroyWindow(g_hParent);
        return;
    }

    // Step 4: Load dcomp.dll and create DirectComposition Device
    HMODULE hDcomp = LoadLibraryA("dcomp.dll");
    m->dcomp_dll_loaded = (hDcomp != NULL);
    if (!hDcomp) {
        swapchain->Release();
        dxgi_dev->Release();
        if (ctx) ctx->Release();
        dev->Release();
        DestroyWindow(g_hChild);
        DestroyWindow(g_hParent);
        return;
    }

    IDCompositionDevice *dcomp_dev = NULL;
    PFN_DCompositionCreateDevice3 pfnCreateDevice3 =
        (PFN_DCompositionCreateDevice3)(void *)GetProcAddress(hDcomp, "DCompositionCreateDevice3");
    PFN_DCompositionCreateDevice2 pfnCreateDevice2 =
        (PFN_DCompositionCreateDevice2)(void *)GetProcAddress(hDcomp, "DCompositionCreateDevice2");
    PFN_DCompositionCreateDevice pfnCreateDevice =
        (PFN_DCompositionCreateDevice)(void *)GetProcAddress(hDcomp, "DCompositionCreateDevice");

    if (pfnCreateDevice3) {
        hr = pfnCreateDevice3(dxgi_dev, IID_IDCompositionDevice, (void **)&dcomp_dev);
        strncpy(m->dcomp_entry_point_used, "DCompositionCreateDevice3", sizeof(m->dcomp_entry_point_used) - 1);
    } else if (pfnCreateDevice2) {
        hr = pfnCreateDevice2(dxgi_dev, IID_IDCompositionDevice, (void **)&dcomp_dev);
        strncpy(m->dcomp_entry_point_used, "DCompositionCreateDevice2", sizeof(m->dcomp_entry_point_used) - 1);
    } else if (pfnCreateDevice) {
        hr = pfnCreateDevice(dxgi_dev, IID_IDCompositionDevice, (void **)&dcomp_dev);
        strncpy(m->dcomp_entry_point_used, "DCompositionCreateDevice", sizeof(m->dcomp_entry_point_used) - 1);
    } else {
        hr = E_FAIL;
    }
    m->dcomp_create_device_called = TRUE;
    m->dcomp_create_device_hr = hr;
    dxgi_dev->Release();

    if (!SUCCEEDED(hr) || !dcomp_dev) {
        FreeLibrary(hDcomp);
        swapchain->Release();
        if (ctx) ctx->Release();
        dev->Release();
        DestroyWindow(g_hChild);
        DestroyWindow(g_hParent);
        return;
    }

    // Step 5: Construct DirectComposition Visual Hierarchy
    // Target(child_hwnd) -> Visual -> SetContent(swapchain) -> SetRoot(visual)
    IDCompositionTarget *target = NULL;
    hr = dcomp_dev->CreateTargetForHwnd(g_hChild, TRUE, &target);
    m->dcomp_target_hr = hr;
    m->dcomp_target_created = SUCCEEDED(hr);

    IDCompositionVisual *visual = NULL;
    if (SUCCEEDED(hr)) {
        hr = dcomp_dev->CreateVisual(&visual);
        m->dcomp_visual_hr = hr;
        m->dcomp_visual_created = SUCCEEDED(hr);
    }

    if (SUCCEEDED(hr) && visual) {
        hr = visual->SetContent((IUnknown *)swapchain);
        m->set_content_hr = hr;
    }

    if (SUCCEEDED(hr) && target && visual) {
        hr = target->SetRoot(visual);
        m->set_root_hr = hr;
    }

    // Step 6: Commit transaction - CALLED EXACTLY ONCE
    if (SUCCEEDED(hr)) {
        hr = dcomp_dev->Commit();
        m->commit_hr = hr;
        if (SUCCEEDED(hr)) {
            m->commit_count = 1;
        }
    }

    if (!SUCCEEDED(m->commit_hr)) {
        if (visual) visual->Release();
        if (target) target->Release();
        dcomp_dev->Release();
        FreeLibrary(hDcomp);
        swapchain->Release();
        if (ctx) ctx->Release();
        dev->Release();
        DestroyWindow(g_hChild);
        DestroyWindow(g_hParent);
        return;
    }

    // Step 7: Create RenderTargetView from composition swapchain backbuffer
    ID3D11Texture2D *backbuffer = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    hr = swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backbuffer);
    if (SUCCEEDED(hr) && backbuffer) {
        dev->CreateRenderTargetView(backbuffer, NULL, &rtv);
        backbuffer->Release();
    }

    LARGE_INTEGER freq, t_start, t_end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_start);

    // Step 8: Continuous 60-frame 2-color Retained Visual Animation Loop
    // Zero additional Commit() calls occur during animation.
    for (UINT frame = 0; frame < cfg->frame_count; ++frame) {
        // Dispatch Win32 messages
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (rtv) {
            // Deterministic 2-color animation: alternate solid Red and solid Blue
            if ((frame % 2) == 0) {
                float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                ctx->ClearRenderTargetView(rtv, red);
            } else {
                float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
                ctx->ClearRenderTargetView(rtv, blue);
            }
            m->frames_rendered++;
        }

        // Present to the composition swapchain
        hr = swapchain->Present(1, 0);
        m->last_present_hr = hr;
        if (SUCCEEDED(hr)) {
            m->frames_presented++;
        }

        // Step 9: Dynamic Resize Test midway (frame 30)
        // Verifies swapchain buffers can resize dynamically while the retained visual tree persists
        if (cfg->test_resize && frame == (cfg->frame_count / 2)) {
            m->resize_attempted = TRUE;
            if (rtv) {
                rtv->Release();
                rtv = NULL;
            }

            UINT new_w = client_w + 64;
            UINT new_h = client_h + 48;

            SetWindowPos(g_hParent, NULL, 0, 0, new_w + 16, new_h + 38,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(g_hChild, NULL, 0, 0, new_w, new_h,
                         SWP_NOZORDER | SWP_NOACTIVATE);

            hr = swapchain->ResizeBuffers(2, new_w, new_h, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
            m->resize_hr = hr;
            m->resize_succeeded = SUCCEEDED(hr);

            if (SUCCEEDED(hr)) {
                ID3D11Texture2D *new_bb = NULL;
                if (SUCCEEDED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&new_bb)) && new_bb) {
                    dev->CreateRenderTargetView(new_bb, NULL, &rtv);
                    new_bb->Release();
                }
            }
        }
    }

    QueryPerformanceCounter(&t_end);
    m->elapsed_seconds = (double)(t_end.QuadPart - t_start.QuadPart) / (double)freq.QuadPart;
    if (m->elapsed_seconds > 0.0) {
        m->fps = (double)m->frames_presented / m->elapsed_seconds;
    }

    // Step 10: Verify Retained Tree Invariant
    // Exactly 1 Commit() was executed, and all 60 frames successfully presented.
    if (m->commit_count == 1 && m->frames_presented >= cfg->frame_count && SUCCEEDED(m->last_present_hr)) {
        m->retained_tree_verified = TRUE;
    }

    // Cleanup resources
    if (rtv) rtv->Release();
    if (visual) visual->Release();
    if (target) target->Release();
    dcomp_dev->Release();
    FreeLibrary(hDcomp);
    swapchain->Release();
    if (ctx) ctx->Release();
    dev->Release();
    DestroyWindow(g_hChild);
    DestroyWindow(g_hParent);
}

static void print_human(const G2ProbeMetrics *m)
{
    printf("=== Phase G2 DirectComposition Animated Diagnostic Probe ===\n");
    printf("  Device Created:           %s (type=%s, fl=%s)\n",
           m->d3d11_device_created ? "YES" : "NO", m->driver_type, m->feature_level);
    printf("  Adapter Description:      %s (Vendor=0x%04x, Device=0x%04x)\n",
           m->adapter_description, m->vendor_id, m->device_id);
    printf("  Parent HWND:              %s\n", m->parent_hwnd_created ? "YES" : "NO");
    printf("  Child HWND:               %s (simulating Chromium child_window_win.cc)\n",
           m->child_hwnd_created ? "YES" : "NO");
    printf("  DComp DLL Loaded:         %s\n", m->dcomp_dll_loaded ? "YES" : "NO");
    printf("  DComp CreateDevice:       hr=0x%08lx (via %s)\n",
           (unsigned long)m->dcomp_create_device_hr, m->dcomp_entry_point_used);
    printf("  Composition SwapChain:    %s (hr=0x%08lx)\n",
           m->composition_swapchain_created ? "YES" : "NO", (unsigned long)m->composition_swapchain_hr);
    printf("  Target for HWND:          %s (hr=0x%08lx)\n",
           m->dcomp_target_created ? "YES" : "NO", (unsigned long)m->dcomp_target_hr);
    printf("  Visual Created:           %s (hr=0x%08lx)\n",
           m->dcomp_visual_created ? "YES" : "NO", (unsigned long)m->dcomp_visual_hr);
    printf("  Visual SetContent:        hr=0x%08lx\n", (unsigned long)m->set_content_hr);
    printf("  Target SetRoot:           hr=0x%08lx\n", (unsigned long)m->set_root_hr);
    printf("  DComp Commit:             hr=0x%08lx (commit_count=%u)\n",
           (unsigned long)m->commit_hr, m->commit_count);
    printf("  Frames Rendered:          %u\n", m->frames_rendered);
    printf("  Frames Presented:         %u (last_hr=0x%08lx)\n",
           m->frames_presented, (unsigned long)m->last_present_hr);
    printf("  Retained Tree Verified:   %s (zero commits during 60-frame animation)\n",
           m->retained_tree_verified ? "YES" : "NO");
    printf("  Elapsed Time:             %.3f s (%.1f FPS)\n", m->elapsed_seconds, m->fps);
    if (m->resize_attempted) {
        printf("  Resize Test:              attempted=YES, succeeded=%s (hr=0x%08lx)\n",
               m->resize_succeeded ? "YES" : "NO", (unsigned long)m->resize_hr);
    }
}

static void print_json(const G2ProbeMetrics *m)
{
    printf("{\n");
    printf("  \"d3d11_device_created\": %s,\n", m->d3d11_device_created ? "true" : "false");
    printf("  \"d3d11_hr\": \"0x%08lx\",\n", (unsigned long)m->d3d11_hr);
    printf("  \"driver_type\": \"%s\",\n", m->driver_type);
    printf("  \"feature_level\": \"%s\",\n", m->feature_level);
    printf("  \"adapter_description\": \"%s\",\n", m->adapter_description);
    printf("  \"vendor_id\": %u,\n", m->vendor_id);
    printf("  \"device_id\": %u,\n", m->device_id);
    printf("  \"parent_hwnd_created\": %s,\n", m->parent_hwnd_created ? "true" : "false");
    printf("  \"child_hwnd_created\": %s,\n", m->child_hwnd_created ? "true" : "false");
    printf("  \"dcomp_dll_loaded\": %s,\n", m->dcomp_dll_loaded ? "true" : "false");
    printf("  \"dcomp_create_device_hr\": \"0x%08lx\",\n", (unsigned long)m->dcomp_create_device_hr);
    printf("  \"dcomp_entry_point_used\": \"%s\",\n", m->dcomp_entry_point_used);
    printf("  \"composition_swapchain_created\": %s,\n", m->composition_swapchain_created ? "true" : "false");
    printf("  \"composition_swapchain_hr\": \"0x%08lx\",\n", (unsigned long)m->composition_swapchain_hr);
    printf("  \"dcomp_target_created\": %s,\n", m->dcomp_target_created ? "true" : "false");
    printf("  \"dcomp_target_hr\": \"0x%08lx\",\n", (unsigned long)m->dcomp_target_hr);
    printf("  \"dcomp_visual_created\": %s,\n", m->dcomp_visual_created ? "true" : "false");
    printf("  \"dcomp_visual_hr\": \"0x%08lx\",\n", (unsigned long)m->dcomp_visual_hr);
    printf("  \"set_content_hr\": \"0x%08lx\",\n", (unsigned long)m->set_content_hr);
    printf("  \"set_root_hr\": \"0x%08lx\",\n", (unsigned long)m->set_root_hr);
    printf("  \"commit_hr\": \"0x%08lx\",\n", (unsigned long)m->commit_hr);
    printf("  \"commit_count\": %u,\n", m->commit_count);
    printf("  \"frames_rendered\": %u,\n", m->frames_rendered);
    printf("  \"frames_presented\": %u,\n", m->frames_presented);
    printf("  \"last_present_hr\": \"0x%08lx\",\n", (unsigned long)m->last_present_hr);
    printf("  \"retained_tree_verified\": %s,\n", m->retained_tree_verified ? "true" : "false");
    printf("  \"resize_attempted\": %s,\n", m->resize_attempted ? "true" : "false");
    printf("  \"resize_succeeded\": %s,\n", m->resize_succeeded ? "true" : "false");
    printf("  \"resize_hr\": \"0x%08lx\",\n", (unsigned long)m->resize_hr);
    printf("  \"elapsed_seconds\": %.4f,\n", m->elapsed_seconds);
    printf("  \"fps\": %.2f\n", m->fps);
    printf("}\n");
}

int main(int argc, char **argv)
{
    G2ProbeConfig cfg;
    cfg.frame_count = 60;
    cfg.width = 800;
    cfg.height = 600;
    cfg.headless = FALSE;
    cfg.json_output = FALSE;
    cfg.test_resize = TRUE;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--json") == 0) {
            cfg.json_output = TRUE;
        } else if (strcmp(argv[i], "--headless") == 0) {
            cfg.headless = TRUE;
        } else if (strcmp(argv[i], "--no-resize") == 0) {
            cfg.test_resize = FALSE;
        } else if (strncmp(argv[i], "--frames=", 9) == 0) {
            cfg.frame_count = (UINT)atoi(argv[i] + 9);
        }
    }

    G2ProbeMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));

    run_g2_probe(&cfg, &metrics);

    if (cfg.json_output) {
        print_json(&metrics);
    } else {
        print_human(&metrics);
    }

    return (metrics.d3d11_device_created &&
            metrics.composition_swapchain_created &&
            metrics.dcomp_target_created &&
            metrics.dcomp_visual_created &&
            SUCCEEDED(metrics.commit_hr) &&
            metrics.retained_tree_verified &&
            (!cfg.test_resize || metrics.resize_succeeded)) ? 0 : 1;
}
