/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe, Windows 11: ask the activation factory of a runtime class for an interface by IID and describe the answer: the HRESULT,
 * the interface pointer and its vtable (module and offset of the first slots), the IIDs the factory reports in GetIids, and whether the
 * object is the factory itself. For interfaces that are not in the metadata (undocumented ones).
 *   factory-iid-probe.exe CLASS IID [SLOTS]      e.g. Windows.ApplicationModel.UserActivities.UserActivityRequestManager dd69f876-9699-4715-9095-e37ea30dfa1b
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o factory-iid-probe.exe factory-iid-probe.c -lole32 -lruntimeobject
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <roapi.h>
#include <winstring.h>

static const GUID IID_IActivationFactory_ = {0x00000035, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
#define IID_IActivationFactory IID_IActivationFactory_

static void describe(const char *what, void *p, int count)
{
    void **vtbl;
    int i;

    if (!p) return;
    vtbl = *(void ***)p;
    printf("%s: %p, vtable %p\n", what, p, (void *)vtbl);
    for (i = 0; i < count; i++)
    {
        HMODULE mod = NULL;
        char name[MAX_PATH] = "?";

        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, vtbl[i], &mod))
            GetModuleFileNameA(mod, name, sizeof(name));
        printf("  slot %2d: +%#lx %s\n", i, mod ? (unsigned long)((char *)vtbl[i] - (char *)mod) : 0,
               strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name);
    }
}

int main(int argc, char **argv)
{
    WCHAR wclass[256], wiid[64];
    HSTRING cls;
    GUID iid;
    void *factory = NULL, *iface = NULL;
    HRESULT hr;
    IUnknown *unk;
    int slots = argc > 3 ? atoi(argv[3]) : 12;

    if (argc < 3) return 2;
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wclass, 256);
    swprintf(wiid, 64, L"{%hs}", argv[2]);
    CLSIDFromString(wiid, &iid);
    RoInitialize(RO_INIT_MULTITHREADED);
    WindowsCreateString(wclass, (UINT32)wcslen(wclass), &cls);

    hr = RoGetActivationFactory(cls, &IID_IActivationFactory, &factory);
    printf("factory (IActivationFactory): hr %08lx %p\n", (ULONG)hr, factory);
    if (FAILED(hr)) return 1;
    describe("factory", factory, 8);
    unk = factory;
    {
        ULONG count = 0;
        IID *iids = NULL;
        typedef HRESULT (WINAPI *getiids_t)(void *, ULONG *, IID **);
        ULONG i;

        hr = ((getiids_t)(*(void ***)factory)[3])(factory, &count, &iids);
        printf("GetIids: hr %08lx, %lu interfaces\n", (ULONG)hr, count);
        for (i = 0; SUCCEEDED(hr) && i < count; i++)
            printf("  {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n", iids[i].Data1, iids[i].Data2, iids[i].Data3, iids[i].Data4[0],
                   iids[i].Data4[1], iids[i].Data4[2], iids[i].Data4[3], iids[i].Data4[4], iids[i].Data4[5], iids[i].Data4[6], iids[i].Data4[7]);
    }
    hr = unk->lpVtbl->QueryInterface(unk, &iid, &iface);
    printf("QueryInterface(%s): hr %08lx %p%s\n", argv[2], (ULONG)hr, iface, iface == factory ? " (the factory itself)" : "");
    describe("interface", iface, slots);
    return 0;
}
