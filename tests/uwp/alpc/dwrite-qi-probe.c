/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: does the DirectWrite text analyzer of Windows answer the interface id that the Windows App SDK asks it for under
 * Wine ({b7e6163e-7f46-43b4-84b3-e4e6249c365e}, one digit away from IDWriteTextAnalyzer)? Prints the HRESULT of QueryInterface,
 * whether the pointer is the analyzer itself, and where its vtable and first slots are (module+offset). Only results are printed.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o dwrite-qi-probe.exe dwrite-qi-probe.c -ldwrite -lole32 -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <dwrite.h>
#include <stdio.h>

static const GUID iid_private = {0xb7e6163e, 0x7f46, 0x43b4, {0x84, 0xb3, 0xe4, 0xe6, 0x24, 0x9c, 0x36, 0x5e}};
static const GUID iid_analyzer = {0xb7e6163e, 0x7f46, 0x43b4, {0x84, 0xb3, 0xe4, 0xe6, 0x24, 0x9c, 0x36, 0x5d}};
static const GUID iid_analyzer1 = {0x80dad800, 0xe21f, 0x4e83, {0x96, 0xce, 0xbf, 0xcc, 0xe5, 0x00, 0xdb, 0x7c}};
static const GUID iid_analyzer2 = {0x553a9ff3, 0x5693, 0x4df7, {0xb5, 0x2b, 0x74, 0x80, 0x6f, 0x7f, 0x2e, 0xb9}};

static void where(void *p, char *buf, size_t n)
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
        where(vt, a, sizeof(a)); where(vt[3], b, sizeof(b));
        printf(" %s the analyzer; vtable %s, slot3 %s", (void *)unk == (void *)obj ? "is" : "is not", a, b);
        IUnknown_Release(unk);
    }
    printf("\n");
}

int main(void)
{
    IDWriteFactory *factory;
    IDWriteTextAnalyzer *analyzer;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&factory);
    printf("DWriteCreateFactory: hr %08lx\n", hr);
    if (FAILED(hr)) return 1;
    hr = IDWriteFactory_CreateTextAnalyzer(factory, &analyzer);
    printf("CreateTextAnalyzer: hr %08lx\n", hr);
    if (FAILED(hr)) return 1;
    query((IUnknown *)analyzer, "IDWriteTextAnalyzer", &iid_analyzer);
    query((IUnknown *)analyzer, "IDWriteTextAnalyzer1", &iid_analyzer1);
    query((IUnknown *)analyzer, "IDWriteTextAnalyzer2", &iid_analyzer2);
    query((IUnknown *)analyzer, "{b7e6163e-...-365e}", &iid_private);
    query((IUnknown *)factory, "{b7e6163e-...-365e} on the factory", &iid_private);
    IDWriteTextAnalyzer_Release(analyzer);
    IDWriteFactory_Release(factory);
    return 0;
}
