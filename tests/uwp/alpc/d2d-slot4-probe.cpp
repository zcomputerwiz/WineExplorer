/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe (Windows 11 and Wine): which Direct2D objects take part in private factory slot 4, the slot the Windows App SDK
 * compositor (dwmcorei.dll) calls as (factory, 1, geometry, NULL, object, &{1.0f, -0.0f}, 0.25f, &out). Prints the vtable offset
 * (from the d2d1.dll base) of the public objects that could be the geometry, the fifth argument and the result (geometries,
 * stroke style, device, device context, geometry realizations, brush), and of the private interface; then calls slot 15 (as the
 * compositor does) and slot 4 with each candidate fifth argument and prints what slot 4 wrote. Black-box: interface calls only.
 * Usage: d2d-slot4-probe [candidate]  (no candidate: offsets only)
 */
#include <windows.h>
#include <d3d11.h>
#include <d2d1_3.h>
#include <stdio.h>
#include <stdlib.h>

static const GUID IID_d2d_private = {0x6f72c0a2, 0x6db7, 0x46e9, {0x9b, 0x62, 0xb5, 0x8a, 0x23, 0xf4, 0x92, 0x8b}};
static ULONG_PTR base;

static void show(const char *name, void *obj)
{
    ULONG_PTR vt;

    if (!obj) { printf("%-28s (none)\n", name); return; }
    vt = *(ULONG_PTR *)obj;
    if (vt >= base && vt < base + 0x1000000) printf("%-28s %p vtable d2d1+%#llx\n", name, obj, (unsigned long long)(vt - base));
    else printf("%-28s %p vtable %p (not in d2d1)\n", name, obj, (void *)vt);
}

typedef HRESULT (STDMETHODCALLTYPE *slot15_fn)(void *iface, UINT_PTR options, IUnknown *device, void **out);
typedef HRESULT (STDMETHODCALLTYPE *slot4_fn)(void *iface, UINT_PTR mode, void *geometry, void *matrix, void *object,
                                             const float *vec, float tolerance, void **out);

int main(int argc, char **argv)
{
    ID2D1Factory1 *factory;
    ID3D11Device *d3d;
    IDXGIDevice *dxgi;
    ID2D1Device *device;
    ID2D1DeviceContext *ctx0;
    ID2D1DeviceContext1 *ctx = NULL;
    ID2D1RectangleGeometry *rect;
    ID2D1RoundedRectangleGeometry *rrect;
    ID2D1EllipseGeometry *ell;
    ID2D1PathGeometry *path;
    ID2D1GeometryGroup *group;
    ID2D1TransformedGeometry *xformed;
    ID2D1StrokeStyle *stroke;
    ID2D1GeometryRealization *filled = NULL, *stroked = NULL;
    ID2D1SolidColorBrush *brush;
    D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Scale(2, 2);
    void *priv = NULL, *cdev = NULL;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    base = (ULONG_PTR)LoadLibraryA("d2d1.dll");
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), NULL, (void **)&factory)))
    { printf("factory %#lx\n", hr); return 1; }
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
    if (FAILED(hr)) { printf("d3d11 %#lx\n", hr); return 1; }
    d3d->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi);
    factory->CreateDevice(dxgi, &device);
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx0);
    ctx0->QueryInterface(__uuidof(ID2D1DeviceContext1), (void **)&ctx);

    factory->CreateRectangleGeometry(D2D1::RectF(0, 26, 4, 30), &rect);
    factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(0, 0, 40, 20), 4, 4), &rrect);
    factory->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(10, 10), 5, 3), &ell);
    factory->CreatePathGeometry(&path);
    {
        ID2D1GeometrySink *s;
        path->Open(&s);
        s->BeginFigure(D2D1::Point2F(0, 0), D2D1_FIGURE_BEGIN_FILLED);
        s->AddLine(D2D1::Point2F(10, 0));
        s->AddLine(D2D1::Point2F(10, 10));
        s->EndFigure(D2D1_FIGURE_END_CLOSED);
        s->Close();
        s->Release();
    }
    {
        ID2D1Geometry *two[2] = {rect, ell};
        factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, two, 2, &group);
    }
    factory->CreateTransformedGeometry(rect, &m, &xformed);
    factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(), NULL, 0, &stroke);
    if (ctx)
    {
        ctx->CreateFilledGeometryRealization(rect, 0.25f, &filled);
        ctx->CreateStrokedGeometryRealization(rect, 0.25f, 1.0f, NULL, &stroked);
    }
    ctx0->CreateSolidColorBrush(D2D1::ColorF(1, 0, 0), &brush);

    show("factory", factory);
    show("device", device);
    show("device context", ctx0);
    show("rectangle geometry", rect);
    show("rounded rectangle geometry", rrect);
    show("ellipse geometry", ell);
    show("path geometry", path);
    show("geometry group", group);
    show("transformed geometry", xformed);
    show("stroke style", stroke);
    show("filled realization", filled);
    show("stroked realization", stroked);
    show("solid brush", brush);
    hr = factory->QueryInterface(IID_d2d_private, &priv);
    printf("private interface: hr %#lx\n", hr);
    if (FAILED(hr)) return 0;
    show("private interface", priv);
    (void)cdev;
    if (argc < 2) return 0;
    {
        /* slot 4 as the compositor calls it, with a 10x10 square at 0,0 and a path square 5..15 x 0..10 (Windows: a rectangle
         * geometry and a path geometry): mode argv[1] (the combine modes are union 0, intersect 1, xor 2, exclude 3), no first
         * transform, an identity matrix with -0 (as measured) or a translation by argv[2] for the second; then the bounds and area
         * of what it wrote */
        ID2D1RectangleGeometry *a;
        ID2D1PathGeometry *b;
        ID2D1GeometrySink *s;
        float mat[6] = {1.0f, -0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
        int mode = atoi(argv[1]);
        void *out = NULL;
        D2D1_RECT_F r = {0};
        float area = -1;

        float mat1[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};

        if (argc > 2) mat[4] = (float)atof(argv[2]);
        /* argv[3]: a translation of the first geometry (its own transform) */
        if (argc > 3) mat1[4] = (float)atof(argv[3]);
        factory->CreateRectangleGeometry(D2D1::RectF(0, 0, 10, 10), &a);
        factory->CreatePathGeometry(&b);
        b->Open(&s);
        s->BeginFigure(D2D1::Point2F(5, 0), D2D1_FIGURE_BEGIN_FILLED);
        s->AddLine(D2D1::Point2F(15, 0));
        s->AddLine(D2D1::Point2F(15, 10));
        s->AddLine(D2D1::Point2F(5, 10));
        s->EndFigure(D2D1_FIGURE_END_CLOSED);
        s->Close();
        s->Release();
        hr = ((slot4_fn)(*(void ***)priv)[4])(priv, mode, a, argc > 3 ? mat1 : NULL, b, mat, 0.25f, &out);
        printf("slot 4 mode %d dx %g dx1 %g: hr %#lx\n", mode, mat[4], mat1[4], hr);
        show("slot 4 out", out);
        if (out)
        {
            IUnknown *u = (IUnknown *)out, *q;
            if (SUCCEEDED(u->QueryInterface(__uuidof(ID2D1PathGeometry), (void **)&q)))
            {
                UINT32 figs = 0, segs = 0;
                ((ID2D1PathGeometry *)q)->GetFigureCount(&figs);
                ((ID2D1PathGeometry *)q)->GetSegmentCount(&segs);
                printf("  is an ID2D1PathGeometry: %u figures, %u segments\n", figs, segs);
                q->Release();
            }
            if (SUCCEEDED(u->QueryInterface(__uuidof(ID2D1Geometry), (void **)&q)))
            {
                ((ID2D1Geometry *)q)->GetBounds(NULL, &r);
                ((ID2D1Geometry *)q)->ComputeArea(NULL, 0.25f, &area);
                printf("  is an ID2D1Geometry: bounds %.2f,%.2f-%.2f,%.2f area %.2f\n", r.left, r.top, r.right, r.bottom, area);
                q->Release();
            }
            printf("  refcount after AddRef/Release: %lu\n", (u->AddRef(), u->Release()));
        }
    }
    return 0;
}
