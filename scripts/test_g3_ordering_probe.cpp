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
    bool textures = texture1d_copy(device, context);
    printf("ordinary_texture1d_copy=%s\n", textures ? "PASS" : "FAIL");
    bool volumes = texture3d_copy(device, context);
    printf("ordinary_texture3d_copy=%s\n", volumes ? "PASS" : "FAIL");
    bool ordering = pending_wait(device, context);
    printf("pending_fence_clear_and_query_order=%s\n", ordering ? "PASS" : "FAIL");
    release(context); release(device);
    bool lifetime = immediate_release();
    printf("queued_signal_immediate_release=%s\n", lifetime ? "PASS" : "FAIL");
    return buffers && textures && volumes && ordering && lifetime ? 0 : 1;
}
