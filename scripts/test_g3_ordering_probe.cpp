// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Original regression probe: ordinary resource copies and a genuinely pending fence.
#define INITGUID
#include <windows.h>
#include <d3d11_4.h>
#include <cstdio>
#include <cstring>

template<typename T> static void release(T *&object)
{
    if (object) object->Release();
    object = NULL;
}

static HRESULT create_device(ID3D11Device **device, ID3D11DeviceContext **context)
{
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, 2,
                                 D3D11_SDK_VERSION, device, &level, context);
    if (FAILED(hr))
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, levels, 2,
                              D3D11_SDK_VERSION, device, &level, context);
    return hr;
}

static bool buffer_copy(ID3D11Device *device, ID3D11DeviceContext *context)
{
    const unsigned int bytes = 256;
    unsigned char input[bytes];
    ID3D11Buffer *source = NULL, *staging = NULL;
    D3D11_BUFFER_DESC desc = {};
    D3D11_SUBRESOURCE_DATA initial = {};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    bool passed = false;

    for (unsigned int i = 0; i < bytes; ++i) input[i] = (i * 37 + 19) & 255;
    desc.ByteWidth = bytes;
    desc.Usage = D3D11_USAGE_DEFAULT;
    initial.pSysMem = input;
    if (FAILED(device->CreateBuffer(&desc, &initial, &source))) goto done;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateBuffer(&desc, NULL, &staging))) goto done;
    context->CopyResource(staging, source);
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) goto done;
    passed = mapped.pData && !memcmp(mapped.pData, input, bytes);
    context->Unmap(staging, 0);
done:
    release(staging);
    release(source);
    return passed;
}

static bool texture1d_copy(ID3D11Device *device, ID3D11DeviceContext *context)
{
    const unsigned int pixels = 16;
    unsigned int input[pixels];
    ID3D11Texture1D *source = NULL, *staging = NULL;
    D3D11_TEXTURE1D_DESC desc = {};
    D3D11_SUBRESOURCE_DATA initial = {};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    bool passed = false;

    for (unsigned int i = 0; i < pixels; ++i) input[i] = 0xff112200 + i;
    desc.Width = pixels;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    initial.pSysMem = input;
    initial.SysMemPitch = sizeof(input);
    if (FAILED(device->CreateTexture1D(&desc, &initial, &source))) goto done;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture1D(&desc, NULL, &staging))) goto done;
    context->CopyResource(staging, source);
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) goto done;
    passed = mapped.pData && !memcmp(mapped.pData, input, sizeof(input));
    context->Unmap(staging, 0);
done:
    release(staging);
    release(source);
    return passed;
}

static bool texture3d_copy(ID3D11Device *device, ID3D11DeviceContext *context)
{
    unsigned int input[4][4][4];
    ID3D11Texture3D *source = NULL, *staging = NULL;
    D3D11_TEXTURE3D_DESC desc = {};
    D3D11_SUBRESOURCE_DATA initial = {};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    bool passed = false;

    for (unsigned int z = 0; z < 4; ++z)
        for (unsigned int y = 0; y < 4; ++y)
            for (unsigned int x = 0; x < 4; ++x)
                input[z][y][x] = 0xff000000 | (z << 16) | (y << 8) | x;
    desc.Width = desc.Height = desc.Depth = 4;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    initial.pSysMem = input;
    initial.SysMemPitch = sizeof(input[0][0]);
    initial.SysMemSlicePitch = sizeof(input[0]);
    if (FAILED(device->CreateTexture3D(&desc, &initial, &source))) goto done;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture3D(&desc, NULL, &staging))) goto done;
    context->CopyResource(staging, source);
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) goto done;
    passed = mapped.pData != NULL;
    if (passed)
        for (unsigned int z = 0; z < 4; ++z)
            for (unsigned int y = 0; y < 4; ++y)
                if (memcmp((const unsigned char *)mapped.pData + z * mapped.DepthPitch
                           + y * mapped.RowPitch, input[z][y], sizeof(input[z][y]))) passed = false;
    context->Unmap(staging, 0);
done:
    release(staging); release(source);
    return passed;
}

static bool pending_wait(ID3D11Device *consumer, ID3D11DeviceContext *context)
{
    ID3D11Device *producer = NULL;
    ID3D11DeviceContext *producer_context = NULL;
    ID3D11Device5 *producer5 = NULL, *consumer5 = NULL;
    ID3D11DeviceContext4 *producer4 = NULL, *consumer4 = NULL;
    ID3D11Fence *producer_fence = NULL, *consumer_fence = NULL;
    ID3D11Texture2D *target = NULL, *staging = NULL;
    ID3D11RenderTargetView *view = NULL;
    ID3D11Query *query = NULL;
    HANDLE shared = NULL;
    D3D11_TEXTURE2D_DESC desc = {};
    D3D11_QUERY_DESC query_desc = {D3D11_QUERY_EVENT, 0};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    BOOL complete = FALSE;
    bool queued = false, passed = false, early = false;
    ULONGLONG start;
    HRESULT hr;

    if (FAILED(create_device(&producer, &producer_context))) goto done;
    if (FAILED(producer->QueryInterface(__uuidof(ID3D11Device5), (void **)&producer5))) goto done;
    if (FAILED(consumer->QueryInterface(__uuidof(ID3D11Device5), (void **)&consumer5))) goto done;
    if (FAILED(producer_context->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&producer4))) goto done;
    if (FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&consumer4))) goto done;
    if (FAILED(producer5->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                    __uuidof(ID3D11Fence), (void **)&producer_fence))) goto done;
    if (FAILED(producer_fence->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &shared))) goto done;
    if (FAILED(consumer5->OpenSharedFence(shared, __uuidof(ID3D11Fence), (void **)&consumer_fence))) goto done;
    desc.Width = desc.Height = 16;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(consumer->CreateTexture2D(&desc, NULL, &target))) goto done;
    if (FAILED(consumer->CreateRenderTargetView(target, NULL, &view))) goto done;
    if (FAILED(consumer->CreateQuery(&query_desc, &query))) goto done;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(consumer->CreateTexture2D(&desc, NULL, &staging))) goto done;

    start = GetTickCount64();
    hr = consumer4->Wait(consumer_fence, 1);
    if (FAILED(hr)) goto done;
    queued = true;
    context->ClearRenderTargetView(view, green);
    context->End(query);
    context->Flush();
    if (GetTickCount64() - start > 250)
    {
        puts("FAIL: pending Wait/Clear/End/Flush blocked the API caller");
        goto done;
    }
    // The old code ignored Wait before Clear and End. Never pre-complete this fence.
    start = GetTickCount64();
    while (GetTickCount64() - start < 200)
    {
        hr = context->GetData(query, &complete, sizeof(complete), 0);
        if (hr == S_OK && complete) {early = true; break;}
        if (FAILED(hr)) goto done;
        Sleep(2);
    }
    if (FAILED(producer4->Signal(producer_fence, 1))) goto done;
    queued = false;
    start = GetTickCount64();
    do
    {
        hr = context->GetData(query, &complete, sizeof(complete), 0);
        if (hr == S_OK && complete) break;
        if (FAILED(hr)) goto done;
        Sleep(2);
    } while (GetTickCount64() - start < 5000);
    if (hr != S_OK || !complete || early)
    {
        printf("FAIL: query completed before release=%d; completion hr=%#lx\n", early, (unsigned long)hr);
        goto done;
    }
    context->CopyResource(staging, target);
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) goto done;
    if (mapped.pData)
    {
        const unsigned char *pixel = (const unsigned char *)mapped.pData;
        passed = pixel[0] <= 5 && pixel[1] >= 250 && pixel[2] <= 5;
    }
    context->Unmap(staging, 0);
done:
    // Always release a successfully queued wait before destroying its device.
    if (queued && producer4 && producer_fence) producer4->Signal(producer_fence, 1);
    release(query); release(view); release(staging); release(target);
    release(consumer_fence); release(producer_fence);
    release(consumer4); release(producer4); release(consumer5); release(producer5);
    release(producer_context); release(producer);
    if (shared) CloseHandle(shared);
    return passed;
}

static bool immediate_release()
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Device5 *device5 = NULL;
    ID3D11DeviceContext4 *context4 = NULL;
    ID3D11Fence *fence = NULL;
    bool passed = false;

    if (FAILED(create_device(&device, &context))) goto done;
    if (FAILED(device->QueryInterface(__uuidof(ID3D11Device5), (void **)&device5))) goto done;
    if (FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&context4))) goto done;
    if (FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), (void **)&fence))) goto done;
    passed = SUCCEEDED(context4->Signal(fence, 1));
done:
    // Do not wait for completion before dropping every public device reference.
    // Queued private references must drain without a self-join or use-after-free.
    release(fence); release(context4); release(device5); release(context); release(device);
    return passed;
}

static bool texture2d_copy(ID3D11Device *device, ID3D11DeviceContext *context)
{
    const unsigned int width = 16, height = 16;
    unsigned int input[height][width];
    ID3D11Texture2D *source = NULL, *staging = NULL;
    D3D11_TEXTURE2D_DESC desc = {};
    D3D11_SUBRESOURCE_DATA initial = {};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    bool passed = false;

    for (unsigned int y = 0; y < height; ++y)
        for (unsigned int x = 0; x < width; ++x)
            input[y][x] = 0xff000000 | (y << 16) | (x << 8) | 0xaa;

    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    initial.pSysMem = input;
    initial.SysMemPitch = width * sizeof(unsigned int);
    if (FAILED(device->CreateTexture2D(&desc, &initial, &source))) goto done;

    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&desc, NULL, &staging))) goto done;

    context->CopyResource(staging, source);
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) goto done;
    passed = mapped.pData != NULL;
    if (passed)
    {
        for (unsigned int y = 0; y < height; ++y)
        {
            if (memcmp((const unsigned char *)mapped.pData + y * mapped.RowPitch,
                       input[y], width * sizeof(unsigned int)))
            {
                passed = false;
                break;
            }
        }
    }
    context->Unmap(staging, 0);
done:
    release(staging);
    release(source);
    return passed;
}

static bool format_layout_rejection(ID3D11Device *device)
{
    ID3D11Texture2D *tex = NULL;
    IDXGIResource1 *res = NULL;
    HANDLE handle = NULL;
    D3D11_TEXTURE2D_DESC desc = {};
    bool passed = true;

    desc.Width = desc.Height = 16;
    desc.MipLevels = 2; // invalid for shared NT handle
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    if (SUCCEEDED(device->CreateTexture2D(&desc, NULL, &tex)))
    {
        if (SUCCEEDED(tex->QueryInterface(__uuidof(IDXGIResource1), (void **)&res)))
        {
            if (SUCCEEDED(res->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &handle)))
            {
                CloseHandle(handle);
                handle = NULL;
                passed = false; // MipLevels > 1 should be rejected
            }
            release(res);
        }
        release(tex);
    }

    // ArraySize > 1
    desc.MipLevels = 1;
    desc.ArraySize = 2;
    if (SUCCEEDED(device->CreateTexture2D(&desc, NULL, &tex)))
    {
        if (SUCCEEDED(tex->QueryInterface(__uuidof(IDXGIResource1), (void **)&res)))
        {
            if (SUCCEEDED(res->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &handle)))
            {
                CloseHandle(handle);
                handle = NULL;
                passed = false; // ArraySize > 1 should be rejected
            }
            release(res);
        }
        release(tex);
    }

    // MSAA SampleDesc.Count > 1
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 4;
    if (SUCCEEDED(device->CreateTexture2D(&desc, NULL, &tex)))
    {
        if (SUCCEEDED(tex->QueryInterface(__uuidof(IDXGIResource1), (void **)&res)))
        {
            if (SUCCEEDED(res->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &handle)))
            {
                CloseHandle(handle);
                handle = NULL;
                passed = false; // MSAA should be rejected
            }
            release(res);
        }
        release(tex);
    }

    // Valid single-subresource RGBA8 must succeed
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (SUCCEEDED(device->CreateTexture2D(&desc, NULL, &tex)))
    {
        if (SUCCEEDED(tex->QueryInterface(__uuidof(IDXGIResource1), (void **)&res)))
        {
            if (FAILED(res->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &handle)))
                passed = false;
            else
            {
                CloseHandle(handle);
                handle = NULL;
            }
            release(res);
        }
        else
            passed = false;
        release(tex);
    }

    // OpenSharedResource1 with invalid handle should return E_INVALIDARG
    ID3D11Device1 *device1 = NULL;
    if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D11Device1), (void **)&device1)))
    {
        ID3D11Texture2D *imported = NULL;
        if (SUCCEEDED(device1->OpenSharedResource1((HANDLE)(ULONG_PTR)0xdeadbeef,
                                                  __uuidof(ID3D11Texture2D), (void **)&imported)))
        {
            release(imported);
            passed = false;
        }
        release(device1);
    }

    return passed;
}

static bool multiple_fence_waiters(ID3D11Device *device, ID3D11DeviceContext *context)
{
    ID3D11Device5 *device5 = NULL;
    ID3D11DeviceContext4 *context4 = NULL;
    ID3D11Fence *fence = NULL;
    HANDLE events[3] = {NULL, NULL, NULL};
    bool passed = false;

    if (FAILED(device->QueryInterface(__uuidof(ID3D11Device5), (void **)&device5))) goto done;
    if (FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&context4))) goto done;
    if (FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void **)&fence))) goto done;

    for (int i = 0; i < 3; ++i)
    {
        events[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (FAILED(fence->SetEventOnCompletion(1, events[i]))) goto done;
    }

    if (FAILED(context4->Signal(fence, 1))) goto done;

    // All 3 waiters must be signaled simultaneously
    passed = (WaitForMultipleObjects(3, events, TRUE, 3000) == WAIT_OBJECT_0);

done:
    for (int i = 0; i < 3; ++i)
        if (events[i]) CloseHandle(events[i]);
    release(fence);
    release(context4);
    release(device5);
    return passed;
}

static bool multiframe_changing_patterns(ID3D11Device *device, ID3D11DeviceContext *context)
{
    ID3D11Device *consumer = NULL;
    ID3D11DeviceContext *consumer_context = NULL;
    ID3D11Device5 *device5 = NULL, *consumer5 = NULL;
    ID3D11DeviceContext4 *context4 = NULL, *consumer4 = NULL;
    ID3D11Fence *producer_fence = NULL, *consumer_fence = NULL;
    ID3D11Texture2D *shared_tex = NULL, *consumer_tex = NULL, *staging = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    IDXGIResource1 *dxgi_res = NULL;
    HANDLE shared_handle = NULL;
    HANDLE shared_fence_h = NULL;
    D3D11_TEXTURE2D_DESC desc = {};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    ID3D11Device1 *consumer1 = NULL;
    bool passed = false;

    if (FAILED(create_device(&consumer, &consumer_context))) goto done;
    if (FAILED(device->QueryInterface(__uuidof(ID3D11Device5), (void **)&device5))) goto done;
    if (FAILED(consumer->QueryInterface(__uuidof(ID3D11Device5), (void **)&consumer5))) goto done;
    if (FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&context4))) goto done;
    if (FAILED(consumer_context->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&consumer4))) goto done;

    // Create shared texture
    desc.Width = desc.Height = 16;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    if (FAILED(device->CreateTexture2D(&desc, NULL, &shared_tex))) goto done;
    if (FAILED(device->CreateRenderTargetView(shared_tex, NULL, &rtv))) goto done;

    if (FAILED(shared_tex->QueryInterface(__uuidof(IDXGIResource1), (void **)&dxgi_res))) goto done;
    if (FAILED(dxgi_res->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &shared_handle))) goto done;

    // Create shared fence
    if (FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void **)&producer_fence))) goto done;
    if (FAILED(producer_fence->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &shared_fence_h))) goto done;

    // Consumer imports texture & fence
    if (FAILED(consumer->QueryInterface(__uuidof(ID3D11Device1), (void **)&consumer1))) goto done;
    if (FAILED(consumer1->OpenSharedResource1(shared_handle, __uuidof(ID3D11Texture2D), (void **)&consumer_tex))) goto done;
    if (FAILED(consumer5->OpenSharedFence(shared_fence_h, __uuidof(ID3D11Fence), (void **)&consumer_fence))) goto done;

    // Staging texture on consumer
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (FAILED(consumer->CreateTexture2D(&desc, NULL, &staging))) goto done;

    // --- Frame 1: Red ---
    context->ClearRenderTargetView(rtv, red);
    if (FAILED(context4->Signal(producer_fence, 1))) goto done;

    if (FAILED(consumer4->Wait(consumer_fence, 1))) goto done;
    consumer_context->CopyResource(staging, consumer_tex);
    if (FAILED(consumer_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) goto done;
    {
        const unsigned char *pixel = (const unsigned char *)mapped.pData;
        if (pixel[0] < 240 || pixel[1] > 15 || pixel[2] > 15) // R should be high, G, B low
        {
            consumer_context->Unmap(staging, 0);
            goto done;
        }
    }
    consumer_context->Unmap(staging, 0);

    // --- Frame 2: Blue (changing pattern) ---
    context->ClearRenderTargetView(rtv, blue);
    if (FAILED(context4->Signal(producer_fence, 2))) goto done;

    if (FAILED(consumer4->Wait(consumer_fence, 2))) goto done;
    consumer_context->CopyResource(staging, consumer_tex);
    if (FAILED(consumer_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) goto done;
    {
        const unsigned char *pixel = (const unsigned char *)mapped.pData;
        // Verify Blue, ensure it is NOT stale Red!
        if (pixel[0] < 15 && pixel[1] < 15 && pixel[2] > 240)
            passed = true;
    }
    consumer_context->Unmap(staging, 0);

done:
    if (shared_fence_h) CloseHandle(shared_fence_h);
    if (shared_handle) CloseHandle(shared_handle);
    release(staging);
    release(consumer_tex);
    release(consumer1);
    release(consumer_fence);
    release(producer_fence);
    release(dxgi_res);
    release(rtv);
    release(shared_tex);
    release(consumer4);
    release(context4);
    release(consumer5);
    release(device5);
    release(consumer_context);
    release(consumer);
    return passed;
}

static bool handle_duplicate_and_close(ID3D11Device *device)
{
    ID3D11Device *consumer = NULL;
    ID3D11DeviceContext *consumer_context = NULL;
    ID3D11Device1 *consumer1 = NULL;
    ID3D11Texture2D *producer_tex = NULL, *consumer_tex = NULL, *staging = NULL;
    IDXGIResource1 *dxgi_res = NULL;
    HANDLE original_handle = NULL, dup_handle = NULL;
    D3D11_TEXTURE2D_DESC desc = {};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr_open = S_OK;
    bool passed = false;

    if (FAILED(create_device(&consumer, &consumer_context))) goto done;
    if (FAILED(consumer->QueryInterface(__uuidof(ID3D11Device1), (void **)&consumer1))) goto done;

    desc.Width = desc.Height = 16;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    if (FAILED(device->CreateTexture2D(&desc, NULL, &producer_tex))) goto done;
    if (FAILED(producer_tex->QueryInterface(__uuidof(IDXGIResource1), (void **)&dxgi_res))) goto done;
    if (FAILED(dxgi_res->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &original_handle))) goto done;

    // Duplicate handle and close original
    if (!DuplicateHandle(GetCurrentProcess(), original_handle, GetCurrentProcess(), &dup_handle,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) goto done;
    CloseHandle(original_handle);
    original_handle = NULL;

    // Import using duplicated handle
    hr_open = consumer1->OpenSharedResource1(dup_handle, __uuidof(ID3D11Texture2D), (void **)&consumer_tex);
    if (FAILED(hr_open)) goto done;

    // Release producer texture entirely to verify consumer retains valid mapping
    release(dxgi_res);
    release(producer_tex);

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (FAILED(consumer->CreateTexture2D(&desc, NULL, &staging))) goto done;
    consumer_context->CopyResource(staging, consumer_tex);
    if (SUCCEEDED(consumer_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
    {
        passed = (mapped.pData != NULL);
        consumer_context->Unmap(staging, 0);
    }

done:
    if (original_handle) CloseHandle(original_handle);
    if (dup_handle) CloseHandle(dup_handle);
    release(staging);
    release(consumer_tex);
    release(consumer1);
    release(producer_tex);
    release(dxgi_res);
    release(consumer_context);
    release(consumer);
    return passed;
}

static bool deferred_context_rejection(ID3D11Device *device)
{
    ID3D11DeviceContext *deferred = NULL;
    ID3D11DeviceContext4 *deferred4 = NULL;
    ID3D11Device5 *device5 = NULL;
    ID3D11Fence *fence = NULL;
    bool passed = false;

    if (FAILED(device->CreateDeferredContext(0, &deferred))) goto done;
    if (FAILED(device->QueryInterface(__uuidof(ID3D11Device5), (void **)&device5))) goto done;
    if (FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), (void **)&fence))) goto done;

    if (SUCCEEDED(deferred->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&deferred4)))
    {
        HRESULT hr_sig = deferred4->Signal(fence, 1);
        HRESULT hr_wait = deferred4->Wait(fence, 1);
        passed = FAILED(hr_sig) && FAILED(hr_wait);
    }
    else
    {
        passed = true;
    }

done:
    release(fence);
    release(device5);
    release(deferred4);
    release(deferred);
    return passed;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--fence-only"))) return 2;
    if (FAILED(create_device(&device, &context))) return 2;
    if (argc == 2)
    {
        // Baseline isolation: its broken buffer cast must not hide the Wait bug.
        bool ordering = pending_wait(device, context);
        printf("pending_fence_clear_and_query_order=%s\n", ordering ? "PASS" : "FAIL");
        release(context); release(device);
        return ordering ? 0 : 1;
    }
    bool buffers = buffer_copy(device, context);
    printf("ordinary_buffer_copy=%s\n", buffers ? "PASS" : "FAIL");
    bool textures1d = texture1d_copy(device, context);
    printf("ordinary_texture1d_copy=%s\n", textures1d ? "PASS" : "FAIL");
    bool textures2d = texture2d_copy(device, context);
    printf("ordinary_texture2d_copy=%s\n", textures2d ? "PASS" : "FAIL");
    bool volumes = texture3d_copy(device, context);
    printf("ordinary_texture3d_copy=%s\n", volumes ? "PASS" : "FAIL");
    bool ordering = pending_wait(device, context);
    printf("pending_fence_clear_and_query_order=%s\n", ordering ? "PASS" : "FAIL");
    bool lifetime = immediate_release();
    printf("queued_signal_immediate_release=%s\n", lifetime ? "PASS" : "FAIL");

    bool format_rej = format_layout_rejection(device);
    printf("format_layout_rejection=%s\n", format_rej ? "PASS" : "FAIL");
    bool mult_waiters = multiple_fence_waiters(device, context);
    printf("multiple_fence_waiters=%s\n", mult_waiters ? "PASS" : "FAIL");
    bool multiframe = multiframe_changing_patterns(device, context);
    printf("multiframe_changing_patterns=%s\n", multiframe ? "PASS" : "FAIL");
    bool dup_handle = handle_duplicate_and_close(device);
    printf("handle_duplicate_and_close=%s\n", dup_handle ? "PASS" : "FAIL");
    bool deferred = deferred_context_rejection(device);
    printf("deferred_context_rejection=%s\n", deferred ? "PASS" : "FAIL");

    release(context); release(device);

    return buffers && textures1d && textures2d && volumes && ordering && lifetime
           && format_rej && mult_waiters && multiframe && dup_handle && deferred ? 0 : 1;
}
