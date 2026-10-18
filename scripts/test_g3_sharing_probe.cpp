// SPDX-License-Identifier: GPL-2.0-or-later
/* Multi-process D3D11 shared resource and fence synchronization diagnostic probe.
 *
 * Verifies Phase G3 objectives:
 * 1. D3D11 Shared NT Handles (D3D11_RESOURCE_MISC_SHARED_NTHANDLE, CreateSharedHandle, OpenSharedResource1).
 * 2. Cross-device and cross-process import and handle duplication.
 * 3. Cross-process D3D11 Monitored Fences (ID3D11Device5::CreateFence, OpenSharedFence, ID3D11Fence,
 *    SetEventOnCompletion, ID3D11DeviceContext4::Signal).
 * 4. Cross-process rendering completion and child pixel readback verification.
 * 5. DXGI Keyed Mutex synchronization (timeout on key mismatch, AcquireSync, ReleaseSync).
 */

#define INITGUID
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

struct G3ProbeResults {
    bool parent_device_created;
    bool shared_texture_created;
    bool tex_shared_handle_created;
    bool cross_device_open_succeeded;
    bool shared_fence_created;
    bool fence_shared_handle_created;
    bool child_spawned;
    bool fence_wait_succeeded;
    uint64_t fence_completed_value;
    DWORD child_exit_code;
    bool child_render_verified;
    bool keyed_mutex_verified;
    bool keyed_mutex_timeout_verified;
    HRESULT error_hr;
};

static int run_child(HANDLE hTex, HANDLE hFence) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 10;

    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    D3D_FEATURE_LEVEL feature_level;

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                           levels, sizeof(levels) / sizeof(levels[0]),
                           D3D11_SDK_VERSION, &dev, &feature_level, &ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0,
                               levels, sizeof(levels) / sizeof(levels[0]),
                               D3D11_SDK_VERSION, &dev, &feature_level, &ctx);
        if (FAILED(hr)) return 11;
    }

    ID3D11Device1 *dev1 = NULL;
    hr = dev->QueryInterface(__uuidof(ID3D11Device1), (void **)&dev1);
    if (FAILED(hr) || !dev1) return 12;

    ID3D11Device5 *dev5 = NULL;
    hr = dev->QueryInterface(__uuidof(ID3D11Device5), (void **)&dev5);
    if (FAILED(hr) || !dev5) return 13;

    ID3D11DeviceContext4 *ctx4 = NULL;
    hr = ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&ctx4);
    if (FAILED(hr) || !ctx4) return 14;

    ID3D11Texture2D *child_tex = NULL;
    hr = dev1->OpenSharedResource1(hTex, __uuidof(ID3D11Texture2D), (void **)&child_tex);
    if (FAILED(hr) || !child_tex) return 15;

    ID3D11Fence *child_fence = NULL;
    hr = dev5->OpenSharedFence(hFence, __uuidof(ID3D11Fence), (void **)&child_fence);
    if (FAILED(hr) || !child_fence) return 16;

    ID3D11RenderTargetView *rtv = NULL;
    hr = dev->CreateRenderTargetView((ID3D11Resource *)child_tex, NULL, &rtv);
    if (FAILED(hr) || !rtv) return 17;

    // Render solid green: R=0.0, G=1.0, B=0.0, A=1.0
    float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    ctx->ClearRenderTargetView(rtv, green);

    // Verify rendered pixels on child_tex
    D3D11_TEXTURE2D_DESC std;
    memset(&std, 0, sizeof(std));
    std.Width = 256;
    std.Height = 256;
    std.MipLevels = 1;
    std.ArraySize = 1;
    std.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std.SampleDesc.Count = 1;
    std.Usage = D3D11_USAGE_STAGING;
    std.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D *staging_tex = NULL;
    hr = dev->CreateTexture2D(&std, NULL, &staging_tex);
    if (FAILED(hr) || !staging_tex) return 19;

    ctx->CopyResource((ID3D11Resource *)staging_tex, (ID3D11Resource *)child_tex);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = ctx->Map((ID3D11Resource *)staging_tex, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr) || !mapped.pData) return 20;

    const uint8_t *row = (const uint8_t *)mapped.pData + 128 * mapped.RowPitch;
    const uint8_t *pixel = row + 128 * 4; // R, G, B, A
    bool green_ok = (pixel[1] >= 250 && pixel[0] <= 5 && pixel[2] <= 5);
    ctx->Unmap((ID3D11Resource *)staging_tex, 0);
    staging_tex->Release();

    if (!green_ok) return 21;

    // Signal fence to value 1
    hr = ctx4->Signal(child_fence, 1);
    if (FAILED(hr)) return 18;

    rtv->Release();
    child_fence->Release();
    child_tex->Release();
    ctx4->Release();
    dev5->Release();
    dev1->Release();
    ctx->Release();
    dev->Release();
    CoUninitialize();

    return 0;
}

static void print_json(const G3ProbeResults &r) {
    printf("{\n");
    printf("  \"parent_device_created\": %s,\n", r.parent_device_created ? "true" : "false");
    printf("  \"shared_texture_created\": %s,\n", r.shared_texture_created ? "true" : "false");
    printf("  \"tex_shared_handle_created\": %s,\n", r.tex_shared_handle_created ? "true" : "false");
    printf("  \"cross_device_open_succeeded\": %s,\n", r.cross_device_open_succeeded ? "true" : "false");
    printf("  \"shared_fence_created\": %s,\n", r.shared_fence_created ? "true" : "false");
    printf("  \"fence_shared_handle_created\": %s,\n", r.fence_shared_handle_created ? "true" : "false");
    printf("  \"child_spawned\": %s,\n", r.child_spawned ? "true" : "false");
    printf("  \"fence_wait_succeeded\": %s,\n", r.fence_wait_succeeded ? "true" : "false");
    printf("  \"fence_completed_value\": %llu,\n", (unsigned long long)r.fence_completed_value);
    printf("  \"child_exit_code\": %lu,\n", (unsigned long)r.child_exit_code);
    printf("  \"child_render_verified\": %s,\n", r.child_render_verified ? "true" : "false");
    printf("  \"keyed_mutex_verified\": %s,\n", r.keyed_mutex_verified ? "true" : "false");
    printf("  \"keyed_mutex_timeout_verified\": %s,\n", r.keyed_mutex_timeout_verified ? "true" : "false");
    printf("  \"error_hr\": \"0x%08lx\"\n", (unsigned long)r.error_hr);
    printf("}\n");
}

int main(int argc, char **argv) {
    if (argc >= 4 && strcmp(argv[1], "--child") == 0) {
        HANDLE hTex = (HANDLE)(uintptr_t)strtoull(argv[2], NULL, 0);
        HANDLE hFence = (HANDLE)(uintptr_t)strtoull(argv[3], NULL, 0);
        return run_child(hTex, hFence);
    }

    bool output_json = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--json") == 0) output_json = true;
    }

    G3ProbeResults res;
    memset(&res, 0, sizeof(res));

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        res.error_hr = hr;
        if (output_json) print_json(res);
        return 1;
    }

    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    D3D_FEATURE_LEVEL feature_level;

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                           levels, sizeof(levels) / sizeof(levels[0]),
                           D3D11_SDK_VERSION, &dev, &feature_level, &ctx);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0,
                               levels, sizeof(levels) / sizeof(levels[0]),
                               D3D11_SDK_VERSION, &dev, &feature_level, &ctx);
        if (FAILED(hr)) {
            res.error_hr = hr;
            if (output_json) print_json(res);
            return 2;
        }
    }
    res.parent_device_created = true;

    ID3D11Device5 *dev5 = NULL;
    hr = dev->QueryInterface(__uuidof(ID3D11Device5), (void **)&dev5);
    if (FAILED(hr) || !dev5) {
        res.error_hr = hr;
        if (output_json) print_json(res);
        return 3;
    }

    // Keyed Mutex Test: Validate lock ownership, key mismatch timeout, and release key transitions
    D3D11_TEXTURE2D_DESC km_td;
    memset(&km_td, 0, sizeof(km_td));
    km_td.Width = 64;
    km_td.Height = 64;
    km_td.MipLevels = 1;
    km_td.ArraySize = 1;
    km_td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    km_td.SampleDesc.Count = 1;
    km_td.Usage = D3D11_USAGE_DEFAULT;
    km_td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    km_td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    ID3D11Texture2D *km_tex = NULL;
    hr = dev->CreateTexture2D(&km_td, NULL, &km_tex);
    if (SUCCEEDED(hr) && km_tex) {
        IDXGIKeyedMutex *km = NULL;
        hr = km_tex->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **)&km);
        if (SUCCEEDED(hr) && km) {
            // Initial key is 0. Attempting to acquire key 1 must timeout
            HRESULT hr_mismatch = km->AcquireSync(1, 50);
            if (hr_mismatch == WAIT_TIMEOUT || hr_mismatch == DXGI_ERROR_WAIT_TIMEOUT) {
                res.keyed_mutex_timeout_verified = true;
            }

            // Acquire key 0 succeeds, transitions to key 1
            HRESULT hr_acq0 = km->AcquireSync(0, 500);
            HRESULT hr_rel1 = km->ReleaseSync(1);
            HRESULT hr_acq1 = km->AcquireSync(1, 500);
            HRESULT hr_rel0 = km->ReleaseSync(0);
            if (SUCCEEDED(hr_acq0) && SUCCEEDED(hr_rel1) && SUCCEEDED(hr_acq1) && SUCCEEDED(hr_rel0)) {
                res.keyed_mutex_verified = true;
            }
            km->Release();
        }
        km_tex->Release();
    }

    // Create Shared Texture with NT Handle flag
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

    ID3D11Texture2D *shared_tex = NULL;
    hr = dev->CreateTexture2D(&td, NULL, &shared_tex);
    if (FAILED(hr) || !shared_tex) {
        res.error_hr = hr;
        if (output_json) print_json(res);
        return 4;
    }
    res.shared_texture_created = true;

    IDXGIResource1 *res1 = NULL;
    hr = shared_tex->QueryInterface(__uuidof(IDXGIResource1), (void **)&res1);
    if (FAILED(hr) || !res1) {
        res.error_hr = hr;
        if (output_json) print_json(res);
        return 5;
    }

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hTexShared = NULL;
    hr = res1->CreateSharedHandle(&sa, GENERIC_ALL, NULL, &hTexShared);
    res1->Release();
    if (FAILED(hr) || !hTexShared) {
        res.error_hr = hr;
        if (output_json) print_json(res);
        return 6;
    }
    SetHandleInformation(hTexShared, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    res.tex_shared_handle_created = true;

    // Cross-Device import test in parent process
    ID3D11Device *dev2 = NULL;
    ID3D11DeviceContext *ctx2 = NULL;
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                           levels, sizeof(levels) / sizeof(levels[0]),
                           D3D11_SDK_VERSION, &dev2, &feature_level, &ctx2);
    if (SUCCEEDED(hr) && dev2) {
        ID3D11Device1 *dev2_1 = NULL;
        hr = dev2->QueryInterface(__uuidof(ID3D11Device1), (void **)&dev2_1);
        if (SUCCEEDED(hr) && dev2_1) {
            ID3D11Texture2D *cross_tex = NULL;
            hr = dev2_1->OpenSharedResource1(hTexShared, __uuidof(ID3D11Texture2D), (void **)&cross_tex);
            if (SUCCEEDED(hr) && cross_tex) {
                D3D11_TEXTURE2D_DESC ctd;
                cross_tex->GetDesc(&ctd);
                if (ctd.Width == 256 && ctd.Height == 256 && ctd.Format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                    res.cross_device_open_succeeded = true;
                }
                cross_tex->Release();
            }
            dev2_1->Release();
        }
        if (ctx2) ctx2->Release();
        dev2->Release();
    }

    // Create Shared Fence
    ID3D11Fence *fence = NULL;
    hr = dev5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void **)&fence);
    if (FAILED(hr) || !fence) {
        res.error_hr = hr;
        if (output_json) print_json(res);
        return 7;
    }
    res.shared_fence_created = true;

    HANDLE hFenceShared = NULL;
    hr = fence->CreateSharedHandle(&sa, GENERIC_ALL, NULL, &hFenceShared);
    if (FAILED(hr) || !hFenceShared) {
        res.error_hr = hr;
        if (output_json) print_json(res);
        return 8;
    }
    SetHandleInformation(hFenceShared, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    res.fence_shared_handle_created = true;

    HANDLE hCompletionEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    hr = fence->SetEventOnCompletion(1, hCompletionEvent);
    if (FAILED(hr)) {
        res.error_hr = hr;
        if (output_json) print_json(res);
        return 9;
    }

    // Spawn child process
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));

    char cmd_line[MAX_PATH * 2];
    snprintf(cmd_line, sizeof(cmd_line), "\"%s\" --child 0x%llx 0x%llx",
             exe_path,
             (unsigned long long)(uintptr_t)hTexShared,
             (unsigned long long)(uintptr_t)hFenceShared);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL proc_created = CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (!proc_created) {
        res.error_hr = HRESULT_FROM_WIN32(GetLastError());
        if (output_json) print_json(res);
        return 10;
    }
    res.child_spawned = true;

    // Wait for fence completion event
    DWORD wait_res = WaitForSingleObject(hCompletionEvent, 10000);
    if (wait_res == WAIT_OBJECT_0) {
        res.fence_wait_succeeded = true;
    }
    res.fence_completed_value = fence->GetCompletedValue();

    // Wait for child process to terminate
    WaitForSingleObject(pi.hProcess, 5000);
    GetExitCodeProcess(pi.hProcess, &res.child_exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (res.child_exit_code == 0) {
        res.child_render_verified = true;
    }

    CloseHandle(hCompletionEvent);
    CloseHandle(hFenceShared);
    CloseHandle(hTexShared);
    fence->Release();
    shared_tex->Release();
    dev5->Release();
    ctx->Release();
    dev->Release();
    CoUninitialize();

    if (output_json) {
        print_json(res);
    } else {
        printf("=== WineExplorer Phase G3 Multi-Process Sharing Diagnostic Probe ===\n");
        printf("  Parent Device:        %s\n", res.parent_device_created ? "OK" : "FAIL");
        printf("  Shared Texture:       %s\n", res.shared_texture_created ? "OK" : "FAIL");
        printf("  Texture NT Handle:    %s\n", res.tex_shared_handle_created ? "OK" : "FAIL");
        printf("  Cross-Device Open:    %s\n", res.cross_device_open_succeeded ? "OK" : "FAIL");
        printf("  Shared Fence:         %s\n", res.shared_fence_created ? "OK" : "FAIL");
        printf("  Fence NT Handle:      %s\n", res.fence_shared_handle_created ? "OK" : "FAIL");
        printf("  Child Spawned:        %s\n", res.child_spawned ? "OK" : "FAIL");
        printf("  Child Exit Code:      %lu\n", (unsigned long)res.child_exit_code);
        printf("  Child Render Verify:  %s\n", res.child_render_verified ? "OK" : "FAIL");
        printf("  Fence Wait Succeeded: %s\n", res.fence_wait_succeeded ? "OK" : "FAIL");
        printf("  Fence Value:          %llu\n", (unsigned long long)res.fence_completed_value);
        printf("  Keyed Mutex Sync:     %s\n", res.keyed_mutex_verified ? "OK" : "FAIL");
        printf("  Keyed Mutex Timeout:  %s\n", res.keyed_mutex_timeout_verified ? "OK" : "FAIL");
        printf("====================================================================\n");
    }

    bool success = res.parent_device_created &&
                   res.shared_texture_created &&
                   res.tex_shared_handle_created &&
                   res.cross_device_open_succeeded &&
                   res.shared_fence_created &&
                   res.fence_shared_handle_created &&
                   res.child_spawned &&
                   res.fence_wait_succeeded &&
                   res.fence_completed_value == 1 &&
                   res.child_exit_code == 0 &&
                   res.child_render_verified &&
                   res.keyed_mutex_verified &&
                   res.keyed_mutex_timeout_verified;

    return success ? 0 : 1;
}
