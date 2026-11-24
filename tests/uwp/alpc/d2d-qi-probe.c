/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: does a Direct2D device context (on a hardware Direct3D 11 device with BGRA support) answer the interface id that
 * XAML of the Windows App SDK asks it for under Wine ({2ea67ed7-d42e-4c07-9dd5-a91ea23e01d2}), and does a DirectWrite text format
 * answer {28d5197f-3940-4535-b315-e327e13d845b}? Prints the HRESULT, whether the pointer is the object itself, the module+offset of
 * the vtable and how many of its slots point into the same module. Only results of calls are printed.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o d2d-qi-probe.exe d2d-qi-probe.c -ld2d1 -ld3d11 -ldwrite -ldxgi -lole32 -luuid
 */
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <stdio.h>

static const GUID iid_d2d_private = {0x2ea67ed7, 0xd42e, 0x4c07, {0x9d, 0xd5, 0xa9, 0x1e, 0xa2, 0x3e, 0x01, 0xd2}};
static const GUID iid_d2d_device_private = {0x46fdbfea, 0x53f8, 0x440c, {0xb9, 0xe2, 0x81, 0xdf, 0xec, 0x61, 0x2a, 0x72}};
static const GUID iid_d3d11_private2 = {0xf13ebcd1, 0x672c, 0x4f8b, {0xa6, 0x31, 0x95, 0x39, 0xca, 0x74, 0x8d, 0x71}};
static const GUID iid_format_private ={0x28d5197f, 0x3940, 0x4535, {0xb3, 0x15, 0xe3, 0x27, 0xe1, 0x3d, 0x84, 0x5b}};

static void where(void *p, char *buf, size_t n, HMODULE *mod)
{
    HMODULE m = NULL;
    char path[MAX_PATH], *s;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, p, &m) && m &&
        GetModuleFileNameA(m, path, sizeof(path)))
    {
        s = strrchr(path, '\\');
        snprintf(buf, n, "%s+%#llx", s ? s + 1 : path, (unsigned long long)((char *)p - (char *)m));
    }
    else snprintf(buf, n, "%p", p);
    if (mod) *mod = m;
}

static void query(IUnknown *obj, const char *name, const GUID *iid)
{
    IUnknown *unk = NULL;
    HRESULT hr = IUnknown_QueryInterface(obj, iid, (void **)&unk);
    char a[160], b[160];

    printf("QI %s: hr %08lx", name, hr);
    if (SUCCEEDED(hr) && unk)
    {
        void **vt = *(void ***)unk;
        HMODULE m3 = NULL, m = NULL;
        unsigned int n;

        where(vt, a, sizeof(a), NULL); where(vt[3], b, sizeof(b), &m3);
        for (n = 3; n < 128; n++)
        {
            m = NULL;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, vt[n], &m) ||
                m != m3) break;
        }
        printf(" %s the object; vtable %s, slot3 %s, %u slots in that module", (void *)unk == (void *)obj ? "is" : "is not", a, b, n);
        IUnknown_Release(unk);
    }
    printf("\n");
}

int main(void)
{
    ID3D11Device *d3d;
    IDXGIDevice *dxgi;
    ID2D1Factory1 *factory;
    ID2D1Device *device;
    ID2D1DeviceContext *context;
    IDWriteFactory *dwrite;
    IDWriteTextFormat *format;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);   /* a crash of a call keeps what was printed before it */
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
                           &d3d, NULL, NULL);
    if (FAILED(hr)) { printf("D3D11CreateDevice %08lx\n", hr); return 1; }
    ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, NULL, (void **)&factory);
    if (FAILED(hr)) { printf("D2D1CreateFactory %08lx\n", hr); return 1; }
    hr = ID2D1Factory1_CreateDevice(factory, dxgi, &device);
    if (FAILED(hr)) { printf("CreateDevice %08lx\n", hr); return 1; }
    hr = ID2D1Device_CreateDeviceContext(device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context);
    if (FAILED(hr)) { printf("CreateDeviceContext %08lx\n", hr); return 1; }
    query((IUnknown *)context, "{2ea67ed7} on the device context", &iid_d2d_private);
    query((IUnknown *)device, "{2ea67ed7} on the device", &iid_d2d_private);
    query((IUnknown *)factory, "{2ea67ed7} on the factory", &iid_d2d_private);
    /* the lifted compositor asks a new Direct2D device for {46fdbfea-...} and drops the device when that fails; it also asks the
     * Direct3D 11 device for {f13ebcd1-...} */
    query((IUnknown *)device, "{46fdbfea} on the device", &iid_d2d_device_private);
    query((IUnknown *)context, "{46fdbfea} on the device context", &iid_d2d_device_private);
    query((IUnknown *)d3d, "{f13ebcd1} on the Direct3D 11 device", &iid_d3d11_private2);
    {
        ID2D1Factory1 *mt = NULL;
        ID2D1Device *mtdevice = NULL;

        if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &IID_ID2D1Factory1, NULL, (void **)&mt)) &&
            SUCCEEDED(ID2D1Factory1_CreateDevice(mt, dxgi, &mtdevice)))
            query((IUnknown *)mtdevice, "{46fdbfea} on a device of a multithreaded factory", &iid_d2d_device_private);
        if (mtdevice)
        {
            /* slot 5 of {46fdbfea}, as the lifted compositor calls it every few seconds: (iface, 2000, 0x1e0000, out) -> S_OK.
             * Called here with a pre-filled output, before and after the device has made a context and a bitmap */
            typedef HRESULT (STDMETHODCALLTYPE *m5_t)(void *, UINT_PTR, UINT_PTR, void *);
            static const UINT_PTR args[][2] = {{2000, 0x1e0000}, {0, 0}, {0xffffffff, 0}, {2000, 0xffffffff}};
            void *priv = NULL;
            unsigned int i, round;

            IUnknown_QueryInterface((IUnknown *)mtdevice, &iid_d2d_device_private, &priv);
            for (round = 0; priv && round < 2; round++)
            {
                if (round == 1)
                {
                    ID2D1DeviceContext *c = NULL;
                    ID2D1Bitmap1 *bm = NULL;
                    D2D1_BITMAP_PROPERTIES1 bp = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                                  D2D1_BITMAP_OPTIONS_TARGET, NULL};
                    D3D11_TEXTURE2D_DESC td2 = {256, 256, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                                                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
                    ID3D11Texture2D *tex2 = NULL;
                    IDXGISurface *surf2 = NULL;

                    ID2D1Device_CreateDeviceContext(mtdevice, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &c);
                    if (c && SUCCEEDED(ID3D11Device_CreateTexture2D(d3d, &td2, NULL, &tex2)) &&
                        SUCCEEDED(ID3D11Texture2D_QueryInterface(tex2, &IID_IDXGISurface, (void **)&surf2)))
                        ID2D1DeviceContext_CreateBitmapFromDxgiSurface(c, surf2, &bp, &bm);
                    if (c && bm)
                    {
                        D2D1_COLOR_F red = {1, 0, 0, 1};
                        ID2D1SolidColorBrush *br = NULL;
                        D2D1_RECT_F rc = {0, 0, 100, 100};

                        ID2D1DeviceContext_SetTarget(c, (void *)bm);
                        ID2D1RenderTarget_BeginDraw((ID2D1RenderTarget *)c);
                        ID2D1RenderTarget_CreateSolidColorBrush((ID2D1RenderTarget *)c, &red, NULL, &br);
                        if (br) ID2D1RenderTarget_FillRectangle((ID2D1RenderTarget *)c, &rc, (ID2D1Brush *)br);
                        ID2D1RenderTarget_EndDraw((ID2D1RenderTarget *)c, NULL, NULL);
                        printf("(drew with a context of this device)\n");
                    }
                }
                for (i = 0; i < ARRAYSIZE(args); i++)
                {
                    UINT64 buf[4];
                    HRESULT h;

                    memset(buf, 0xaa, sizeof(buf));
                    h = ((m5_t)(*(void ***)priv)[5])(priv, args[i][0], args[i][1], buf);
                    printf("slot 5 (%#llx, %#llx): hr %08lx out %016llx %016llx %016llx\n", (unsigned long long)args[i][0],
                           (unsigned long long)args[i][1], h, buf[0], buf[1], buf[2]);
                }
            }
            if (priv)
            {
                HRESULT h = ((m5_t)(*(void ***)priv)[5])(priv, 2000, 0x1e0000, NULL);
                printf("slot 5 with a NULL output: hr %08lx\n", h);
            }
        }
    }

    {
        /* the vtables of known Direct2D objects, to name the objects that the private methods take and return (by matching the
         * module offsets of their vtables with a trace of the same d2d1.dll) */
        D2D1_BITMAP_PROPERTIES1 props = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                                         D2D1_BITMAP_OPTIONS_TARGET, NULL};
        D2D1_SIZE_U size = {16, 16};
        D2D1_COLOR_F color = {1.0f, 0.0f, 0.0f, 1.0f};
        D2D1_GRADIENT_STOP stops[2] = {{0.0f, {0, 0, 0, 1}}, {1.0f, {1, 1, 1, 1}}};
        ID2D1Bitmap1 *bitmap = NULL, *plain = NULL;
        ID2D1SolidColorBrush *brush = NULL;
        ID2D1CommandList *list = NULL;
        ID2D1GradientStopCollection *grad = NULL;
        ID2D1Effect *effect = NULL;
        ID2D1StrokeStyle *stroke = NULL;
        ID2D1PathGeometry *path = NULL;
        ID2D1RectangleGeometry *rect = NULL;
        D2D1_RECT_F r = {0, 0, 1, 1};
        D2D1_STROKE_STYLE_PROPERTIES sp = {0};
        char a[160];
        IDXGISurface *surface = NULL;
        ID3D11Texture2D *tex = NULL;
        D3D11_TEXTURE2D_DESC td = {16, 16, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                                   D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
        ID2D1Bitmap1 *surfbitmap = NULL;

#define SHOW(name, obj) do { if (obj) { where(*(void **)(obj), a, sizeof(a), NULL); printf("vtable of %s: %s\n", name, a); } \
                             else printf("%s: not created\n", name); } while (0)
        SHOW("ID2D1Factory1", factory);
        SHOW("ID2D1Device", device);
        SHOW("ID2D1DeviceContext", context);
        (void)size; (void)bitmap; (void)plain;
        if (SUCCEEDED(ID3D11Device_CreateTexture2D(d3d, &td, NULL, &tex)) &&
            SUCCEEDED(ID3D11Texture2D_QueryInterface(tex, &IID_IDXGISurface, (void **)&surface)))
        {
            props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
            ID2D1DeviceContext_CreateBitmapFromDxgiSurface(context, surface, &props, &surfbitmap);
        }
        SHOW("ID2D1Bitmap1 (from a DXGI surface)", surfbitmap);
        ID2D1RenderTarget_CreateSolidColorBrush((ID2D1RenderTarget *)context, &color, NULL, &brush);
        SHOW("ID2D1SolidColorBrush", brush);
        ID2D1DeviceContext_CreateCommandList(context, &list);
        SHOW("ID2D1CommandList", list);
        (void)stops; (void)grad;
        ID2D1DeviceContext_CreateEffect(context, &CLSID_D2D1GaussianBlur, &effect);
        SHOW("ID2D1Effect (blur)", effect);
        ID2D1Factory_CreateStrokeStyle((ID2D1Factory *)factory, &sp, NULL, 0, &stroke);
        SHOW("ID2D1StrokeStyle", stroke);
        ID2D1Factory_CreatePathGeometry((ID2D1Factory *)factory, &path);
        SHOW("ID2D1PathGeometry", path);
        ID2D1Factory_CreateRectangleGeometry((ID2D1Factory *)factory, &r, &rect);
        SHOW("ID2D1RectangleGeometry", rect);
#undef SHOW

        /* Slot 12 of the private interface, as traced in Notepad: (iface, ID2D1Bitmap1 *, D2D1_BITMAP_PROPERTIES1 * {A8_UNORM,
         * premultiplied, 96, 96, TARGET, NULL}, out ID2D1Bitmap1 *). Called here on bitmaps of this program, to see what it returns. */
        if (surfbitmap)
        {
            typedef HRESULT (STDMETHODCALLTYPE *m12_t)(void *, void *, const D2D1_BITMAP_PROPERTIES1 *, void **);
            static const D2D1_BITMAP_PROPERTIES1 formats[] =
            {
                {{DXGI_FORMAT_A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f, D2D1_BITMAP_OPTIONS_TARGET, NULL},
                {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f, D2D1_BITMAP_OPTIONS_TARGET, NULL},
                {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 192.0f, 192.0f, D2D1_BITMAP_OPTIONS_TARGET, NULL},
                {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE}, 96.0f, 96.0f, D2D1_BITMAP_OPTIONS_NONE, NULL},
                {{DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f, D2D1_BITMAP_OPTIONS_TARGET, NULL},
                {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE}, 96.0f, 96.0f, D2D1_BITMAP_OPTIONS_TARGET, NULL},
                {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f, D2D1_BITMAP_OPTIONS_NONE, NULL},
                {{DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN}, 0.0f, 0.0f, D2D1_BITMAP_OPTIONS_TARGET, NULL},
                {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
                 D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, NULL},
            };
            void *priv = NULL;
            unsigned int i;

            IUnknown_QueryInterface((IUnknown *)context, &iid_d2d_private, &priv);
            for (i = 0; priv && i < ARRAYSIZE(formats); i++)
            {
                ID2D1Bitmap1 *out = NULL;
                HRESULT hr12 = ((m12_t)(*(void ***)priv)[12])(priv, surfbitmap, &formats[i], (void **)&out);

                printf("slot 12 on a B8G8R8A8 surface bitmap, format %u alpha %u dpi %g options %#x: hr %08lx out %p",
                       formats[i].pixelFormat.format, formats[i].pixelFormat.alphaMode, formats[i].dpiX, formats[i].bitmapOptions,
                       hr12, out);
                if (SUCCEEDED(hr12) && out)
                {
                    D2D1_PIXEL_FORMAT pf;
                    D2D1_SIZE_U ps;

                    /* ID2D1Bitmap::GetPixelSize is slot 5 and GetPixelFormat slot 6; structure returns take a hidden pointer */
                    ((void (STDMETHODCALLTYPE *)(void *, D2D1_SIZE_U *))(*(void ***)out)[5])(out, &ps);
                    ((void (STDMETHODCALLTYPE *)(void *, D2D1_PIXEL_FORMAT *))(*(void ***)out)[6])(out, &pf);
                    IDXGISurface *s1 = NULL, *s2 = NULL;
                    D2D1_BITMAP_OPTIONS o = ID2D1Bitmap1_GetOptions(out);

                    ID2D1Bitmap1_GetSurface(out, &s1);
                    ID2D1Bitmap1_GetSurface(surfbitmap, &s2);
                    where(*(void **)out, a, sizeof(a), NULL);
                    printf(" -> %s the source; vtable %s; format %u alpha %u; %ux%u; options %#x; %s surface",
                           (void *)out == (void *)surfbitmap ? "is" : "is not", a, pf.format, pf.alphaMode, ps.width, ps.height,
                           o, !s1 ? "no" : s1 == s2 ? "the same" : "another");
                    if (s1 && s2)
                    {
                        DXGI_SURFACE_DESC d1, d2;
                        IDXGISurface_GetDesc(s1, &d1); IDXGISurface_GetDesc(s2, &d2);
                        printf("; surface formats %u / %u", d1.Format, d2.Format);
                    }
                    if (s1) IDXGISurface_Release(s1);
                    if (s2) IDXGISurface_Release(s2);
                    IUnknown_Release((IUnknown *)out);
                }
                printf("\n");
                {
                    /* the public call with the same properties on the surface of the source, for comparison */
                    ID2D1Bitmap1 *pub = NULL;
                    HRESULT hrp = ID2D1DeviceContext_CreateBitmapFromDxgiSurface(context, surface, &formats[i], &pub);

                    printf("    CreateBitmapFromDxgiSurface with the same properties: hr %08lx\n", hrp);
                    if (pub) IUnknown_Release((IUnknown *)pub);
                }
            }
            if (priv)
            {
                /* a NULL source, NULL properties, NULL output */
                typedef HRESULT (STDMETHODCALLTYPE *m12_t2)(void *, void *, const D2D1_BITMAP_PROPERTIES1 *, void **);
                ID2D1Bitmap1 *out = NULL;
                HRESULT h;

                h = ((m12_t2)(*(void ***)priv)[12])(priv, surfbitmap, NULL, (void **)&out);
                printf("slot 12 with NULL properties: hr %08lx out %p\n", h, out);
                if (out) IUnknown_Release((IUnknown *)out);
                out = (void *)0xdead;
                h = ((m12_t2)(*(void ***)priv)[12])(priv, NULL, &formats[1], (void **)&out);
                printf("slot 12 with a NULL source: hr %08lx out %p\n", h, out);
            }
        }
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&dwrite);
    hr = IDWriteFactory_CreateTextFormat(dwrite, L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &format);
    if (SUCCEEDED(hr)) query((IUnknown *)format, "{28d5197f} on a text format", &iid_format_private);
    return 0;
}
