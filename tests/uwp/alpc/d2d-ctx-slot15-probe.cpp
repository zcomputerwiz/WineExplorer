/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: slot 15 of the private interface {2ea67ed7-d42e-4c07-9dd5-a91ea23e01d2} of a Direct2D device context. The lifted
 * compositor calls slot 10 once with (NULL, 1, 0), then per draw: BeginDraw, slot 7 (target), SetTransform, slot 10 (pointer, 0),
 * Clear, slot 15 (object of its own, 0), EndDraw; its Direct3D draws come during EndDraw, called from Direct2D. This passes an
 * object of our own whose QueryInterface and other vtable slots log their calls (slot numbers, arguments, the module an argument
 * points into, the thread and the phase), in separate scenarios, each on a fresh device context. 64-bit only.
 * Build (mingw): x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -o d2d-ctx-slot15-probe.exe d2d-ctx-slot15-probe.cpp -ld2d1 -ld3d11 -ldxguid -luuid
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <d3d11.h>
#include <d2d1_1.h>

static const GUID IID_ctx_private = {0x2ea67ed7, 0xd42e, 0x4c07, {0x9d, 0xd5, 0xa9, 0x1e, 0xa2, 0x3e, 0x01, 0xd2}};
typedef HRESULT (STDMETHODCALLTYPE *slot15_fn)(void *iface, IUnknown *object, UINT_PTR arg2);
typedef HRESULT (STDMETHODCALLTYPE *slot10_fn)(void *iface, void *arg1, UINT_PTR arg2, UINT_PTR arg3);
typedef HRESULT (STDMETHODCALLTYPE *slot7_fn)(void *iface, ID2D1Bitmap1 *bitmap, void *arg2);

static const char *phase = "setup";
static LONG refs = 1;
static ID3D11Device *d3d;
static ID2D1Device *device;

#define NSLOTS 48
struct spy { void **vtbl; };
static struct spy spy_obj;
static void *spy_vtbl[NSLOTS];

static void print_guid(const GUID *g)
{
    printf("{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", (unsigned long)g->Data1, g->Data2, g->Data3, g->Data4[0],
           g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

/* the module an address lies in, by name only (no offsets), or what object of ours it is */
static void describe(const char *name, UINT_PTR v)
{
    HMODULE mod;
    char path[MAX_PATH], *base;
    if (v < 0x10000) return;
    if ((void *)v == (void *)d3d) { printf(" %s=our d3d device", name); return; }
    if ((void *)v == (void *)device) { printf(" %s=our d2d device", name); return; }
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)v, &mod)
        && GetModuleFileNameA(mod, path, sizeof(path)))
    {
        base = strrchr(path, '\\');
        printf(" %s in %s%s", name, base ? base + 1 : path, (UINT_PTR)mod == v ? " (its base)" : "");
        return;
    }
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void *)v, &mbi, sizeof(mbi)))
        printf(" %s %s", name, mbi.Type == MEM_PRIVATE ? "private memory" : mbi.Type == MEM_MAPPED ? "mapped memory" : "other");
}

static HRESULT STDMETHODCALLTYPE spy_QueryInterface(void *iface, REFIID iid, void **out)
{
    printf("  [%s] QI ", phase);
    print_guid(&iid);
    printf(" -> E_NOINTERFACE\n");
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE spy_AddRef(void *iface)
{
    ULONG r = InterlockedIncrement(&refs);
    printf("  [%s] AddRef -> %lu\n", phase, r);
    return r;
}

static ULONG STDMETHODCALLTYPE spy_Release(void *iface)
{
    ULONG r = InterlockedDecrement(&refs);
    printf("  [%s] Release -> %lu\n", phase, r);
    return r;
}

static ID3D11Resource *current_target;
static HRESULT slot3_result = S_OK;
static int paint_green;
static void **requeue_priv;   /* when set, slot 3 queues the object once more (once) */
typedef HRESULT (STDMETHODCALLTYPE *slot15_fn_t)(void *iface, IUnknown *object, UINT_PTR arg2);

/* what Direct3D state Direct2D leaves bound on the immediate context when it calls the object */
static void dump_d3d_state(void)
{
    ID3D11DeviceContext *imm = NULL;
    ID3D11RenderTargetView *rtv[2] = {};
    ID3D11DepthStencilView *dsv = NULL;
    ID3D11Resource *res = NULL;
    D3D11_VIEWPORT vp[4];
    D3D11_RECT sc[4];
    UINT nvp = 4, nsc = 4;
    ID3D11PixelShader *ps = NULL;
    ID3D11VertexShader *vs = NULL;
    ID3D11BlendState *blend = NULL;
    float factor[4];
    UINT mask;

    d3d->GetImmediateContext(&imm);
    imm->OMGetRenderTargets(2, rtv, &dsv);
    if (rtv[0]) rtv[0]->GetResource(&res);
    printf("    state: rtv0 %s, rtv1 %s, dsv %s", !rtv[0] ? "none" : res == current_target ? "the d2d target" : "other",
           rtv[1] ? "set" : "none", dsv ? "set" : "none");
    imm->RSGetViewports(&nvp, vp);
    printf(", %u viewports", nvp);
    if (nvp) printf(" (%.0f,%.0f %.0fx%.0f)", vp[0].TopLeftX, vp[0].TopLeftY, vp[0].Width, vp[0].Height);
    imm->RSGetScissorRects(&nsc, sc);
    printf(", %u scissors", nsc);
    if (nsc) printf(" (%ld,%ld)-(%ld,%ld)", sc[0].left, sc[0].top, sc[0].right, sc[0].bottom);
    imm->PSGetShader(&ps, NULL, NULL);
    imm->VSGetShader(&vs, NULL, NULL);
    imm->OMGetBlendState(&blend, factor, &mask);
    printf(", ps %s, vs %s, blend %s\n", ps ? "set" : "none", vs ? "set" : "none", blend ? "set" : "none");
    if (ps) ps->Release();
    if (vs) vs->Release();
    if (blend) blend->Release();
    if (res) res->Release();
    if (rtv[0]) rtv[0]->Release();
    if (rtv[1]) rtv[1]->Release();
    if (dsv) dsv->Release();
    imm->Release();
}

template <int N> static UINT_PTR STDMETHODCALLTYPE spy_slot(void *iface, UINT_PTR a, UINT_PTR b, UINT_PTR c)
{
    printf("  [%s] tid %lu slot %d (%#llx, %#llx, %#llx)", phase, GetCurrentThreadId(), N,
           (unsigned long long)a, (unsigned long long)b, (unsigned long long)c);
    describe("a", a); describe("b", b); describe("c", c);
    printf("\n");
    if (N == 3)
    {
        dump_d3d_state();
        if (requeue_priv)
        {
            void **p = requeue_priv;
            requeue_priv = NULL;
            HRESULT hr = ((slot15_fn_t)(*(void ***)p)[15])(p, (IUnknown *)iface, 0);
            printf("    requeued from slot 3: %#lx\n", (unsigned long)hr);
        }
        if (paint_green)
        {
            static const float green[4] = {0, 1, 0, 1};
            ID3D11DeviceContext *imm;
            ID3D11RenderTargetView *rtv = NULL;
            d3d->GetImmediateContext(&imm);
            imm->OMGetRenderTargets(1, &rtv, NULL);
            if (rtv) { imm->ClearRenderTargetView(rtv, green); rtv->Release(); }
            imm->Release();
        }
        return slot3_result;
    }
    return 0;
}

template <int N> static void fill_slots(void)
{
    spy_vtbl[N] = (void *)spy_slot<N>;
    if constexpr (N + 1 < NSLOTS) fill_slots<N + 1>();
}

struct ctx_set { ID2D1DeviceContext *ctx; void **priv; ID2D1Bitmap1 *target; };

static void fresh(struct ctx_set *s)
{
    D2D1_BITMAP_PROPERTIES1 bp = {};
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &s->ctx);
    s->ctx->QueryInterface(IID_ctx_private, (void **)&s->priv);
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.dpiX = bp.dpiY = 96.0f;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    s->ctx->CreateBitmap(D2D1::SizeU(64, 64), NULL, 0, &bp, &s->target);
    s->ctx->SetTarget(s->target);
    IDXGISurface *surface = NULL;
    if (current_target) current_target->Release();
    current_target = NULL;
    if (SUCCEEDED(s->target->GetSurface(&surface)))
    {
        surface->QueryInterface(__uuidof(ID3D11Resource), (void **)&current_target);
        surface->Release();
    }
}

static void done(struct ctx_set *s)
{
    phase = "teardown";
    s->ctx->SetTarget(NULL);
    s->target->Release();
    ((IUnknown *)s->priv)->Release();
    s->ctx->Release();
    printf(" after teardown: refs %ld\n", refs);
}

static void scenario(const char *name, int slot10_first, UINT_PTR arg2, int flush, int twice)
{
    struct ctx_set s;
    D2D1_TAG t1, t2;
    HRESULT hr;

    printf("== %s\n", name);
    fresh(&s);
    slot15_fn slot15 = (slot15_fn)(*(void ***)s.priv)[15];
    slot10_fn slot10 = (slot10_fn)(*(void ***)s.priv)[10];
    if (slot10_first)
    {
        hr = slot10(s.priv, NULL, 1, 0);
        printf(" slot10(NULL, 1, 0): %#lx\n", (unsigned long)hr);
    }
    s.ctx->BeginDraw();
    s.ctx->Clear(D2D1::ColorF(0, 0, 1, 1));
    phase = "slot15";
    hr = slot15(s.priv, (IUnknown *)&spy_obj, arg2);
    printf(" slot15(obj, %llu): %#lx, refs %ld\n", (unsigned long long)arg2, (unsigned long)hr, refs);
    if (twice)
    {
        phase = "slot15 #2";
        hr = slot15(s.priv, (IUnknown *)&spy_obj, arg2);
        printf(" slot15 again: %#lx, refs %ld\n", (unsigned long)hr, refs);
    }
    if (flush)
    {
        phase = "Flush";
        hr = s.ctx->Flush(&t1, &t2);
        printf(" Flush: %#lx, refs %ld\n", (unsigned long)hr, refs);
    }
    phase = "EndDraw";
    hr = s.ctx->EndDraw(&t1, &t2);
    printf(" EndDraw: %#lx, refs %ld\n", (unsigned long)hr, refs);
    done(&s);
}

int main(int argc, char **argv)
{
    IDXGIDevice *dxgi = NULL;
    ID2D1Factory1 *factory = NULL;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    fill_slots<3>();
    spy_vtbl[0] = (void *)spy_QueryInterface;
    spy_vtbl[1] = (void *)spy_AddRef;
    spy_vtbl[2] = (void *)spy_Release;
    spy_obj.vtbl = spy_vtbl;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
    if (FAILED(hr)) return 1;
    d3d->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi);
    D2D1_FACTORY_OPTIONS opts = {D2D1_DEBUG_LEVEL_NONE};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &opts, (void **)&factory);
    factory->CreateDevice(dxgi, &device);
    printf("d3d device %p, d2d device %p\n", (void *)d3d, (void *)device);

    scenario("arg 0", 0, 0, 0, 0);
    scenario("arg 1", 0, 1, 0, 0);
    scenario("arg 1, Flush", 0, 1, 1, 0);
    scenario("arg 1, twice", 0, 1, 0, 1);
    scenario("slot10(NULL,1) first, arg 0", 1, 0, 0, 0);
    scenario("slot10(NULL,1) first, arg 0, twice", 1, 0, 0, 1);
    scenario("arg 2", 0, 2, 0, 0);
    slot3_result = E_FAIL;
    scenario("slot 3 returns E_FAIL", 0, 0, 0, 0);
    slot3_result = S_OK;
    /* the order against Direct2D's own drawing: a fill before and after the object, then read three pixels */
    {
        struct ctx_set s;
        D2D1_TAG t1, t2;
        ID2D1SolidColorBrush *brush = NULL;
        printf("== order\n");
        fresh(&s);
        slot15_fn slot15 = (slot15_fn)(*(void ***)s.priv)[15];
        s.ctx->CreateSolidColorBrush(D2D1::ColorF(1, 0, 0, 1), &brush);
        s.ctx->BeginDraw();
        s.ctx->Clear(D2D1::ColorF(0, 0, 1, 1));
        s.ctx->SetTransform(D2D1::Matrix3x2F::Translation(5, 7));
        s.ctx->FillRectangle(D2D1::RectF(0, 0, 10, 10), brush);
        phase = "order slot15";
        slot15(s.priv, (IUnknown *)&spy_obj, 0);
        s.ctx->FillRectangle(D2D1::RectF(20, 0, 30, 10), brush);
        phase = "order EndDraw";
        printf(" EndDraw: %#lx\n", (unsigned long)s.ctx->EndDraw(&t1, &t2));
        brush->Release();
        done(&s);
    }
    /* the order in pixels: the object clears its target green; before or after a Direct2D clear */
    for (int after = 1; after >= 0; --after)
    {
        struct ctx_set s;
        D2D1_TAG t1, t2;
        ID2D1Bitmap1 *readback = NULL;
        D2D1_BITMAP_PROPERTIES1 bp = {};
        D2D1_POINT_2U origin = {0, 0};
        D2D1_MAPPED_RECT mapped;
        printf("== pixels, object %s a blue clear\n", after ? "after" : "before");
        fresh(&s);
        slot15_fn slot15 = (slot15_fn)(*(void ***)s.priv)[15];
        paint_green = 1;
        s.ctx->BeginDraw();
        s.ctx->Clear(D2D1::ColorF(1, 0, 0, 1));
        if (!after) slot15(s.priv, (IUnknown *)&spy_obj, 0);
        s.ctx->Clear(D2D1::ColorF(0, 0, 1, 1));
        if (after) slot15(s.priv, (IUnknown *)&spy_obj, 0);
        phase = "pixels EndDraw";
        printf(" EndDraw: %#lx\n", (unsigned long)s.ctx->EndDraw(&t1, &t2));
        paint_green = 0;
        bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        bp.dpiX = bp.dpiY = 96.0f;
        bp.bitmapOptions = (D2D1_BITMAP_OPTIONS)(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW);
        s.ctx->CreateBitmap(D2D1::SizeU(64, 64), NULL, 0, &bp, &readback);
        readback->CopyFromBitmap(&origin, s.target, NULL);
        if (SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
        {
            printf(" pixel (BGRA) %08x\n", *(UINT32 *)mapped.bits);
            readback->Unmap();
        }
        readback->Release();
        done(&s);
    }

    /* N1: an object queued from slot 3 during EndDraw */
    {
        struct ctx_set s;
        D2D1_TAG t1, t2;
        printf("== requeue in EndDraw\n");
        fresh(&s);
        slot15_fn slot15 = (slot15_fn)(*(void ***)s.priv)[15];
        s.ctx->BeginDraw();
        slot15(s.priv, (IUnknown *)&spy_obj, 0);
        requeue_priv = s.priv;
        phase = "requeue EndDraw";
        printf(" EndDraw: %#lx, refs %ld\n", (unsigned long)s.ctx->EndDraw(&t1, &t2), refs);
        requeue_priv = NULL;
        phase = "requeue next draw";
        s.ctx->BeginDraw();
        printf(" EndDraw 2: %#lx, refs %ld\n", (unsigned long)s.ctx->EndDraw(&t1, &t2), refs);
        done(&s);
    }
    /* N3: BeginDraw again with an object queued, and a context released with one queued */
    {
        struct ctx_set s;
        D2D1_TAG t1, t2;
        printf("== BeginDraw twice\n");
        fresh(&s);
        slot15_fn slot15 = (slot15_fn)(*(void ***)s.priv)[15];
        s.ctx->BeginDraw();
        slot15(s.priv, (IUnknown *)&spy_obj, 0);
        phase = "second BeginDraw";
        s.ctx->BeginDraw();
        printf(" after second BeginDraw: refs %ld\n", refs);
        phase = "twice EndDraw";
        printf(" EndDraw: %#lx, refs %ld\n", (unsigned long)s.ctx->EndDraw(&t1, &t2), refs);
        phase = "twice EndDraw 2";
        printf(" EndDraw 2: %#lx, refs %ld\n", (unsigned long)s.ctx->EndDraw(&t1, &t2), refs);
        done(&s);

        printf("== release with one queued\n");
        fresh(&s);
        slot15 = (slot15_fn)(*(void ***)s.priv)[15];
        s.ctx->BeginDraw();
        slot15(s.priv, (IUnknown *)&spy_obj, 0);
        printf(" queued: refs %ld\n", refs);
        done(&s);
    }

    /* a target change between the object and the flush: when is it called, and on which target */
    {
        struct ctx_set s;
        D2D1_TAG t1, t2;
        ID2D1Bitmap1 *other = NULL;
        ID3D11Resource *first;
        D2D1_BITMAP_PROPERTIES1 bp = {};
        printf("== target change\n");
        fresh(&s);
        first = current_target;
        first->AddRef();
        bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        bp.dpiX = bp.dpiY = 96.0f;
        bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
        s.ctx->CreateBitmap(D2D1::SizeU(32, 16), NULL, 0, &bp, &other);
        slot15_fn slot15 = (slot15_fn)(*(void ***)s.priv)[15];
        slot7_fn slot7 = (slot7_fn)(*(void ***)s.priv)[7];
        s.ctx->BeginDraw();
        phase = "change slot15";
        slot15(s.priv, (IUnknown *)&spy_obj, 0);
        phase = "change SetTarget";
        s.ctx->SetTarget(other);
        printf(" after SetTarget(other)\n");
        (void)slot7;
        phase = "change EndDraw";
        printf(" EndDraw: %#lx\n", (unsigned long)s.ctx->EndDraw(&t1, &t2));
        /* is the "d2d target" in the dump the first target? */
        printf(" (in the dumps, \"the d2d target\" means the first, 64x64 target)\n");
        first->Release();
        other->Release();
        done(&s);
    }
    printf("end\n");
    return 0;
}
