/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe (Windows 11 and Wine): slots 6, 7 and 8 of the Direct3D 11 device's private interface
 * {26c5dc23-e49c-4b0a-8f79-e7b1ac804d32}. XAML of the Windows App SDK calls slot 6 as (texture, const RECT *rect, 0) before it
 * draws with Direct2D on a bitmap made from that texture, slot 7 as (texture) afterwards, and slot 8 as (texture) once before the
 * first use (traced at the interface boundary in Notepad; the rectangle is the one it gives slot 9 of the Direct2D context's
 * private interface for text). The probe does the same on a 128x64 texture that starts green and prints what is in it afterwards.
 * Black-box: interface calls and results only.
 * Usage: d3d11-m6-probe MODE
 *   d2d     slot 6 with {16,8,48,24}; Direct2D: Clear red, aliased blue fill at 0,0-8,4 and at 20,10-24,12; slot 7
 *   clear   slot 6; ClearRenderTargetView red; slot 7
 *   view    slot 6; ClearView red with no rectangle; slot 7
 *   m8      slot 8; read back; slot 7; read back
 *   m8d2d   slot 8; slot 7; then as d2d
 *   none    as d2d without the private calls
 */
#include <windows.h>
#include <d3d11_1.h>
#include <d2d1_1.h>
#include <stdio.h>
#include <string.h>

static const GUID IID_private = {0x26c5dc23, 0xe49c, 0x4b0a, {0x8f, 0x79, 0xe7, 0xb1, 0xac, 0x80, 0x4d, 0x32}};
typedef HRESULT (STDMETHODCALLTYPE *slot6_fn)(void *iface, void *resource, const RECT *rect, void *arg3);
typedef HRESULT (STDMETHODCALLTYPE *slot7_fn)(void *iface, void *resource, void *arg2, void *arg3);

static ID3D11Device *d3d;
static ID3D11DeviceContext *d3dctx;

static void dump(ID3D11Texture2D *tex, const char *when)
{
    static const POINT pts[] = {{0, 0}, {4, 2}, {15, 8}, {16, 8}, {20, 10}, {40, 20}, {47, 23}, {48, 24}, {100, 50}};
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Texture2D *staging;
    unsigned int i, x, y, c, k;
    static const unsigned int colours[] = {0xffff0000, 0xff0000ff, 0xff00ff00, 0};
    static const char *names[] = {"red", "blue", "green", "zero"};

    tex->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    d3d->CreateTexture2D(&desc, NULL, &staging);
    d3dctx->CopyResource(staging, tex);
    if (FAILED(d3dctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) { printf("%s: map failed\n", when); return; }
    printf("%s:", when);
    for (i = 0; i < sizeof(pts) / sizeof(pts[0]); i++)
        printf(" (%ld,%ld)=%08x", pts[i].x, pts[i].y, *(unsigned int *)((char *)map.pData + pts[i].y * map.RowPitch + pts[i].x * 4));
    printf("\n");
    for (k = 0; k < 4; k++)
    {
        unsigned int l = 9999, t = 9999, r = 0, b = 0, n = 0;
        for (y = 0; y < desc.Height; y++) for (x = 0; x < desc.Width; x++)
        {
            c = *(unsigned int *)((char *)map.pData + y * map.RowPitch + x * 4);
            if (c != colours[k]) continue;
            n++;
            if (x < l) l = x; if (y < t) t = y; if (x + 1 > r) r = x + 1; if (y + 1 > b) b = y + 1;
        }
        if (n) printf("  %s: %u pixels within %u,%u-%u,%u\n", names[k], n, l, t, r, b);
    }
    d3dctx->Unmap(staging, 0);
    staging->Release();
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "d2d";
    static const float green[] = {0.0f, 1.0f, 0.0f, 1.0f}, red[] = {1.0f, 0.0f, 0.0f, 1.0f};
    D3D11_TEXTURE2D_DESC desc = {};
    D2D1_BITMAP_PROPERTIES1 props = {};
    RECT rect = {16, 8, 48, 24};
    ID3D11RenderTargetView *rtv;
    ID2D1SolidColorBrush *brush;
    ID2D1DeviceContext *ctx;
    ID2D1Factory1 *factory;
    ID3D11Texture2D *tex;
    IDXGISurface *surface;
    ID2D1Bitmap1 *bitmap;
    ID2D1Device *device;
    IDXGIDevice *dxgi;
    void *priv = NULL;
    bool calls = strcmp(mode, "none");
    slot6_fn slot6 = NULL;
    slot7_fn slot7 = NULL, slot8 = NULL;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, &d3dctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, &d3dctx);
    if (FAILED(hr)) { printf("d3d11 %#lx\n", hr); return 1; }

    desc.Width = 128; desc.Height = 64; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d3d->CreateTexture2D(&desc, NULL, &tex);
    tex->QueryInterface(__uuidof(IDXGISurface), (void **)&surface);
    d3d->CreateRenderTargetView(tex, NULL, &rtv);
    d3dctx->ClearRenderTargetView(rtv, green);
    printf("mode %s; texture %p (vtable %p), surface %p (vtable %p)\n", mode, tex, *(void **)tex, surface, *(void **)surface);
    dump(tex, "start");

    if (calls)
    {
        hr = d3d->QueryInterface(IID_private, &priv);
        printf("private interface hr %#lx\n", hr);
        if (FAILED(hr)) return 0;
        slot6 = (slot6_fn)(*(void ***)priv)[6];
        slot7 = (slot7_fn)(*(void ***)priv)[7];
        slot8 = (slot7_fn)(*(void ***)priv)[8];
    }

    if (!strncmp(mode, "m8", 2))
    {
        hr = slot8(priv, tex, NULL, NULL);
        printf("slot 8 hr %#lx\n", hr);
        dump(tex, "after slot 8");
        hr = slot7(priv, tex, NULL, NULL);
        printf("slot 7 hr %#lx\n", hr);
        dump(tex, "after slot 8 and slot 7");
        if (strcmp(mode, "m8d2d")) return 0;
    }

    if (calls) { hr = slot6(priv, tex, &rect, NULL); printf("slot 6 (texture, {16,8,48,24}, 0) hr %#lx\n", hr); }
    if (!strcmp(mode, "clear"))
    {
        d3dctx->ClearRenderTargetView(rtv, red);
    }
    else if (!strcmp(mode, "view"))
    {
        ID3D11DeviceContext1 *ctx1;
        d3dctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&ctx1);
        ctx1->ClearView(rtv, red, NULL, 0);
        ctx1->Release();
    }
    else
    {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), NULL, (void **)&factory);
        d3d->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi);
        factory->CreateDevice(dxgi, &device);
        device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx);
        props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        props.dpiX = props.dpiY = 96.0f;
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        hr = ctx->CreateBitmapFromDxgiSurface(surface, &props, &bitmap);
        printf("bitmap hr %#lx\n", hr);
        if (FAILED(hr)) return 1;
        ctx->SetTarget(bitmap);
        ctx->BeginDraw();
        ctx->Clear(D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f));
        ctx->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f), &brush);
        ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        ctx->FillRectangle(D2D1::RectF(0.0f, 0.0f, 8.0f, 4.0f), brush);
        ctx->FillRectangle(D2D1::RectF(20.0f, 10.0f, 24.0f, 12.0f), brush);
        hr = ctx->EndDraw();
        printf("draw hr %#lx\n", hr);
        ctx->SetTarget(NULL);
    }
    dump(tex, "after drawing, before slot 7");
    if (calls) { hr = slot7(priv, tex, NULL, NULL); printf("slot 7 hr %#lx\n", hr); }
    dump(tex, "after slot 7");
    return 0;
}
