/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: D2D1CreateFactory (multi-threaded, ID2D1Factory1) and QueryInterface of the factory for the interfaces that
 * dwmcorei.dll of the Windows App Runtime asks for in its DllMain ({6f72c0a2-6db7-46e9-9b62-b58a23f4928b}). Prints HRESULTs,
 * whether the pointer differs from the factory, and the first words of the vtable. Run on Windows 11 and on Wine.
 * Build: x86_64-w64-mingw32-gcc -O2 -o d2dqi-probe.exe d2dqi-probe.c -luuid
 */
#include <stdio.h>
#include <windows.h>
#include <unknwn.h>

typedef HRESULT (WINAPI *create_factory_t)(int type, REFIID riid, const void *options, void **out);

static void qi(IUnknown *obj, const char *name, GUID iid)
{
    void *p = NULL;
    HRESULT hr = obj->lpVtbl->QueryInterface(obj, &iid, &p);
    printf("QI %s: hr=%08lx ptr=%s", name, hr, p ? (p == (void *)obj ? "same" : "other") : "null");
    if (p)
    {
        void **vtbl = *(void ***)p;
        MEMORY_BASIC_INFORMATION mbi;
        char mod[MAX_PATH] = "?";
        if (VirtualQuery(vtbl[3], &mbi, sizeof(mbi))) GetModuleFileNameA(mbi.AllocationBase, mod, sizeof(mod));
        printf(" slot3-in=%s", mod);
        ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
    }
    printf("\n");
}

int main(void)
{
    HMODULE d2d1 = LoadLibraryA("d2d1.dll");
    create_factory_t create = (create_factory_t)GetProcAddress(d2d1, "D2D1CreateFactory");
    GUID factory1 = {0xbb12d362, 0xdaee, 0x4b9a, {0xaa, 0x1d, 0x14, 0xba, 0x40, 0x1c, 0xfa, 0x1f}};
    GUID target = {0x6f72c0a2, 0x6db7, 0x46e9, {0x9b, 0x62, 0xb5, 0x8a, 0x23, 0xf4, 0x92, 0x8b}};
    GUID multithread = {0x31e6e7bc, 0xe0ff, 0x4d46, {0x8c, 0x64, 0xa0, 0xa8, 0xc4, 0x1c, 0x15, 0xd3}};
    GUID factory2 = {0x94f81a73, 0x9212, 0x4376, {0x9c, 0x58, 0xb1, 0x6a, 0x3a, 0x0d, 0x39, 0x92}};
    GUID factory3 = {0x0869759f, 0x4f00, 0x413f, {0xb0, 0x3e, 0x2b, 0xda, 0x45, 0x40, 0x4d, 0x0f}};
    GUID factory7 = {0xbdc2bdd3, 0xb96c, 0x4de6, {0xbd, 0xf7, 0x99, 0xd4, 0x74, 0x54, 0x54, 0xde}};
    IUnknown *factory = NULL;
    HRESULT hr;
    int i;

    hr = create(1, &factory1, NULL, (void **)&factory);
    printf("D2D1CreateFactory(multi-threaded, ID2D1Factory1): hr=%08lx\n", hr);
    if (FAILED(hr)) return 1;
    qi(factory, "target {6f72c0a2-6db7-46e9-9b62-b58a23f4928b}", target);
    qi(factory, "ID2D1Multithread", multithread);
    qi(factory, "ID2D1Factory2", factory2);
    qi(factory, "ID2D1Factory3", factory3);
    qi(factory, "ID2D1Factory7", factory7);
    for (i = 0; i < 3; i++)
    {
        IUnknown *f2 = NULL;
        hr = create(i == 0 ? 0 : 1, &factory1, NULL, (void **)&f2);
        printf("again type %d: hr=%08lx\n", i == 0 ? 0 : 1, hr);
        if (f2) f2->lpVtbl->Release(f2);
    }
    return 0;
}
