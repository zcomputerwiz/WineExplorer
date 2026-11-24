/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe of the documented theme animation queries of uxtheme (GetThemeAnimationProperty, GetThemeAnimationTransform,
 * GetThemeTimingFunction): what they return for a NULL theme, for theme classes, for small buffers. Only the results of the calls
 * are printed. Build (mingw): x86_64-w64-mingw32-gcc -O2 -o uxtheme-anim-probe.exe uxtheme-anim-probe.c -luxtheme
 */
#include <windows.h>
#include <uxtheme.h>
#include <stdio.h>

typedef HRESULT (WINAPI *prop_t)(HTHEME, int, int, int, void *, DWORD, DWORD *);
typedef HRESULT (WINAPI *trans_t)(HTHEME, int, int, DWORD, void *, DWORD, DWORD *);
typedef HRESULT (WINAPI *timing_t)(HTHEME, int, void *, DWORD, DWORD *);

int main(void)
{
    static const WCHAR *classes[] = { L"BUTTON", L"DragDrop", L"LISTVIEW", L"TASKDIALOG", L"Explorer::TreeView" };
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    prop_t prop = (prop_t)GetProcAddress(ux, "GetThemeAnimationProperty");
    trans_t trans = (trans_t)GetProcAddress(ux, "GetThemeAnimationTransform");
    timing_t timing = (timing_t)GetProcAddress(ux, "GetThemeTimingFunction");
    BYTE buf[256];
    DWORD out;
    HRESULT hr;
    unsigned int i;
    int sb, tg, p;

    printf("exports: property %p transform %p timing %p\n", prop, trans, timing);
    if (!prop) return 1;
    out = 0xdead;
    hr = prop(NULL, 1, 1, 0, buf, sizeof(buf), &out);
    printf("property NULL theme: hr %08lx out %#lx\n", hr, out);
    out = 0xdead;
    if (trans) { hr = trans(NULL, 1, 1, 0, buf, sizeof(buf), &out); printf("transform NULL theme: hr %08lx out %#lx\n", hr, out); }
    out = 0xdead;
    if (timing) { hr = timing(NULL, 1, buf, sizeof(buf), &out); printf("timing NULL theme: hr %08lx out %#lx\n", hr, out); }

    for (i = 0; i < ARRAYSIZE(classes); i++)
    {
        HTHEME th = OpenThemeData(NULL, classes[i]);
        int found = 0;

        printf("%ls: theme %p\n", classes[i], th);
        if (!th) continue;
        for (sb = 0; sb <= 20; sb++)
            for (tg = 0; tg <= 3; tg++)
                for (p = 0; p <= 5; p++)
                {
                    memset(buf, 0xcc, sizeof(buf));
                    out = 0xdead;
                    hr = prop(th, sb, tg, p, buf, sizeof(buf), &out);
                    if (hr == S_OK && found < 6)
                    {
                        printf("  property storyboard %d target %d property %d: S_OK out %lu value %08lx\n", sb, tg, p, out, *(DWORD *)buf);
                        found++;
                    }
                    else if (sb == 1 && tg == 1 && p == 0) printf("  property 1/1/0: hr %08lx out %#lx\n", hr, out);
                }
        out = 0xdead;
        hr = prop(th, 1, 1, 0, buf, 0, &out);
        printf("  property 1/1/0 size 0: hr %08lx out %#lx\n", hr, out);
        for (p = -1; p <= 8; p++)
        {
            out = 0xdead;
            hr = prop(th, 1, 1, p, buf, sizeof(buf), &out);
            printf("  property 1/1/%d: hr %08lx out %#lx\n", p, hr, out);
        }
        out = 0xdead;
        hr = prop(th, 1, 1, 0, NULL, 4, &out);
        printf("  property 1/1/0 NULL buffer: hr %08lx out %#lx\n", hr, out);
        hr = prop(th, 1, 1, 0, buf, 4, NULL);
        printf("  property 1/1/0 NULL size out: hr %08lx\n", hr);
        out = 0xdead;
        hr = prop(th, 1, 1, 0, buf, 3, &out);
        printf("  property 1/1/0 size 3: hr %08lx out %#lx\n", hr, out);
        if (trans)
        {
            out = 0xdead;
            hr = trans(th, 1, 1, 0, buf, 0, &out);
            printf("  transform size 0: hr %08lx out %#lx\n", hr, out);
        }
        if (timing)
        {
            out = 0xdead;
            hr = timing(th, 1, buf, 0, &out);
            printf("  timing size 0: hr %08lx out %#lx\n", hr, out);
        }
        if (i) { CloseThemeData(th); continue; }
        if (trans)
        {
            out = 0xdead;
            hr = trans(th, 1, 1, 0, buf, sizeof(buf), &out);
            printf("  transform 1/1/0: hr %08lx out %#lx\n", hr, out);
        }
        if (timing)
        {
            out = 0xdead;
            hr = timing(th, 1, buf, sizeof(buf), &out);
            printf("  timing 1: hr %08lx out %#lx\n", hr, out);
        }
        CloseThemeData(th);
    }
    return 0;
}
