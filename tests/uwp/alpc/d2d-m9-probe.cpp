/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe (Windows 11 and Wine): slot 9 of the Direct2D device context's private interface
 * {2ea67ed7-d42e-4c07-9dd5-a91ea23e01d2}. XAML of the Windows App SDK calls it as (context, bitmap, &rect) before SetTarget /
 * BeginDraw / Clear / DrawGlyphRun on a bitmap made from a DXGI surface, and as (context, bitmap, NULL) afterwards. The probe does
 * the same on a 128x64 texture that starts green: slot 9 with a rectangle, Clear red, fill a small blue rectangle at 0,0, EndDraw,
 * slot 9 with NULL; then it reads the texture back and prints pixels inside and outside the rectangle. Black-box: interface calls.
 * Usage: d2d-m9-probe [skip|inside]   (skip: no slot 9 calls, for comparison; inside: a second blue fill at 20,10 inside the
 * rectangle)
 */
#include <windows.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <stdio.h>
#include <string.h>

static const GUID IID_private = {0x2ea67ed7, 0xd42e, 0x4c07, {0x9d, 0xd5, 0xa9, 0x1e, 0xa2, 0x3e, 0x01, 0xd2}};
typedef HRESULT (STDMETHODCALLTYPE *slot9_fn)(void *iface, ID2D1Bitmap *bitmap, const D2D1_RECT_U *rect);

static ID3D11Device *d3d;
static ID3D11DeviceContext *d3dctx;

static void dump(ID3D11Texture2D *tex, const char *when)
{
    static const POINT pts[] = {{0, 0}, {4, 2}, {12, 6}, {16, 8}, {17, 9}, {20, 10}, {23, 11}, {24, 12}, {40, 20}, {47, 23}, {48, 24},
                                {60, 30}, {100, 50}};
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Texture2D *staging;
    unsigned int i, x, y, l = 9999, t = 9999, r = 0, b = 0, blue_l = 9999, blue_t = 9999, blue_r = 0, blue_b = 0;

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
    /* the bounds of the red and of the blue pixels */
    for (y = 0; y < desc.Height; y++) for (x = 0; x < desc.Width; x++)
    {
        unsigned int c = *(unsigned int *)((char *)map.pData + y * map.RowPitch + x * 4);
        if (c == 0xffff0000) { if (x < l) l = x; if (y < t) t = y; if (x + 1 > r) r = x + 1; if (y + 1 > b) b = y + 1; }
        if (c == 0xff0000ff) { if (x < blue_l) blue_l = x; if (y < blue_t) blue_t = y; if (x + 1 > blue_r) blue_r = x + 1; if (y + 1 > blue_b) blue_b = y + 1; }
    }
    if (r) printf("  red pixels within %u,%u-%u,%u\n", l, t, r, b); else printf("  no red pixels\n");
    if (blue_r) printf("  blue pixels within %u,%u-%u,%u\n", blue_l, blue_t, blue_r, blue_b); else printf("  no blue pixels\n");
    d3dctx->Unmap(staging, 0);
    staging->Release();
}

int main(int argc, char **argv)
{
    D3D11_TEXTURE2D_DESC desc = {};
    D2D1_BITMAP_PROPERTIES1 props = {};
    D2D1_RECT_U rect = {16, 8, 48, 24};
    bool skip = argc > 1 && !strcmp(argv[1], "skip");
    ID2D1SolidColorBrush *brush;
    ID2D1DeviceContext *ctx;
    ID2D1Factory1 *factory;
    ID3D11Texture2D *tex;
    IDXGISurface *surface;
    ID2D1Bitmap1 *bitmap;
    ID2D1Device *device;
    IDXGIDevice *dxgi;
    void *priv = NULL;
    slot9_fn slot9;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, &d3dctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, &d3dctx);
    if (FAILED(hr)) { printf("d3d11 %#lx\n", hr); return 1; }
    D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), NULL, (void **)&factory);
    d3d->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi);
    factory->CreateDevice(dxgi, &device);
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx);

    desc.Width = 128; desc.Height = 64; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d3d->CreateTexture2D(&desc, NULL, &tex);
    tex->QueryInterface(__uuidof(IDXGISurface), (void **)&surface);
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = props.dpiY = 96.0f;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    hr = ctx->CreateBitmapFromDxgiSurface(surface, &props, &bitmap);
    printf("bitmap hr %#lx\n", hr);
    if (FAILED(hr)) return 1;

    ctx->SetTarget(bitmap);
    ctx->BeginDraw();
    ctx->Clear(D2D1::ColorF(0.0f, 1.0f, 0.0f, 1.0f));
    hr = ctx->EndDraw();
    ctx->SetTarget(NULL);
    printf("green clear hr %#lx\n", hr);
    dump(tex, "start");

    hr = ctx->QueryInterface(IID_private, &priv);
    printf("private interface hr %#lx\n", hr);
    if (FAILED(hr)) return 0;
    slot9 = (slot9_fn)(*(void ***)priv)[9];

    if (!skip) { hr = slot9(priv, bitmap, &rect); printf("slot 9 (bitmap, {16,8,48,24}) hr %#lx\n", hr); }
    ctx->SetTarget(bitmap);
    ctx->BeginDraw();
    ctx->Clear(D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f));
    ctx->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f), &brush);
    ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    ctx->FillRectangle(D2D1::RectF(0.0f, 0.0f, 8.0f, 4.0f), brush);
    if (argc > 1 && !strcmp(argv[1], "inside")) ctx->FillRectangle(D2D1::RectF(20.0f, 10.0f, 24.0f, 12.0f), brush);
    hr = ctx->EndDraw();
    printf("draw hr %#lx\n", hr);
    if (!skip) { hr = slot9(priv, bitmap, NULL); printf("slot 9 (bitmap, NULL) hr %#lx\n", hr); }
    ctx->SetTarget(NULL);
    dump(tex, skip ? "after drawing without slot 9" : "after drawing between the slot 9 calls");

    /* a second draw after the NULL call: is the rectangle still in force? */
    ctx->SetTarget(bitmap);
    ctx->BeginDraw();
    ctx->FillRectangle(D2D1::RectF(100.0f, 50.0f, 104.0f, 52.0f), brush);
    hr = ctx->EndDraw();
    ctx->SetTarget(NULL);
    dump(tex, "after a later fill at 100,50");

    /* is the rectangle the bitmap's? Another bitmap as the target while the first one's rectangle is in force */
    if (!skip)
    {
        ID3D11Texture2D *tex2;
        IDXGISurface *surface2;
        ID2D1Bitmap1 *bitmap2;

        d3d->CreateTexture2D(&desc, NULL, &tex2);
        tex2->QueryInterface(__uuidof(IDXGISurface), (void **)&surface2);
        ctx->CreateBitmapFromDxgiSurface(surface2, &props, &bitmap2);
        slot9(priv, bitmap, &rect);
        ctx->SetTarget(bitmap2);
        ctx->BeginDraw();
        ctx->Clear(D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f));
        hr = ctx->EndDraw();
        ctx->SetTarget(NULL);
        slot9(priv, bitmap, NULL);
        printf("other bitmap draw hr %#lx\n", hr);
        dump(tex2, "another bitmap, drawn while the first one's rectangle is in force");
    }
    return 0;
}
