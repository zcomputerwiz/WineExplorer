/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe (Windows 11 and Wine): ID2D1Geometry::Outline and GetWidenedBounds. Prints what Outline gives a simplified-geometry sink
 * (fill mode, figures, begin/end flags, segment kinds, point count and the figures' bounds) for overlapping rectangles in one path,
 * a path with a hollow figure, a rounded rectangle and a transformed case; and the widened bounds of a rectangle path with and
 * without a transform and of an ellipse.
 */
#include <windows.h>
#include <d2d1.h>
#include <stdio.h>
#include <float.h>

struct sink : public ID2D1SimplifiedGeometrySink
{
    LONG ref = 1;
    int figures = 0, lines = 0, beziers = 0, filled = 0, hollow = 0, closed = 0, open = 0, fill_mode = -1;
    float l = FLT_MAX, t = FLT_MAX, r = -FLT_MAX, b = -FLT_MAX;
    void add(D2D1_POINT_2F p) { if (p.x < l) l = p.x; if (p.y < t) t = p.y; if (p.x > r) r = p.x; if (p.y > b) b = p.y; }
    STDMETHOD(QueryInterface)(REFIID iid, void **out)
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(ID2D1SimplifiedGeometrySink)) { *out = this; AddRef(); return S_OK; }
        *out = NULL; return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() { return ++ref; }
    STDMETHOD_(ULONG, Release)() { return --ref; }
    STDMETHOD_(void, SetFillMode)(D2D1_FILL_MODE m) { fill_mode = m; }
    STDMETHOD_(void, SetSegmentFlags)(D2D1_PATH_SEGMENT) {}
    BOOL verbose = FALSE;
    STDMETHOD_(void, BeginFigure)(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN f)
    {
        figures++; if (f == D2D1_FIGURE_BEGIN_FILLED) filled++; else hollow++; add(p);
        if (verbose) printf("    figure %s at %.2f,%.2f:", f == D2D1_FIGURE_BEGIN_FILLED ? "filled" : "hollow", p.x, p.y);
    }
    STDMETHOD_(void, AddLines)(const D2D1_POINT_2F *p, UINT32 n)
    {
        lines += n;
        for (UINT32 i = 0; i < n; i++) { add(p[i]); if (verbose) printf(" %.2f,%.2f", p[i].x, p[i].y); }
    }
    STDMETHOD_(void, AddBeziers)(const D2D1_BEZIER_SEGMENT *s, UINT32 n) { beziers += n; for (UINT32 i = 0; i < n; i++) add(s[i].point3); }
    STDMETHOD_(void, EndFigure)(D2D1_FIGURE_END e)
    {
        if (e == D2D1_FIGURE_END_CLOSED) closed++; else open++;
        if (verbose) printf(" [%s]\n", e == D2D1_FIGURE_END_CLOSED ? "closed" : "open");
    }
    STDMETHOD(Close)() { return S_OK; }
};

static ID2D1Factory *factory;

static void outline(const char *name, ID2D1Geometry *g, const D2D1_MATRIX_3X2_F *m)
{
    sink s;
    ID2D1PathGeometry *out;
    ID2D1GeometrySink *gs;
    float area = -1.0f;
    s.verbose = getenv("PROBE_VERBOSE") != NULL;
    HRESULT hr = g->Outline(m, 0.25f, &s);
    /* the area the outline covers (the same outline written into a path geometry) */
    factory->CreatePathGeometry(&out);
    out->Open(&gs);
    g->Outline(m, 0.25f, gs);
    gs->Close();
    out->ComputeArea(NULL, 0.25f, &area);
    gs->Release();
    out->Release();
    printf("%-34s hr %#lx fill %d figures %d (filled %d hollow %d, closed %d open %d) lines %d beziers %d bounds %.2f,%.2f-%.2f,%.2f area %.1f\n",
           name, hr, s.fill_mode, s.figures, s.filled, s.hollow, s.closed, s.open, s.lines, s.beziers, s.l, s.t, s.r, s.b, area);
}

static void widened(const char *name, ID2D1Geometry *g, float w, const D2D1_MATRIX_3X2_F *m)
{
    D2D1_RECT_F r = {0};
    HRESULT hr = g->GetWidenedBounds(w, NULL, m, 0.25f, &r);
    printf("%-34s hr %#lx bounds %.2f,%.2f-%.2f,%.2f\n", name, hr, r.left, r.top, r.right, r.bottom);
}

static ID2D1PathGeometry *path(ID2D1Factory *f, const D2D1_RECT_F *rects, int n, BOOL hollow_last)
{
    ID2D1PathGeometry *p;
    ID2D1GeometrySink *gs;
    f->CreatePathGeometry(&p);
    p->Open(&gs);
    for (int i = 0; i < n; i++)
    {
        D2D1_POINT_2F pts[3] = {{rects[i].right, rects[i].top}, {rects[i].right, rects[i].bottom}, {rects[i].left, rects[i].bottom}};
        gs->BeginFigure(D2D1::Point2F(rects[i].left, rects[i].top),
                        hollow_last && i == n - 1 ? D2D1_FIGURE_BEGIN_HOLLOW : D2D1_FIGURE_BEGIN_FILLED);
        gs->AddLines(pts, 3);
        gs->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    gs->Close();
    gs->Release();
    return p;
}

int main()
{
    ID2D1Factory *f;
    D2D1_RECT_F two[2] = {{0, 0, 10, 10}, {5, 5, 15, 15}}, one[1] = {{0, 0, 10, 10}};
    D2D1_MATRIX_3X2_F scale = D2D1::Matrix3x2F::Scale(2, 3), move = D2D1::Matrix3x2F::Translation(100, 50);
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f))) { printf("no factory\n"); return 1; }
    factory = f;

    ID2D1PathGeometry *p2 = path(f, two, 2, FALSE), *ph = path(f, two, 2, TRUE), *p1 = path(f, one, 1, FALSE);
    outline("path, two overlapping rects", p2, NULL);
    outline("path, second rect hollow", ph, NULL);
    outline("path, two rects, scale 2x3", p2, &scale);
    outline("path, two rects, translate", p2, &move);

    ID2D1RoundedRectangleGeometry *rr;
    f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(0, 0, 40, 20), 4, 4), &rr);
    outline("rounded rect 40x20 r4", rr, NULL);
    {
        /* the rounded rectangle itself: its area, and what Simplify (lines) gives */
        float a = -1.0f;
        sink s;
        rr->ComputeArea(NULL, 0.25f, &a);
        rr->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES, NULL, 0.25f, &s);
        printf("%-34s area %.1f; Simplify(lines): figures %d lines %d bounds %.2f,%.2f-%.2f,%.2f\n", "rounded rect itself", a,
               s.figures, s.lines, s.l, s.t, s.r, s.b);
        sink s2;
        rr->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES, NULL, 0.25f, &s2);
        printf("%-34s Simplify(cubics): figures %d lines %d beziers %d bounds %.2f,%.2f-%.2f,%.2f\n", "rounded rect itself",
               s2.figures, s2.lines, s2.beziers, s2.l, s2.t, s2.r, s2.b);
    }
    ID2D1RectangleGeometry *rc;
    f->CreateRectangleGeometry(D2D1::RectF(1, 2, 11, 12), &rc);
    outline("rectangle 1,2-11,12", rc, NULL);

    widened("widened path rect, w2", p1, 2.0f, NULL);
    widened("widened path rect, w2, scale 2x3", p1, 2.0f, &scale);
    widened("widened path rect, w0", p1, 0.0f, NULL);
    ID2D1EllipseGeometry *el;
    f->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(10, 10), 5, 3), &el);
    widened("widened ellipse 5x3, w4", el, 4.0f, NULL);
    widened("widened ellipse 5x3, w4, scale 2x3", el, 4.0f, &scale);
    widened("widened rounded rect, w1, scale 2x3", rr, 1.0f, &scale);
    outline("ellipse 5x3 at 10,10", el, NULL);
    {
        float a = -1.0f;
        sink s;
        el->ComputeArea(NULL, 0.25f, &a);
        el->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES, NULL, 0.25f, &s);
        printf("%-34s area %.1f; Simplify(cubics): figures %d lines %d beziers %d bounds %.2f,%.2f-%.2f,%.2f\n", "ellipse itself", a,
               s.figures, s.lines, s.beziers, s.l, s.t, s.r, s.b);
    }
    widened("widened rounded rect, w1", rr, 1.0f, NULL);
    return 0;
}
