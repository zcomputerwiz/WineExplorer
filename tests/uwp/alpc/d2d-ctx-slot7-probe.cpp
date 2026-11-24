/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: slot 7 of the private interface {2ea67ed7-d42e-4c07-9dd5-a91ea23e01d2} of a Direct2D device context. The
 * compositor of the Windows App SDK calls it with a bitmap and 0 after BeginDraw and then draws without calling SetTarget. Does the
 * slot make the bitmap the target (GetTarget, a Clear that succeeds), with which reference count, and what do NULL and other
 * bitmaps give? 64-bit only (the slot's argument sizes on i386 are not measured).
 * Build (mingw): x86_64-w64-mingw32-g++ -O2 -static -o d2d-ctx-slot7-probe.exe d2d-ctx-slot7-probe.cpp -ld2d1 -ld3d11 -ldxguid -luuid
 */
#include <stdio.h>
#include <windows.h>
#include <d3d11.h>
#include <d2d1_1.h>

static const GUID IID_ctx_private = {0x2ea67ed7, 0xd42e, 0x4c07, {0x9d, 0xd5, 0xa9, 0x1e, 0xa2, 0x3e, 0x01, 0xd2}};
typedef HRESULT (STDMETHODCALLTYPE *slot7_fn)(void *iface, ID2D1Bitmap1 *bitmap, UINT_PTR arg2);

static ULONG refs(IUnknown *obj)
{
    obj->AddRef();
    return obj->Release();
}

static void show_target(const char *when, ID2D1DeviceContext *ctx, ID2D1Bitmap1 *a, ID2D1Bitmap1 *b)
{
    ID2D1Image *target = NULL;

    ctx->GetTarget(&target);
    printf("%s: target %s; refs a %lu b %lu\n", when, !target ? "NULL" : target == a ? "a" : target == b ? "b" : "other",
           refs(a) - (target == a), refs(b) - (target == b));
    if (target) target->Release();
}

int main(void)
{
    ID3D11Device *d3d = NULL;
    IDXGIDevice *dxgi = NULL;
    ID2D1Factory1 *factory = NULL;
    ID2D1Device *device = NULL;
    ID2D1DeviceContext *ctx = NULL;
    void **priv = NULL;
    ID2D1Bitmap1 *a = NULL, *b = NULL, *plain = NULL;
    D2D1_TAG t1, t2;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
    if (FAILED(hr)) return 1;
    d3d->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi);
    D2D1_FACTORY_OPTIONS opts = {D2D1_DEBUG_LEVEL_NONE};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &opts, (void **)&factory);
    factory->CreateDevice(dxgi, &device);
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx);
    hr = ctx->QueryInterface(IID_ctx_private, (void **)&priv);
    printf("QI private: %#lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    slot7_fn slot7 = (slot7_fn)(*(void ***)priv)[7];

    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.dpiX = bp.dpiY = 96.0f;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    ctx->CreateBitmap(D2D1::SizeU(16, 16), NULL, 0, &bp, &a);
    ctx->CreateBitmap(D2D1::SizeU(16, 16), NULL, 0, &bp, &b);
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
    ctx->CreateBitmap(D2D1::SizeU(16, 16), NULL, 0, &bp, &plain);
    show_target("start", ctx, a, b);

    /* no target: what Clear gives */
    ctx->BeginDraw();
    ctx->Clear(D2D1::ColorF(0, 0, 0, 1));
    hr = ctx->EndDraw(&t1, &t2);
    printf("no target, BeginDraw/Clear/EndDraw: %#lx\n", (unsigned long)hr);

    /* slot 7 outside a draw */
    hr = slot7(priv, a, 0);
    printf("slot7(a, 0) outside a draw: %#lx\n", (unsigned long)hr);
    show_target("after slot7(a)", ctx, a, b);

    /* the compositor's order: BeginDraw, slot 7, draw, EndDraw */
    ctx->SetTarget(NULL);
    show_target("after SetTarget(NULL)", ctx, a, b);
    ctx->BeginDraw();
    hr = slot7(priv, b, 0);
    printf("slot7(b, 0) in a draw: %#lx\n", (unsigned long)hr);
    show_target("in the draw", ctx, a, b);
    ctx->Clear(D2D1::ColorF(1, 0, 0, 1));
    hr = ctx->EndDraw(&t1, &t2);
    printf("Clear/EndDraw after slot7(b): %#lx\n", (unsigned long)hr);
    show_target("after EndDraw", ctx, a, b);

    /* another call replaces the target? NULL? a bitmap without the target option? a nonzero second argument? */
    hr = slot7(priv, a, 0);
    printf("slot7(a, 0) again: %#lx\n", (unsigned long)hr);
    show_target("after slot7(a)", ctx, a, b);
    hr = slot7(priv, NULL, 0);
    printf("slot7(NULL, 0): %#lx\n", (unsigned long)hr);
    show_target("after slot7(NULL)", ctx, a, b);
    hr = slot7(priv, plain, 0);
    printf("slot7(no target option, 0): %#lx\n", (unsigned long)hr);
    {
        ID2D1Image *target = NULL;
        ctx->GetTarget(&target);
        printf("  target is the plain bitmap: %d\n", target == plain);
        if (target) target->Release();
    }
    hr = slot7(priv, b, 1);
    printf("slot7(b, 1): %#lx\n", (unsigned long)hr);
    show_target("after slot7(b, 1)", ctx, a, b);

    ctx->SetTarget(NULL);
    show_target("end", ctx, a, b);
    return 0;
}
