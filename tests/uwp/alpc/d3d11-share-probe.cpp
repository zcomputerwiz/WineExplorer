/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe (Windows 11 and Wine): does a texture shared with an NT handle show the same pixels on a second Direct3D 11 device in the
 * same process? XAML of the Windows App SDK draws its text atlas (B8G8R8A8, render target + shader resource, misc
 * SHARED | SHARED_NTHANDLE) on the UI thread's device; the compositor thread opens it with OpenSharedResource1 on its own device.
 * Device A: create, clear green, write a red square at 16,8-48,24 with ClearView, Flush. Device B: open, read back. Then A clears
 * blue again and B reads again (is it live, or a copy taken at open?). Also B writes, A reads.
 * Usage: d3d11-share-probe [keyed]   (keyed: SHARED_KEYEDMUTEX | SHARED_NTHANDLE, with AcquireSync/ReleaseSync)
 */
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <stdio.h>
#include <string.h>

static void dump(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, const char *when)
{
    static const POINT pts[] = {{0, 0}, {16, 8}, {30, 15}, {47, 23}, {48, 24}, {70, 15}, {100, 50}};
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Texture2D *staging;
    unsigned int i;

    tex->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (FAILED(dev->CreateTexture2D(&desc, NULL, &staging))) { printf("%s: no staging\n", when); return; }
    ctx->CopyResource(staging, tex);
    if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) { printf("%s: map failed\n", when); staging->Release(); return; }
    printf("%s:", when);
    for (i = 0; i < sizeof(pts) / sizeof(pts[0]); i++)
        printf(" (%ld,%ld)=%08x", pts[i].x, pts[i].y, *(unsigned int *)((char *)map.pData + pts[i].y * map.RowPitch + pts[i].x * 4));
    printf("\n");
    ctx->Unmap(staging, 0);
    staging->Release();
}

static HRESULT make_device(ID3D11Device **dev, ID3D11DeviceContext **ctx)
{
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                                   D3D11_SDK_VERSION, dev, NULL, ctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                                           D3D11_SDK_VERSION, dev, NULL, ctx);
    return hr;
}

int main(int argc, char **argv)
{
    static const float green[] = {0.0f, 1.0f, 0.0f, 1.0f}, red[] = {1.0f, 0.0f, 0.0f, 1.0f}, blue[] = {0.0f, 0.0f, 1.0f, 1.0f};
    bool keyed = argc > 1 && !strcmp(argv[1], "keyed");
    D3D11_TEXTURE2D_DESC desc = {};
    ID3D11Device *a, *b;
    ID3D11Device1 *b1;
    ID3D11DeviceContext *actx, *bctx;
    ID3D11DeviceContext1 *actx1, *bctx1;
    ID3D11Texture2D *tex, *opened;
    ID3D11RenderTargetView *rtv, *brtv;
    IDXGIResource1 *res;
    IDXGIKeyedMutex *ka = NULL, *kb = NULL;
    D3D11_RECT square = {16, 8, 48, 24};
    HANDLE handle = NULL;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (FAILED(make_device(&a, &actx)) || FAILED(make_device(&b, &bctx))) { printf("no devices\n"); return 1; }
    actx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&actx1);
    bctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&bctx1);
    b->QueryInterface(__uuidof(ID3D11Device1), (void **)&b1);

    desc.Width = 128; desc.Height = 64; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = keyed ? (D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
                           : (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
    hr = a->CreateTexture2D(&desc, NULL, &tex);
    printf("create (misc %#x) hr %#lx\n", desc.MiscFlags, hr);
    if (FAILED(hr)) return 1;
    tex->QueryInterface(__uuidof(IDXGIResource1), (void **)&res);
    hr = res->CreateSharedHandle(NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, NULL, &handle);
    printf("CreateSharedHandle hr %#lx handle %p\n", hr, handle);
    if (FAILED(hr)) return 1;
    a->CreateRenderTargetView(tex, NULL, &rtv);
    if (keyed) { tex->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **)&ka); printf("A acquire hr %#lx\n", ka->AcquireSync(0, 1000)); }
    actx->ClearRenderTargetView(rtv, green);
    actx1->ClearView(rtv, red, &square, 1);
    actx->Flush();
    dump(a, actx, tex, "A after green + red square");
    if (keyed) printf("A release hr %#lx\n", ka->ReleaseSync(1));

    hr = b1->OpenSharedResource1(handle, __uuidof(ID3D11Texture2D), (void **)&opened);
    printf("B OpenSharedResource1 hr %#lx\n", hr);
    if (FAILED(hr)) return 1;
    if (keyed) { opened->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **)&kb); printf("B acquire hr %#lx\n", kb->AcquireSync(1, 1000)); }
    dump(b, bctx, opened, "B after open");
    if (keyed) printf("B release hr %#lx\n", kb->ReleaseSync(0));

    if (keyed) printf("A acquire hr %#lx\n", ka->AcquireSync(0, 1000));
    actx->ClearRenderTargetView(rtv, blue);
    actx->Flush();
    if (keyed) printf("A release hr %#lx\n", ka->ReleaseSync(1));
    if (keyed) printf("B acquire hr %#lx\n", kb->AcquireSync(1, 1000));
    dump(b, bctx, opened, "B after A clears blue");
    b->CreateRenderTargetView(opened, NULL, &brtv);
    bctx->ClearRenderTargetView(brtv, red);
    bctx->Flush();
    if (keyed) printf("B release hr %#lx\n", kb->ReleaseSync(0));
    if (keyed) printf("A acquire hr %#lx\n", ka->AcquireSync(0, 1000));
    dump(a, actx, tex, "A after B clears red");

    /* A draws with Direct2D, as XAML draws its text: Clear transparent, fill a green square at 16,8-48,24 */
    {
        D2D1_BITMAP_PROPERTIES1 props = {};
        ID2D1SolidColorBrush *brush;
        ID2D1DeviceContext *dc;
        ID2D1Factory1 *factory;
        IDXGISurface *surface;
        ID2D1Bitmap1 *bitmap;
        ID2D1Device *device;
        IDXGIDevice *dxgi;

        D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), NULL, (void **)&factory);
        a->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi);
        factory->CreateDevice(dxgi, &device);
        device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
        tex->QueryInterface(__uuidof(IDXGISurface), (void **)&surface);
        props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        props.dpiX = props.dpiY = 96.0f;
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        hr = dc->CreateBitmapFromDxgiSurface(surface, &props, &bitmap);
        dc->SetTarget(bitmap);
        dc->BeginDraw();
        dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        dc->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 0.0f, 1.0f), &brush);
        dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        dc->FillRectangle(D2D1::RectF(16.0f, 8.0f, 48.0f, 24.0f), brush);
        hr = dc->EndDraw();
        dc->SetTarget(NULL);
        actx->Flush();
        printf("A Direct2D draw hr %#lx\n", hr);
        dump(a, actx, tex, "A after its Direct2D draw");
        if (keyed) { ka->ReleaseSync(1); kb->AcquireSync(1, 1000); }
        dump(b, bctx, opened, "B after A's Direct2D draw");
        if (keyed) { kb->ReleaseSync(0); ka->AcquireSync(0, 1000); }
    }
    if (keyed) ka->ReleaseSync(1);

    /* as XAML updates its atlas: A's private slots 6 (texture, rect, 0) and 7 (texture) around a clear, no Flush; what B reads */
    if (!keyed)
    {
        static const GUID iid_private = {0x26c5dc23, 0xe49c, 0x4b0a, {0x8f, 0x79, 0xe7, 0xb1, 0xac, 0x80, 0x4d, 0x32}};
        typedef HRESULT (STDMETHODCALLTYPE *slot6_fn)(void *, void *, const RECT *, void *);
        typedef HRESULT (STDMETHODCALLTYPE *slot7_fn)(void *, void *, void *, void *);
        static const float yellow[] = {1.0f, 1.0f, 0.0f, 1.0f};
        RECT rect = {16, 8, 48, 24};
        void *priv;

        actx->ClearRenderTargetView(rtv, blue);
        actx->Flush();
        dump(b, bctx, opened, "B before the update (A cleared blue, flushed)");
        if (SUCCEEDED(a->QueryInterface(iid_private, &priv)))
        {
            ((slot6_fn)(*(void ***)priv)[6])(priv, tex, &rect, NULL);
            actx->ClearRenderTargetView(rtv, yellow);
            dump(b, bctx, opened, "B between A's slot 6 and 7");
            ((slot7_fn)(*(void ***)priv)[7])(priv, tex, NULL, NULL);
            dump(b, bctx, opened, "B after A's slot 7, no Flush");
            dump(a, actx, tex, "A after its slot 7");
            /* a second update of another rectangle: does B keep the first one? */
            rect.left = 64; rect.right = 80;
            ((slot6_fn)(*(void ***)priv)[6])(priv, tex, &rect, NULL);
            actx->ClearRenderTargetView(rtv, red);
            ((slot7_fn)(*(void ***)priv)[7])(priv, tex, NULL, NULL);
            dump(b, bctx, opened, "B after a second update at 64,8-80,24");
        }
        else printf("no private interface\n");
    }
    CloseHandle(handle);
    return 0;
}
