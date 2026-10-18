// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Phase G1 Non-DComp Animated Diagnostic Probe.
 *
 * Simulates Chromium's non-DComp presentation architecture:
 * - A parent top-level application window.
 * - An intermediate child window (matching Chromium's child_window_win.cc).
 * - A DXGI swapchain targeting the child window (image_transport_surface_win.cc).
 * - Continuous frame rendering, backbuffer presentation, and WM_SIZE buffer resizing.
 *
 * Compiles cleanly for both x86_64 and i686 MinGW without proprietary dependencies.
 */

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct G1ProbeConfig {
    UINT frame_count;
    UINT width;
    UINT height;
    BOOL headless;
    BOOL json_output;
    BOOL test_resize;
};

struct G1ProbeMetrics {
    BOOL d3d11_device_created;
    HRESULT d3d11_hr;
    char driver_type[32];
    char feature_level[32];
    char adapter_description[128];
    UINT vendor_id;
    UINT device_id;
    BOOL parent_hwnd_created;
    BOOL child_hwnd_created;
    BOOL swapchain_created;
    HRESULT swapchain_hr;
    UINT frames_rendered;
    UINT frames_presented;
    HRESULT last_present_hr;
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
    wc.lpszClassName = "WEX_G1_ParentWnd";
    RegisterClassExA(&wc);

    WNDCLASSEXA child_wc;
    memset(&child_wc, 0, sizeof(child_wc));
    child_wc.cbSize = sizeof(child_wc);
    child_wc.lpfnWndProc = ChildWndProc;
    child_wc.hInstance = hInstance;
    child_wc.lpszClassName = "WEX_G1_ChildWnd";
    RegisterClassExA(&child_wc);
}

static void run_g1_probe(const G1ProbeConfig *cfg, G1ProbeMetrics *m)
{
    HINSTANCE hInst = GetModuleHandleA(NULL);
    register_classes(hInst);

    DWORD parent_style = cfg->headless ? WS_POPUP : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    g_hParent = CreateWindowExA(0, "WEX_G1_ParentWnd",
                                "Chromium G1 Non-DComp Animated Diagnostic Probe",
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

    // Create intermediate child window (matching Chromium's GPU process child HWND)
    g_hChild = CreateWindowExA(0, "WEX_G1_ChildWnd", NULL,
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

    // Create D3D11 device (HARDWARE first, WARP fallback)
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

    // Query DXGI adapter description
    IDXGIDevice *dxgi_dev = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory *factory = NULL;
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
            adapter->GetParent(__uuidof(IDXGIFactory), (void **)&factory);
            adapter->Release();
        }
        dxgi_dev->Release();
    }

    if (!factory) {
        if (ctx) ctx->Release();
        dev->Release();
        DestroyWindow(g_hChild);
        DestroyWindow(g_hParent);
        return;
    }

    // Create DXGI HWND swapchain on the child HWND (matching image_transport_surface_win.cc)
    DXGI_SWAP_CHAIN_DESC sc_desc;
    memset(&sc_desc, 0, sizeof(sc_desc));
    sc_desc.BufferDesc.Width = client_w;
    sc_desc.BufferDesc.Height = client_h;
    sc_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.BufferDesc.RefreshRate.Numerator = 60;
    sc_desc.BufferDesc.RefreshRate.Denominator = 1;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.BufferCount = 1;
    sc_desc.OutputWindow = g_hChild;
    sc_desc.Windowed = TRUE;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain *sc = NULL;
    hr = factory->CreateSwapChain(dev, &sc_desc, &sc);
    m->swapchain_hr = hr;
    m->swapchain_created = SUCCEEDED(hr);
    factory->Release();

    if (!SUCCEEDED(hr) || !sc) {
        if (ctx) ctx->Release();
        dev->Release();
        DestroyWindow(g_hChild);
        DestroyWindow(g_hParent);
        return;
    }

    // Create RenderTargetView
    ID3D11Texture2D *backbuffer = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backbuffer);
    if (SUCCEEDED(hr) && backbuffer) {
        dev->CreateRenderTargetView(backbuffer, NULL, &rtv);
        backbuffer->Release();
    }

    LARGE_INTEGER freq, t_start, t_end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t_start);

    // Continuous rendering animation loop (simulates browser rendering WebGL/Canvas/UI)
    for (UINT frame = 0; frame < cfg->frame_count; ++frame) {
        // Dispatch pending Win32 messages
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (rtv) {
            // Animate background color smoothly (changing RGB values)
            float t = (float)frame / (float)(cfg->frame_count > 0 ? cfg->frame_count : 1);
            float clear_color[4] = {
                0.2f + 0.6f * t,
                0.3f + 0.4f * (1.0f - t),
                0.7f,
                1.0f
            };
            ctx->ClearRenderTargetView(rtv, clear_color);
            m->frames_rendered++;
        }

        // Present to the child HWND
        hr = sc->Present(0, 0);
        m->last_present_hr = hr;
        if (SUCCEEDED(hr)) {
            m->frames_presented++;
        }

        // Mid-sequence resize test
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

            hr = sc->ResizeBuffers(1, new_w, new_h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
            m->resize_hr = hr;
            m->resize_succeeded = SUCCEEDED(hr);

            if (SUCCEEDED(hr)) {
                ID3D11Texture2D *new_bb = NULL;
                if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&new_bb)) && new_bb) {
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

    if (rtv) rtv->Release();
    sc->Release();
    if (ctx) ctx->Release();
    dev->Release();
    DestroyWindow(g_hChild);
    DestroyWindow(g_hParent);
}

static void print_human(const G1ProbeMetrics *m)
{
    printf("=== Phase G1 Non-DComp Animated Diagnostic Probe ===\n");
    printf("  Device Created:      %s (type=%s, fl=%s)\n",
           m->d3d11_device_created ? "YES" : "NO", m->driver_type, m->feature_level);
    printf("  Adapter Description: %s (Vendor=0x%04x, Device=0x%04x)\n",
           m->adapter_description, m->vendor_id, m->device_id);
    printf("  Parent HWND:         %s\n", m->parent_hwnd_created ? "YES" : "NO");
    printf("  Child HWND:          %s (simulating Chromium child_window_win.cc)\n",
           m->child_hwnd_created ? "YES" : "NO");
    printf("  SwapChain on Child:  %s (hr=0x%08lx)\n",
           m->swapchain_created ? "YES" : "NO", (unsigned long)m->swapchain_hr);
    printf("  Frames Rendered:     %u\n", m->frames_rendered);
    printf("  Frames Presented:    %u (last_hr=0x%08lx)\n",
           m->frames_presented, (unsigned long)m->last_present_hr);
    printf("  Elapsed Time:        %.3f s (%.1f FPS)\n", m->elapsed_seconds, m->fps);
    if (m->resize_attempted) {
        printf("  Resize Test:         attempted=YES, succeeded=%s (hr=0x%08lx)\n",
               m->resize_succeeded ? "YES" : "NO", (unsigned long)m->resize_hr);
    }
    printf("====================================================\n");
}

static void print_json(const G1ProbeMetrics *m)
{
    printf("{\n");
    printf("  \"d3d11_device_created\": %s,\n", m->d3d11_device_created ? "true" : "false");
    printf("  \"driver_type\": \"%s\",\n", m->driver_type);
    printf("  \"feature_level\": \"%s\",\n", m->feature_level);
    printf("  \"adapter\": \"%s\",\n", m->adapter_description);
    printf("  \"vendor_id\": %u,\n", m->vendor_id);
    printf("  \"device_id\": %u,\n", m->device_id);
    printf("  \"parent_hwnd_created\": %s,\n", m->parent_hwnd_created ? "true" : "false");
    printf("  \"child_hwnd_created\": %s,\n", m->child_hwnd_created ? "true" : "false");
    printf("  \"swapchain_created\": %s,\n", m->swapchain_created ? "true" : "false");
    printf("  \"swapchain_hr\": \"0x%08lx\",\n", (unsigned long)m->swapchain_hr);
    printf("  \"frames_rendered\": %u,\n", m->frames_rendered);
    printf("  \"frames_presented\": %u,\n", m->frames_presented);
    printf("  \"last_present_hr\": \"0x%08lx\",\n", (unsigned long)m->last_present_hr);
    printf("  \"resize_attempted\": %s,\n", m->resize_attempted ? "true" : "false");
    printf("  \"resize_succeeded\": %s,\n", m->resize_succeeded ? "true" : "false");
    printf("  \"resize_hr\": \"0x%08lx\",\n", (unsigned long)m->resize_hr);
    printf("  \"elapsed_seconds\": %.4f,\n", m->elapsed_seconds);
    printf("  \"fps\": %.2f\n", m->fps);
    printf("}\n");
}

int main(int argc, char **argv)
{
    G1ProbeConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.frame_count = 60;
    cfg.width = 640;
    cfg.height = 480;
    cfg.headless = FALSE;
    cfg.json_output = FALSE;
    cfg.test_resize = TRUE;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--headless")) cfg.headless = TRUE;
        else if (!strcmp(argv[i], "--json")) cfg.json_output = TRUE;
        else if (!strcmp(argv[i], "--no-resize")) cfg.test_resize = FALSE;
        else if (!strncmp(argv[i], "--frames=", 9)) {
            cfg.frame_count = (UINT)atoi(argv[i] + 9);
        }
    }

    G1ProbeMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));

    run_g1_probe(&cfg, &metrics);

    if (cfg.json_output) {
        print_json(&metrics);
    } else {
        print_human(&metrics);
    }

    BOOL pass = metrics.d3d11_device_created &&
                metrics.child_hwnd_created &&
                metrics.swapchain_created &&
                (metrics.frames_presented == cfg.frame_count) &&
                (!cfg.test_resize || metrics.resize_succeeded);

    return pass ? 0 : 1;
}
