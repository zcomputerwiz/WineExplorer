/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: what ID2D1Geometry::CombineWithGeometry of Windows writes to a simplified geometry sink, for the four combine
 * modes on simple shapes (nested, overlapping and disjoint rectangles, a rounded rectangle, an ellipse, a transform, a path with
 * each fill mode). Each sink call is printed (fill mode, figure begin/end flags, lines, beziers), so Wine's implementation can
 * produce the same kind of output. Only the results of documented calls are printed.
 * Build (mingw): x86_64-w64-mingw32-g++ -O2 -o d2d-combine-probe.exe d2d-combine-probe.cpp -ld2d1 -luuid
 */
#include <windows.h>
#include <d2d1.h>
#include <stdio.h>

class Sink : public ID2D1SimplifiedGeometrySink
{
public:
    unsigned int lines = 0, beziers = 0, figures = 0;

    STDMETHOD(QueryInterface)(REFIID iid, void **out) override
    {
        if (iid == __uuidof(ID2D1SimplifiedGeometrySink) || iid == __uuidof(IUnknown)) { *out = this; return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return 2; }
    STDMETHOD_(ULONG, Release)() override { return 1; }
    STDMETHOD_(void, SetFillMode)(D2D1_FILL_MODE mode) override
    {
        printf("    SetFillMode %s\n", mode == D2D1_FILL_MODE_ALTERNATE ? "ALTERNATE" : "WINDING");
    }
    STDMETHOD_(void, SetSegmentFlags)(D2D1_PATH_SEGMENT flags) override { printf("    SetSegmentFlags %#x\n", flags); }
    STDMETHOD_(void, BeginFigure)(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN begin) override
    {
        figures++;
        printf("    BeginFigure (%.4g, %.4g) %s\n", p.x, p.y, begin == D2D1_FIGURE_BEGIN_FILLED ? "FILLED" : "HOLLOW");
    }
    STDMETHOD_(void, AddLines)(const D2D1_POINT_2F *p, UINT32 count) override
    {
        UINT32 i;
        lines += count;
        printf("    AddLines %u:", count);
        for (i = 0; i < count && i < 10; i++) printf(" (%.4g, %.4g)", p[i].x, p[i].y);
        if (i < count) printf(" ...");
        printf("\n");
    }
    STDMETHOD_(void, AddBeziers)(const D2D1_BEZIER_SEGMENT *b, UINT32 count) override
    {
        UINT32 i;
        beziers += count;
        printf("    AddBeziers %u:", count);
        for (i = 0; i < count && i < 2; i++)
            printf(" [(%.4g, %.4g) (%.4g, %.4g) (%.4g, %.4g)]", b[i].point1.x, b[i].point1.y, b[i].point2.x, b[i].point2.y,
                   b[i].point3.x, b[i].point3.y);
        if (i < count) printf(" ...");
        printf("\n");
    }
    STDMETHOD_(void, EndFigure)(D2D1_FIGURE_END end) override
    {
        printf("    EndFigure %s\n", end == D2D1_FIGURE_END_CLOSED ? "CLOSED" : "OPEN");
    }
    STDMETHOD(Close)() override { printf("    Close\n"); return S_OK; }
};

static const char *mode_name(int m)
{
    static const char *names[] = {"UNION", "INTERSECT", "XOR", "EXCLUDE"};
    return m >= 0 && m <= 3 ? names[m] : "?";
}

static void combine(const char *what, ID2D1Geometry *a, ID2D1Geometry *b, const D2D1_MATRIX_3X2_F *t, float tolerance)
{
    for (int m = D2D1_COMBINE_MODE_UNION; m <= D2D1_COMBINE_MODE_EXCLUDE; m++)
    {
        Sink s;
        printf("%s, %s:\n", what, mode_name(m));
        HRESULT hr = a->CombineWithGeometry(b, (D2D1_COMBINE_MODE)m, t, tolerance, &s);
        printf("  hr %08lx: %u figures, %u lines, %u beziers\n", hr, s.figures, s.lines, s.beziers);
    }
}

static ID2D1PathGeometry *bowtie(ID2D1Factory *factory, D2D1_FILL_MODE mode)
{
    /* two squares in one figure each, the second inside the first (a hole with ALTERNATE, filled with WINDING) */
    ID2D1PathGeometry *path;
    ID2D1GeometrySink *sink;
    D2D1_POINT_2F a[] = {{100, 0}, {100, 100}, {0, 100}}, b[] = {{70, 30}, {70, 70}, {30, 70}};

    factory->CreatePathGeometry(&path);
    path->Open(&sink);
    sink->SetFillMode(mode);
    sink->BeginFigure(D2D1::Point2F(0, 0), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(a, 3);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->BeginFigure(D2D1::Point2F(30, 30), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(b, 3);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    return path;
}

int main()
{
    ID2D1Factory *factory;
    ID2D1RectangleGeometry *outer, *inner, *right, *disjoint, *band;
    ID2D1RoundedRectangleGeometry *rounded;
    ID2D1EllipseGeometry *ellipse;
    D2D1_MATRIX_3X2_F shift = D2D1::Matrix3x2F::Translation(10, 0);

    setvbuf(stdout, NULL, _IONBF, 0);
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory))) return 1;
    factory->CreateRectangleGeometry(D2D1::RectF(0, 0, 100, 100), &outer);
    factory->CreateRectangleGeometry(D2D1::RectF(20, 20, 80, 80), &inner);
    factory->CreateRectangleGeometry(D2D1::RectF(50, 50, 150, 150), &right);
    factory->CreateRectangleGeometry(D2D1::RectF(200, 200, 210, 210), &disjoint);
    factory->CreateRectangleGeometry(D2D1::RectF(-10, 40, 110, 60), &band);
    factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(0, 0, 100, 40), 8, 8), &rounded);
    factory->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(100, 100), 30, 30), &ellipse);

    combine("rect 0..100 with nested rect 20..80", outer, inner, NULL, 0.25f);
    combine("rect 0..100 with overlapping rect 50..150", outer, right, NULL, 0.25f);
    combine("rect 0..100 with disjoint rect 200..210", outer, disjoint, NULL, 0.25f);
    combine("rect 0..100 with a band crossing it (-10..110 x 40..60)", outer, band, NULL, 0.25f);
    combine("rect 0..100 with nested rect, the second shifted by 10", outer, inner, &shift, 0.25f);
    combine("rect 0..100 with ellipse at 100,100 r30", outer, ellipse, NULL, 0.25f);
    combine("rounded rect 100x40 r8 with nested rect 20..80", rounded, inner, NULL, 0.25f);
    combine("rect with itself", outer, outer, NULL, 0.25f);
    combine("path of two nested squares ALTERNATE with rect 20..80", bowtie(factory, D2D1_FILL_MODE_ALTERNATE), inner, NULL, 0.25f);
    combine("path of two nested squares WINDING with rect 20..80", bowtie(factory, D2D1_FILL_MODE_WINDING), inner, NULL, 0.25f);
    combine("rect 20..80 with path of two nested squares ALTERNATE", inner, bowtie(factory, D2D1_FILL_MODE_ALTERNATE), NULL, 0.25f);

    Sink s;
    printf("invalid combine mode 4: hr %08lx\n", outer->CombineWithGeometry(inner, (D2D1_COMBINE_MODE)4, NULL, 0.25f, &s));
    printf("tolerance 0: hr %08lx\n", outer->CombineWithGeometry(inner, D2D1_COMBINE_MODE_UNION, NULL, 0.0f, &s));
    printf("tolerance -1: hr %08lx\n", outer->CombineWithGeometry(inner, D2D1_COMBINE_MODE_UNION, NULL, -1.0f, &s));
    return 0;
}
