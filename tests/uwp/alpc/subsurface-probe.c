/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: what a DXGI surface made by IDXGIResource1::CreateSubresourceSurface answers (the lifted compositor of the Windows
 * App SDK gives Direct2D such a surface of the texture of a composition surface, with D2D1_BITMAP_OPTIONS_TARGET): QueryInterface for
 * ID3D11Texture2D, ID3D11Resource, IDXGIResource, and whether ID2D1DeviceContext::CreateBitmapFromDxgiSurface takes it as a target.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o subsurface-probe.exe subsurface-probe.c -ld2d1 -ld3d11 -ldxgi -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <stdio.h>

static void qi(IUnknown *obj, const char *name, REFIID iid, IUnknown *expect)
{
    IUnknown *out = NULL;
    HRESULT hr = IUnknown_QueryInterface(obj, iid, (void **)&out);

    printf("QI %s: hr %08lx%s\n", name, hr, SUCCEEDED(hr) ? (out == expect ? " (the texture)" : " (another object)") : "");
    if (out) IUnknown_Release(out);
}

int main(void)
{
    D3D11_TEXTURE2D_DESC td = {64, 32, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                               D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
    D2D1_BITMAP_PROPERTIES1 bp = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                  D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, NULL};
    ID3D11Device *d3d;
    ID3D11Texture2D *tex;
    IDXGIResource1 *res;
    IDXGISurface2 *sub = NULL;
    IDXGIDevice *dxgi;
    ID2D1Factory1 *factory;
    ID2D1Device *device;
    ID2D1DeviceContext *context;
    ID2D1Bitmap1 *bitmap = NULL;
    HRESULT hr;
    int k;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (FAILED(hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &d3d, NULL, NULL))) { printf("device %08lx\n", hr); return 1; }
    ID3D11Device_CreateTexture2D(d3d, &td, NULL, &tex);
    ID3D11Texture2D_QueryInterface(tex, &IID_IDXGIResource1, (void **)&res);
    hr = IDXGIResource1_CreateSubresourceSurface(res, 0, &sub);
    printf("CreateSubresourceSurface: hr %08lx\n", hr);
    if (!sub) return 1;
    qi((IUnknown *)sub, "ID3D11Texture2D", &IID_ID3D11Texture2D, (IUnknown *)tex);
    qi((IUnknown *)sub, "ID3D11Resource", &IID_ID3D11Resource, (IUnknown *)tex);
    qi((IUnknown *)sub, "IDXGIResource", &IID_IDXGIResource, (IUnknown *)res);
    qi((IUnknown *)sub, "IDXGISurface", &IID_IDXGISurface, (IUnknown *)sub);
    {
        ID3D11Texture2D *t2 = NULL;
        UINT index = 99;

        hr = IDXGISurface2_GetResource(sub, &IID_ID3D11Texture2D, (void **)&t2, &index);
        printf("GetResource: hr %08lx, %s, index %u\n", hr, t2 == tex ? "the texture" : "another object", index);
        if (t2) ID3D11Texture2D_Release(t2);
    }

    ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, NULL, (void **)&factory);
    ID2D1Factory1_CreateDevice(factory, dxgi, &device);
    ID2D1Device_CreateDeviceContext(device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context);
    for (k = 0; k < 3; ++k)
    {
        static const D2D1_BITMAP_OPTIONS options[3] = {D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1_BITMAP_OPTIONS_TARGET, D2D1_BITMAP_OPTIONS_NONE};

        bp.bitmapOptions = options[k];
        bitmap = NULL;
        hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface(context, (IDXGISurface *)sub, &bp, &bitmap);
        printf("CreateBitmapFromDxgiSurface on the subresource surface, options %#x: hr %08lx", options[k], hr);
        if (bitmap)
        {
            printf(", bitmap options %#x", ID2D1Bitmap1_GetOptions(bitmap));
            IUnknown_Release((IUnknown *)bitmap);
        }
        printf("\n");
    }
    return 0;
}
