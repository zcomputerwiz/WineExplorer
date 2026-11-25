/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: slot 9 of the private interface {2ea67ed7-d42e-4c07-9dd5-a91ea23e01d2} of a Direct2D device context. The
 * lifted compositor (dcompi.dll) calls it on the UI thread in pairs per bitmap: (bitmap, rect, NULL), where the rect holds four
 * dwords such as {0,1,42,13} (cells of an atlas), then (bitmap, NULL, pointer to a dword). This prints what the slot returns and
 * writes, whether it changes the bitmap's pixels, and how it behaves inside and outside a draw. 64-bit only.
 * Build (mingw): x86_64-w64-mingw32-g++ -O2 -static -o d2d-ctx-slot9-probe.exe d2d-ctx-slot9-probe.cpp -ld2d1 -ld3d11 -ldxguid -luuid
 */
#include <stdio.h>
#include <windows.h>
#include <d3d11.h>
#include <d2d1_1.h>

static const GUID IID_ctx_private = {0x2ea67ed7, 0xd42e, 0x4c07, {0x9d, 0xd5, 0xa9, 0x1e, 0xa2, 0x3e, 0x01, 0xd2}};
typedef HRESULT (STDMETHODCALLTYPE *slot9_fn)(void *iface, ID2D1Bitmap1 *bitmap, const void *rect, void *out);

static ID2D1DeviceContext *ctx;
static ID2D1Bitmap1 *readback;

static void dump_pixels(const char *when, ID2D1Bitmap1 *bitmap)
{
    static const UINT32 points[][2] = {{5, 5}, {30, 6}, {50, 5}, {5, 20}, {50, 50}};
    D2D1_POINT_2U origin = {0, 0};
    D2D1_RECT_U all = {0, 0, 64, 64};
    D2D1_MAPPED_RECT mapped;
    HRESULT hr;
    unsigned int i;

    hr = readback->CopyFromBitmap(&origin, bitmap, &all);
    if (FAILED(hr)) { printf("%s: CopyFromBitmap %#lx\n", when, (unsigned long)hr); return; }
    hr = readback->Map(D2D1_MAP_OPTIONS_READ, &mapped);
    if (FAILED(hr)) { printf("%s: Map %#lx\n", when, (unsigned long)hr); return; }
    printf("%s: pixels", when);
    for (i = 0; i < ARRAYSIZE(points); ++i)
        printf(" (%u,%u)=%08x", points[i][0], points[i][1],
               *(UINT32 *)(mapped.bits + points[i][1] * mapped.pitch + points[i][0] * 4));
    printf("\n");
    readback->Unmap();
}

static void fill(ID2D1Bitmap1 *bitmap, float r, float g, float b)
{
    D2D1_TAG t1, t2;
    HRESULT hr;

    ctx->SetTarget(bitmap);
    ctx->BeginDraw();
    ctx->Clear(D2D1::ColorF(r, g, b, 1));
    hr = ctx->EndDraw(&t1, &t2);
    if (FAILED(hr)) printf("fill: EndDraw %#lx\n", (unsigned long)hr);
    ctx->SetTarget(NULL);
}

int main(void)
{
    ID3D11Device *d3d = NULL;
    IDXGIDevice *dxgi = NULL;
    ID2D1Factory1 *factory = NULL;
    ID2D1Device *device = NULL;
    void **priv = NULL;
    ID2D1Bitmap1 *a = NULL, *plain = NULL;
    D2D1_TAG t1, t2;
    DWORD rect1[4] = {0, 1, 42, 13}, rect2[4] = {42, 1, 53, 12}, rect3[4] = {0, 13, 59, 27};
    DWORD out[2];
    HRESULT hr;
    int i;

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
    slot9_fn slot9 = (slot9_fn)(*(void ***)priv)[9];

    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.dpiX = bp.dpiY = 96.0f;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    ctx->CreateBitmap(D2D1::SizeU(64, 64), NULL, 0, &bp, &a);
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
    ctx->CreateBitmap(D2D1::SizeU(64, 64), NULL, 0, &bp, &plain);
    bp.bitmapOptions = (D2D1_BITMAP_OPTIONS)(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW);
    ctx->CreateBitmap(D2D1::SizeU(64, 64), NULL, 0, &bp, &readback);

    fill(a, 1, 0, 0);
    dump_pixels("red", a);

    out[0] = 0xdeadbeef; out[1] = 0xcafef00d;
    hr = slot9(priv, a, NULL, out);
    printf("slot9(a, NULL, out) fresh: %#lx out %08lx %08lx\n", (unsigned long)hr, out[0], out[1]);

    hr = slot9(priv, a, rect1, NULL);
    printf("slot9(a, rect1, NULL) outside a draw: %#lx\n", (unsigned long)hr);
    out[0] = 0xdeadbeef; out[1] = 0xcafef00d;
    hr = slot9(priv, a, NULL, out);
    printf("slot9(a, NULL, out): %#lx out %08lx %08lx\n", (unsigned long)hr, out[0], out[1]);
    dump_pixels("after rect1 outside a draw", a);

    /* in a draw on the bitmap, as a target */
    ctx->SetTarget(a);
    ctx->BeginDraw();
    hr = slot9(priv, a, rect2, NULL);
    printf("slot9(a, rect2, NULL) in a draw: %#lx\n", (unsigned long)hr);
    out[0] = 0xdeadbeef; out[1] = 0xcafef00d;
    hr = slot9(priv, a, NULL, out);
    printf("slot9(a, NULL, out) in a draw: %#lx out %08lx %08lx\n", (unsigned long)hr, out[0], out[1]);
    hr = ctx->EndDraw(&t1, &t2);
    printf("EndDraw: %#lx\n", (unsigned long)hr);
    ctx->SetTarget(NULL);
    dump_pixels("after rect2 in a draw", a);

    /* repeated rects: does the dword count? */
    for (i = 0; i < 3; ++i)
    {
        hr = slot9(priv, a, rect3, NULL);
        out[0] = 0xdeadbeef; out[1] = 0xcafef00d;
        HRESULT hr2 = slot9(priv, a, NULL, out);
        printf("repeat %d: %#lx, %#lx out %08lx %08lx\n", i, (unsigned long)hr, (unsigned long)hr2, out[0], out[1]);
    }
    dump_pixels("after repeats", a);

    /* the out dword after drawing new content */
    fill(a, 0, 0, 1);
    out[0] = 0xdeadbeef; out[1] = 0xcafef00d;
    hr = slot9(priv, a, NULL, out);
    printf("after a new fill: %#lx out %08lx %08lx\n", (unsigned long)hr, out[0], out[1]);

    /* state: target and draw state after each call of a pair, and whether a draw between the two calls reaches the bitmap */
    {
        ID2D1Image *target;
        DWORD rect4[4] = {0, 0, 32, 16};

        ctx->SetTarget(NULL);
        fill(a, 1, 0, 0);
        hr = slot9(priv, a, rect4, NULL);
        target = NULL; ctx->GetTarget(&target);
        printf("state: slot9(a, rect4, NULL) %#lx, target %s\n", (unsigned long)hr, !target ? "NULL" : target == a ? "a" : "other");
        if (target) target->Release();
        ctx->BeginDraw();
        ctx->Clear(D2D1::ColorF(0, 1, 0, 1));
        hr = ctx->EndDraw(&t1, &t2);
        printf("state: BeginDraw/Clear(green)/EndDraw between: %#lx\n", (unsigned long)hr);
        out[0] = 0xdeadbeef; out[1] = 0xcafef00d;
        hr = slot9(priv, a, NULL, out);
        target = NULL; ctx->GetTarget(&target);
        printf("state: slot9(a, NULL, out) %#lx out %08lx %08lx, target %s\n", (unsigned long)hr, out[0], out[1],
               !target ? "NULL" : target == a ? "a" : "other");
        if (target) target->Release();
        dump_pixels("state: after the pair", a);
        ctx->SetTarget(NULL);
        fill(a, 1, 0, 0);
        dump_pixels("state: fill red after the pair", a);

        /* the pair inside one draw with the bitmap as target, a clear between */
        ctx->SetTarget(a);
        ctx->BeginDraw();
        hr = slot9(priv, a, rect4, NULL);
        ctx->Clear(D2D1::ColorF(0, 1, 0, 1));
        out[0] = 0xdeadbeef;
        HRESULT hr2 = slot9(priv, a, NULL, out);
        HRESULT hr3 = ctx->EndDraw(&t1, &t2);
        printf("state: in one draw: %#lx, %#lx out %08lx, EndDraw %#lx\n", (unsigned long)hr, (unsigned long)hr2, out[0],
               (unsigned long)hr3);
        ctx->SetTarget(NULL);
        dump_pixels("state: after the pair in a draw", a);
        fill(a, 1, 0, 0);
        dump_pixels("state: fill red after that", a);

        /* only the first call, then an ordinary fill */
        hr = slot9(priv, a, rect4, NULL);
        fill(a, 0, 0, 1);
        printf("state: rect only, then fill blue (EndDraw printed above if it failed)\n");
        dump_pixels("state: after rect only + fill blue", a);
        out[0] = 0xdeadbeef;
        hr = slot9(priv, a, NULL, out);
        fill(a, 0, 0, 1);
        dump_pixels("state: after the closing call + fill blue", a);
    }

    /* both NULL, both set, a bitmap without the target option */
    hr = slot9(priv, a, NULL, NULL);
    printf("slot9(a, NULL, NULL): %#lx\n", (unsigned long)hr);
    out[0] = 0xdeadbeef; out[1] = 0xcafef00d;
    hr = slot9(priv, a, rect1, out);
    printf("slot9(a, rect1, out): %#lx out %08lx %08lx\n", (unsigned long)hr, out[0], out[1]);
    hr = slot9(priv, plain, rect1, NULL);
    printf("slot9(plain, rect1, NULL): %#lx\n", (unsigned long)hr);
    out[0] = 0xdeadbeef; out[1] = 0xcafef00d;
    hr = slot9(priv, plain, NULL, out);
    printf("slot9(plain, NULL, out): %#lx out %08lx %08lx\n", (unsigned long)hr, out[0], out[1]);
    printf("done\n");
    return 0;
}
