/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Activates WinRT classes by name and prints what the activation factory says about itself: result, runtime class name,
 * the IIDs from IInspectable::GetIids, and whether it answers a given IID. Usage: winrt-iids-probe CLASS [IID] [CLASS [IID]]...
 * Only public interfaces of IInspectable are used (black-box).
 */
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <stdio.h>
#include <string.h>

static const GUID iid_inspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};

static void print_guid(const GUID *g)
{
    printf("{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1],
            g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static BOOL parse_guid(const char *s, GUID *g)
{
    unsigned d1, d2, d3, b[8]; int i;
    if (*s == '{') s++;
    if (sscanf(s, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, b, b + 1, b + 2, b + 3, b + 4, b + 5, b + 6, b + 7) != 11) return FALSE;
    g->Data1 = d1; g->Data2 = d2; g->Data3 = d3;
    for (i = 0; i < 8; i++) g->Data4[i] = b[i];
    return TRUE;
}

int main(int argc, char **argv)
{
    int i = 1;
    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_MULTITHREADED);
    while (i < argc)
    {
        const char *cls = argv[i++];
        GUID iid = {0}; BOOL have_iid = FALSE;
        WCHAR wcls[256];
        HSTRING hs;
        IInspectable *f = NULL;
        HRESULT hr;

        if (i < argc && argv[i][0] == '{') { have_iid = parse_guid(argv[i], &iid); i++; }
        MultiByteToWideChar(CP_ACP, 0, cls, -1, wcls, 256);
        WindowsCreateString(wcls, wcslen(wcls), &hs);
        hr = RoGetActivationFactory(hs, &iid_inspectable, (void **)&f);
        printf("%s: RoGetActivationFactory(IInspectable) = %#lx\n", cls, hr);
        if (SUCCEEDED(hr))
        {
            ULONG n = 0, k; IID *iids = NULL; HSTRING name = NULL; TrustLevel tl = 0;
            if (SUCCEEDED(f->lpVtbl->GetRuntimeClassName(f, &name)))
            {
                printf("  runtime class name %ls\n", WindowsGetStringRawBuffer(name, NULL));
                WindowsDeleteString(name);
            }
            f->lpVtbl->GetTrustLevel(f, &tl);
            printf("  trust level %d\n", (int)tl);
            if (SUCCEEDED(f->lpVtbl->GetIids(f, &n, &iids)))
            {
                for (k = 0; k < n; k++) { printf("  iid "); print_guid(&iids[k]); printf("\n"); }
                CoTaskMemFree(iids);
            }
            if (have_iid)
            {
                void *p = NULL;
                hr = f->lpVtbl->QueryInterface(f, &iid, &p);
                printf("  QueryInterface "); print_guid(&iid); printf(" = %#lx\n", hr);
                if (SUCCEEDED(hr)) ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
            }
            f->lpVtbl->Release(f);
        }
        WindowsDeleteString(hs);
    }
    return 0;
}
