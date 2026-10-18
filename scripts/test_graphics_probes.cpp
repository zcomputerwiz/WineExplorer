// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Native Win32/PE graphics diagnostic probe for WineExplorer.
 * Tests D3D11 HWND swapchains, DirectComposition entry points,
 * composition swapchains, and cross-resource sharing/fences.
 *
 * Designed to compile cleanly under both x86_64-w64-mingw32-g++
 * and i686-w64-mingw32-g++ with no proprietary dependencies.
 */

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef HRESULT (WINAPI *PFN_DCOMPOSITION_CREATE_DEVICE)(
    IDXGIDevice *dxgiDevice, REFIID iid, void **dcompositionDevice);
typedef HRESULT (WINAPI *PFN_DCOMPOSITION_CREATE_DEVICE2)(
    IUnknown *renderingDevice, REFIID iid, void **dcompositionDesktopDevice);
typedef HRESULT (WINAPI *PFN_DCOMPOSITION_CREATE_DEVICE3)(
    IUnknown *renderingDevice, REFIID iid, void **dcompositionDesktopDevice);

struct ProbeResults {
    // Probe 1: D3D11 HWND SwapChain
    BOOL d3d11_device_created;
    HRESULT d3d11_hr;
    char d3d11_driver_type[32];
    char d3d11_feature_level[32];
    char adapter_description[128];
    UINT vendor_id;
    UINT device_id;
    SIZE_T dedicated_vram;
    BOOL swapchain_created;
    HRESULT swapchain_hr;
    BOOL present_succeeded;
    HRESULT present_hr;

    // Probe 2: DirectComposition & Composition SwapChain
    BOOL dcomp_dll_loaded;
    BOOL dcomp_create_device_exported;
    HRESULT dcomp_create_device_hr;
    BOOL dcomp_create_device2_exported;
    HRESULT dcomp_create_device2_hr;
    BOOL dcomp_create_device3_exported;
    HRESULT dcomp_create_device3_hr;
    BOOL dxgi_factory2_available;
    BOOL composition_swapchain_attempted;
    HRESULT composition_swapchain_hr;

    // Probe 3: Shared Resources & Synchronization
    HRESULT shared_nt_texture_hr;
    HRESULT create_shared_handle_hr;
    HRESULT open_shared_resource1_hr;
    HRESULT keyed_mutex_texture_hr;
    HRESULT keyed_mutex_acquire_hr;
    HRESULT keyed_mutex_release_hr;
    HRESULT d3d11_device5_query_hr;
    HRESULT create_fence_hr;
};

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

static LRESULT CALLBACK dummy_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND create_probe_window(void)
{
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = dummy_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "WEX_GraphicsProbe_Wnd";
    RegisterClassExA(&wc);

    return CreateWindowExA(0, wc.lpszClassName, "Graphics Probe Window",
                           WS_OVERLAPPEDWINDOW, 0, 0, 320, 240,
                           NULL, NULL, wc.hInstance, NULL);
}

static void run_probe1_d3d11(HWND hwnd, ProbeResults *r, ID3D11Device **out_dev, IDXGIFactory2 **out_fac2)
{
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

    // Try HARDWARE first
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                   0, levels, sizeof(levels) / sizeof(levels[0]),
                                   D3D11_SDK_VERSION, &dev, &obtained_level, &ctx);
    if (SUCCEEDED(hr)) {
        r->d3d11_device_created = TRUE;
        r->d3d11_hr = hr;
        strncpy(r->d3d11_driver_type, "HARDWARE", sizeof(r->d3d11_driver_type) - 1);
        strncpy(r->d3d11_feature_level, feature_level_to_str(obtained_level), sizeof(r->d3d11_feature_level) - 1);
    } else {
        // Fallback to WARP
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL,
                               0, levels, sizeof(levels) / sizeof(levels[0]),
                               D3D11_SDK_VERSION, &dev, &obtained_level, &ctx);
        if (SUCCEEDED(hr)) {
            r->d3d11_device_created = TRUE;
            r->d3d11_hr = hr;
            strncpy(r->d3d11_driver_type, "WARP", sizeof(r->d3d11_driver_type) - 1);
            strncpy(r->d3d11_feature_level, feature_level_to_str(obtained_level), sizeof(r->d3d11_feature_level) - 1);
        } else {
            r->d3d11_device_created = FALSE;
            r->d3d11_hr = hr;
            strncpy(r->d3d11_driver_type, "NONE", sizeof(r->d3d11_driver_type) - 1);
            strncpy(r->d3d11_feature_level, "NONE", sizeof(r->d3d11_feature_level) - 1);
            return;
        }
    }

    // Query DXGI device and adapter information
    IDXGIDevice *dxgi_dev = NULL;
    hr = dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev);
    if (SUCCEEDED(hr)) {
        IDXGIAdapter *adapter = NULL;
        hr = dxgi_dev->GetAdapter(&adapter);
        if (SUCCEEDED(hr)) {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                    r->adapter_description, sizeof(r->adapter_description) - 1,
                                    NULL, NULL);
                r->vendor_id = desc.VendorId;
                r->device_id = desc.DeviceId;
                r->dedicated_vram = desc.DedicatedVideoMemory;
            }

            IDXGIFactory *factory = NULL;
            hr = adapter->GetParent(__uuidof(IDXGIFactory), (void **)&factory);
            if (SUCCEEDED(hr)) {
                // Check if IDXGIFactory2 is available
                IDXGIFactory2 *factory2 = NULL;
                if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory2), (void **)&factory2))) {
                    r->dxgi_factory2_available = TRUE;
                    if (out_fac2) *out_fac2 = factory2;
                }

                // Create standard HWND swapchain
                DXGI_SWAP_CHAIN_DESC sc_desc;
                memset(&sc_desc, 0, sizeof(sc_desc));
                sc_desc.BufferDesc.Width = 320;
                sc_desc.BufferDesc.Height = 240;
                sc_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                sc_desc.SampleDesc.Count = 1;
                sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                sc_desc.BufferCount = 1;
                sc_desc.OutputWindow = hwnd;
                sc_desc.Windowed = TRUE;
                sc_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

                IDXGISwapChain *sc = NULL;
                hr = factory->CreateSwapChain(dev, &sc_desc, &sc);
                r->swapchain_hr = hr;
                if (SUCCEEDED(hr)) {
                    r->swapchain_created = TRUE;
                    hr = sc->Present(0, 0);
                    r->present_hr = hr;
                    r->present_succeeded = SUCCEEDED(hr);
                    sc->Release();
                }
                factory->Release();
            }
            adapter->Release();
        }
        dxgi_dev->Release();
    }

    if (ctx) ctx->Release();
    if (out_dev) {
        *out_dev = dev;
    } else {
        dev->Release();
    }
}

static void run_probe2_dcomp(ID3D11Device *dev, IDXGIFactory2 *fac2, ProbeResults *r)
{
    HMODULE hDcomp = LoadLibraryA("dcomp.dll");
    if (!hDcomp) {
        r->dcomp_dll_loaded = FALSE;
        return;
    }
    r->dcomp_dll_loaded = TRUE;

    PFN_DCOMPOSITION_CREATE_DEVICE pfn1 =
        (PFN_DCOMPOSITION_CREATE_DEVICE)(void *)GetProcAddress(hDcomp, "DCompositionCreateDevice");
    PFN_DCOMPOSITION_CREATE_DEVICE2 pfn2 =
        (PFN_DCOMPOSITION_CREATE_DEVICE2)(void *)GetProcAddress(hDcomp, "DCompositionCreateDevice2");
    PFN_DCOMPOSITION_CREATE_DEVICE3 pfn3 =
        (PFN_DCOMPOSITION_CREATE_DEVICE3)(void *)GetProcAddress(hDcomp, "DCompositionCreateDevice3");

    r->dcomp_create_device_exported = (pfn1 != NULL);
    r->dcomp_create_device2_exported = (pfn2 != NULL);
    r->dcomp_create_device3_exported = (pfn3 != NULL);

    IDXGIDevice *dxgi_dev = NULL;
    if (dev) {
        dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev);
    }

    if (pfn1) {
        void *dcomp_dev = NULL;
        r->dcomp_create_device_hr = pfn1(dxgi_dev, __uuidof(IDCompositionDevice), &dcomp_dev);
        if (SUCCEEDED(r->dcomp_create_device_hr) && dcomp_dev) {
            ((IUnknown *)dcomp_dev)->Release();
        }
    } else {
        r->dcomp_create_device_hr = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    if (pfn2) {
        void *dcomp_dev2 = NULL;
        r->dcomp_create_device2_hr = pfn2((IUnknown *)dxgi_dev, __uuidof(IDCompositionDesktopDevice), &dcomp_dev2);
        if (SUCCEEDED(r->dcomp_create_device2_hr) && dcomp_dev2) {
            ((IUnknown *)dcomp_dev2)->Release();
        }
    } else {
        r->dcomp_create_device2_hr = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    if (pfn3) {
        void *dcomp_dev3 = NULL;
        r->dcomp_create_device3_hr = pfn3((IUnknown *)dxgi_dev, __uuidof(IDCompositionDesktopDevice), &dcomp_dev3);
        if (SUCCEEDED(r->dcomp_create_device3_hr) && dcomp_dev3) {
            ((IUnknown *)dcomp_dev3)->Release();
        }
    } else {
        r->dcomp_create_device3_hr = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }

    if (dxgi_dev) dxgi_dev->Release();

    // Probe composition swapchain on IDXGIFactory2
    if (fac2 && dev) {
        r->composition_swapchain_attempted = TRUE;
        DXGI_SWAP_CHAIN_DESC1 desc1;
        memset(&desc1, 0, sizeof(desc1));
        desc1.Width = 320;
        desc1.Height = 240;
        desc1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc1.SampleDesc.Count = 1;
        desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc1.BufferCount = 2;
        desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

        IDXGISwapChain1 *comp_sc = NULL;
        r->composition_swapchain_hr = fac2->CreateSwapChainForComposition(dev, &desc1, NULL, &comp_sc);
        if (SUCCEEDED(r->composition_swapchain_hr) && comp_sc) {
            comp_sc->Release();
        }
    } else {
        r->composition_swapchain_attempted = FALSE;
        r->composition_swapchain_hr = E_NOINTERFACE;
    }

    FreeLibrary(hDcomp);
}

static void run_probe3_sharing(ID3D11Device *dev, ProbeResults *r)
{
    if (!dev) {
        r->shared_nt_texture_hr = E_POINTER;
        r->create_shared_handle_hr = E_POINTER;
        r->open_shared_resource1_hr = E_POINTER;
        r->keyed_mutex_texture_hr = E_POINTER;
        r->keyed_mutex_acquire_hr = E_POINTER;
        r->keyed_mutex_release_hr = E_POINTER;
        r->d3d11_device5_query_hr = E_POINTER;
        r->create_fence_hr = E_POINTER;
        return;
    }

    // Test 3A: Texture with D3D11_RESOURCE_MISC_SHARED_NTHANDLE
    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof(td));
    td.Width = 256;
    td.Height = 256;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;

    ID3D11Texture2D *tex_nt = NULL;
    r->shared_nt_texture_hr = dev->CreateTexture2D(&td, NULL, &tex_nt);
    HANDLE hShared = NULL;
    if (SUCCEEDED(r->shared_nt_texture_hr) && tex_nt) {
        IDXGIResource1 *res1 = NULL;
        HRESULT hr = tex_nt->QueryInterface(__uuidof(IDXGIResource1), (void **)&res1);
        if (SUCCEEDED(hr)) {
            r->create_shared_handle_hr = res1->CreateSharedHandle(
                NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &hShared);
            res1->Release();
        } else {
            r->create_shared_handle_hr = hr;
        }

        ID3D11Device1 *dev1 = NULL;
        hr = dev->QueryInterface(__uuidof(ID3D11Device1), (void **)&dev1);
        if (SUCCEEDED(hr)) {
            if (hShared) {
                ID3D11Texture2D *opened_tex = NULL;
                r->open_shared_resource1_hr = dev1->OpenSharedResource1(
                    hShared, __uuidof(ID3D11Texture2D), (void **)&opened_tex);
                if (SUCCEEDED(r->open_shared_resource1_hr) && opened_tex) {
                    opened_tex->Release();
                }
            } else {
                r->open_shared_resource1_hr = HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
            }
            dev1->Release();
        } else {
            r->open_shared_resource1_hr = hr;
        }

        if (hShared) CloseHandle(hShared);
        tex_nt->Release();
    } else {
        r->create_shared_handle_hr = r->shared_nt_texture_hr;
        r->open_shared_resource1_hr = r->shared_nt_texture_hr;
    }

    // Test 3B: Keyed Mutex
    D3D11_TEXTURE2D_DESC km_desc = td;
    km_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ID3D11Texture2D *tex_km = NULL;
    r->keyed_mutex_texture_hr = dev->CreateTexture2D(&km_desc, NULL, &tex_km);
    if (SUCCEEDED(r->keyed_mutex_texture_hr) && tex_km) {
        IDXGIKeyedMutex *km = NULL;
        HRESULT hr = tex_km->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **)&km);
        if (SUCCEEDED(hr)) {
            r->keyed_mutex_acquire_hr = km->AcquireSync(0, 500);
            if (SUCCEEDED(r->keyed_mutex_acquire_hr)) {
                r->keyed_mutex_release_hr = km->ReleaseSync(1);
            } else {
                r->keyed_mutex_release_hr = r->keyed_mutex_acquire_hr;
            }
            km->Release();
        } else {
            r->keyed_mutex_acquire_hr = hr;
            r->keyed_mutex_release_hr = hr;
        }
        tex_km->Release();
    } else {
        r->keyed_mutex_acquire_hr = r->keyed_mutex_texture_hr;
        r->keyed_mutex_release_hr = r->keyed_mutex_texture_hr;
    }

    // Test 3C: D3D11 Device5 Fence
    ID3D11Device5 *dev5 = NULL;
    r->d3d11_device5_query_hr = dev->QueryInterface(__uuidof(ID3D11Device5), (void **)&dev5);
    if (SUCCEEDED(r->d3d11_device5_query_hr) && dev5) {
        ID3D11Fence *fence = NULL;
        r->create_fence_hr = dev5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), (void **)&fence);
        if (SUCCEEDED(r->create_fence_hr) && fence) {
            fence->Release();
        }
        dev5->Release();
    } else {
        r->create_fence_hr = r->d3d11_device5_query_hr;
    }
}

static void print_human(const ProbeResults *r)
{
    printf("=== WineExplorer Graphics Diagnostic Probes ===\n");
    printf("[Probe 1: D3D11 & HWND SwapChain]\n");
    printf("  Device Created:    %s (hr=0x%08lx, type=%s, fl=%s)\n",
           r->d3d11_device_created ? "YES" : "NO", (unsigned long)r->d3d11_hr,
           r->d3d11_driver_type, r->d3d11_feature_level);
    printf("  Adapter:           %s (Vendor=0x%04x, Device=0x%04x, VRAM=%llu MB)\n",
           r->adapter_description, r->vendor_id, r->device_id,
           (unsigned long long)(r->dedicated_vram / (1024 * 1024)));
    printf("  HWND SwapChain:    %s (hr=0x%08lx)\n",
           r->swapchain_created ? "YES" : "NO", (unsigned long)r->swapchain_hr);
    printf("  Present:           %s (hr=0x%08lx)\n",
           r->present_succeeded ? "YES" : "NO", (unsigned long)r->present_hr);

    printf("\n[Probe 2: DirectComposition & Composition SwapChain]\n");
    printf("  dcomp.dll Loaded:  %s\n", r->dcomp_dll_loaded ? "YES" : "NO");
    printf("  DCompositionCreateDevice:  exported=%s hr=0x%08lx\n",
           r->dcomp_create_device_exported ? "YES" : "NO", (unsigned long)r->dcomp_create_device_hr);
    printf("  DCompositionCreateDevice2: exported=%s hr=0x%08lx\n",
           r->dcomp_create_device2_exported ? "YES" : "NO", (unsigned long)r->dcomp_create_device2_hr);
    printf("  DCompositionCreateDevice3: exported=%s hr=0x%08lx\n",
           r->dcomp_create_device3_exported ? "YES" : "NO", (unsigned long)r->dcomp_create_device3_hr);
    printf("  Factory2 Available:        %s\n", r->dxgi_factory2_available ? "YES" : "NO");
    printf("  Composition SwapChain:     attempted=%s hr=0x%08lx\n",
           r->composition_swapchain_attempted ? "YES" : "NO", (unsigned long)r->composition_swapchain_hr);

    printf("\n[Probe 3: Shared Resources & Synchronization]\n");
    printf("  CreateTexture2D (NT Handle): hr=0x%08lx\n", (unsigned long)r->shared_nt_texture_hr);
    printf("  CreateSharedHandle (NT):     hr=0x%08lx\n", (unsigned long)r->create_shared_handle_hr);
    printf("  OpenSharedResource1:         hr=0x%08lx\n", (unsigned long)r->open_shared_resource1_hr);
    printf("  CreateTexture2D (KeyedMutex):hr=0x%08lx\n", (unsigned long)r->keyed_mutex_texture_hr);
    printf("  KeyedMutex Acquire/Release:  acquire=0x%08lx release=0x%08lx\n",
           (unsigned long)r->keyed_mutex_acquire_hr, (unsigned long)r->keyed_mutex_release_hr);
    printf("  ID3D11Device5 Query:         hr=0x%08lx\n", (unsigned long)r->d3d11_device5_query_hr);
    printf("  CreateFence:                 hr=0x%08lx\n", (unsigned long)r->create_fence_hr);
    printf("===============================================\n");
}

static void print_json(const ProbeResults *r)
{
    printf("{\n");
    printf("  \"probe1_d3d11\": {\n");
    printf("    \"device_created\": %s,\n", r->d3d11_device_created ? "true" : "false");
    printf("    \"hr\": \"0x%08lx\",\n", (unsigned long)r->d3d11_hr);
    printf("    \"driver_type\": \"%s\",\n", r->d3d11_driver_type);
    printf("    \"feature_level\": \"%s\",\n", r->d3d11_feature_level);
    printf("    \"adapter\": \"%s\",\n", r->adapter_description);
    printf("    \"vendor_id\": %u,\n", r->vendor_id);
    printf("    \"device_id\": %u,\n", r->device_id);
    printf("    \"dedicated_vram_bytes\": %llu,\n", (unsigned long long)r->dedicated_vram);
    printf("    \"swapchain_created\": %s,\n", r->swapchain_created ? "true" : "false");
    printf("    \"swapchain_hr\": \"0x%08lx\",\n", (unsigned long)r->swapchain_hr);
    printf("    \"present_succeeded\": %s,\n", r->present_succeeded ? "true" : "false");
    printf("    \"present_hr\": \"0x%08lx\"\n", (unsigned long)r->present_hr);
    printf("  },\n");

    printf("  \"probe2_dcomp\": {\n");
    printf("    \"dll_loaded\": %s,\n", r->dcomp_dll_loaded ? "true" : "false");
    printf("    \"dcomp_create_device_exported\": %s,\n", r->dcomp_create_device_exported ? "true" : "false");
    printf("    \"dcomp_create_device_hr\": \"0x%08lx\",\n", (unsigned long)r->dcomp_create_device_hr);
    printf("    \"dcomp_create_device2_exported\": %s,\n", r->dcomp_create_device2_exported ? "true" : "false");
    printf("    \"dcomp_create_device2_hr\": \"0x%08lx\",\n", (unsigned long)r->dcomp_create_device2_hr);
    printf("    \"dcomp_create_device3_exported\": %s,\n", r->dcomp_create_device3_exported ? "true" : "false");
    printf("    \"dcomp_create_device3_hr\": \"0x%08lx\",\n", (unsigned long)r->dcomp_create_device3_hr);
    printf("    \"factory2_available\": %s,\n", r->dxgi_factory2_available ? "true" : "false");
    printf("    \"composition_swapchain_attempted\": %s,\n", r->composition_swapchain_attempted ? "true" : "false");
    printf("    \"composition_swapchain_hr\": \"0x%08lx\"\n", (unsigned long)r->composition_swapchain_hr);
    printf("  },\n");

    printf("  \"probe3_sharing\": {\n");
    printf("    \"shared_nt_texture_hr\": \"0x%08lx\",\n", (unsigned long)r->shared_nt_texture_hr);
    printf("    \"create_shared_handle_hr\": \"0x%08lx\",\n", (unsigned long)r->create_shared_handle_hr);
    printf("    \"open_shared_resource1_hr\": \"0x%08lx\",\n", (unsigned long)r->open_shared_resource1_hr);
    printf("    \"keyed_mutex_texture_hr\": \"0x%08lx\",\n", (unsigned long)r->keyed_mutex_texture_hr);
    printf("    \"keyed_mutex_acquire_hr\": \"0x%08lx\",\n", (unsigned long)r->keyed_mutex_acquire_hr);
    printf("    \"keyed_mutex_release_hr\": \"0x%08lx\",\n", (unsigned long)r->keyed_mutex_release_hr);
    printf("    \"d3d11_device5_query_hr\": \"0x%08lx\",\n", (unsigned long)r->d3d11_device5_query_hr);
    printf("    \"create_fence_hr\": \"0x%08lx\"\n", (unsigned long)r->create_fence_hr);
    printf("  }\n");
    printf("}\n");
}

int main(int argc, char **argv)
{
    BOOL output_json = FALSE;
    BOOL run_all = TRUE;
    BOOL do_probe1 = FALSE, do_probe2 = FALSE, do_probe3 = FALSE;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--json")) output_json = TRUE;
        else if (!strcmp(argv[i], "--d3d11")) { run_all = FALSE; do_probe1 = TRUE; }
        else if (!strcmp(argv[i], "--dcomp")) { run_all = FALSE; do_probe2 = TRUE; }
        else if (!strcmp(argv[i], "--sharing")) { run_all = FALSE; do_probe3 = TRUE; }
    }
    if (run_all) {
        do_probe1 = do_probe2 = do_probe3 = TRUE;
    }

    ProbeResults results;
    memset(&results, 0, sizeof(results));
    HWND hwnd = create_probe_window();
    ID3D11Device *dev = NULL;
    IDXGIFactory2 *fac2 = NULL;

    if (do_probe1 || do_probe2 || do_probe3) {
        run_probe1_d3d11(hwnd, &results, &dev, &fac2);
    }
    if (do_probe2) {
        run_probe2_dcomp(dev, fac2, &results);
    }
    if (do_probe3) {
        run_probe3_sharing(dev, &results);
    }

    if (output_json) {
        print_json(&results);
    } else {
        print_human(&results);
    }

    if (fac2) fac2->Release();
    if (dev) dev->Release();
    if (hwnd) DestroyWindow(hwnd);

    // Return 0 if basic D3D11 device creation passed; diagnostic probes report individual HRs
    return results.d3d11_device_created ? 0 : 1;
}
