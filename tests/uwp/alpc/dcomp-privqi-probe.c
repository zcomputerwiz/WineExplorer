/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: which private interfaces does a DirectComposition device answer? dcompi.dll (Windows App Runtime) asks the
 * device made by DCompositionCreateDevice3 for {d14b6158-c3fa-4bce-9c1f-b61d8665eab0}, and the device of DCompositionCreateDevice2
 * for {28d6ad3d-ee2a-4bcd-9419-7d54800435b1}. This asks Windows whether it answers them, and for each answer prints the vtable
 * slots (module and offset) and the IInspectable/IUnknown behaviour. Also tries the IIDs on the other device kinds.
 * Build: x86_64-w64-mingw32-gcc -O2 -o dcomp-privqi-probe.exe dcomp-privqi-probe.c
 */
#include <stdio.h>
#include <windows.h>

static const GUID IID_Unknown_ = {0x00000000, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_Desktop = {0x5f4633fe, 0x1e08, 0x4cb8, {0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02}};
static const GUID IID_Device2 = {0x75f6468d, 0x1b8e, 0x447c, {0x9b, 0xc6, 0x75, 0xfe, 0xa8, 0x0b, 0x5b, 0x25}};
static const GUID IID_Device1 = {0xc37ea93a, 0xe7aa, 0x450d, {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};
static const GUID IID_PrivA = {0xd14b6158, 0xc3fa, 0x4bce, {0x9c, 0x1f, 0xb6, 0x1d, 0x86, 0x65, 0xea, 0xb0}};
static const GUID IID_PrivB = {0x28d6ad3d, 0xee2a, 0x4bcd, {0x94, 0x19, 0x7d, 0x54, 0x80, 0x04, 0x35, 0xb1}};

typedef HRESULT (WINAPI *create_t)(IUnknown *, REFIID, void **);

static void where(const char *what, void *p)
{
    HMODULE m = NULL;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, p, &m))
        GetModuleFileNameA(m, name, sizeof(name));
    printf("    %s %p  %s+0x%llx\n", what, p, strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name,
           (unsigned long long)((char *)p - (char *)m));
}

static void try_qi(const char *dev_name, IUnknown *dev, const char *iid_name, REFIID iid)
{
    void *out = NULL;
    HRESULT hr = dev->lpVtbl->QueryInterface(dev, iid, &out);
    printf("%s QI %s: hr %08lx %p\n", dev_name, iid_name, hr, out);
    if (!hr && out)
    {
        void **vtbl = *(void ***)out;
        int i;
        for (i = 0; i < 12; i++) { char label[16]; sprintf(label, "slot %2d", i); where(label, vtbl[i]); }
        ((IUnknown *)out)->lpVtbl->Release((IUnknown *)out);
    }
}

int main(void)
{
    HMODULE dcomp = LoadLibraryA("dcomp.dll");
    create_t create2 = (create_t)GetProcAddress(dcomp, "DCompositionCreateDevice2");
    create_t create3 = (create_t)GetProcAddress(dcomp, "DCompositionCreateDevice3");
    IUnknown *dev;
    HRESULT hr;

    hr = create3(NULL, &IID_Desktop, (void **)&dev);
    printf("CreateDevice3(desktop): hr %08lx\n", hr);
    if (!hr)
    {
        /* which known interface is the same pointer as each private one? */
        static const GUID IID_Device3 = {0x0987cb06, 0xf916, 0x48bf, {0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9}};
        struct { const char *n; const GUID *g; } known[] = {
            { "IUnknown", &IID_Unknown_ }, { "Device", &IID_Device1 }, { "Device2", &IID_Device2 },
            { "Desktop", &IID_Desktop }, { "Device3", &IID_Device3 }, { "PrivA", &IID_PrivA }, { "PrivB", &IID_PrivB } };
        int i;
        for (i = 0; i < 7; i++)
        {
            void *p = NULL;
            HRESULT h = dev->lpVtbl->QueryInterface(dev, known[i].g, &p);
            printf("  dev3 %-8s hr %08lx ptr %p vtbl %p\n", known[i].n, h, p, p ? *(void **)p : NULL);
            if (!h) ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
        }
        try_qi("dev3", dev, "PrivA d14b6158", &IID_PrivA);
        try_qi("dev3", dev, "PrivB 28d6ad3d", &IID_PrivB);
        dev->lpVtbl->Release(dev);
    }
    hr = create2(NULL, &IID_Desktop, (void **)&dev);
    printf("CreateDevice2(desktop): hr %08lx\n", hr);
    if (!hr)
    {
        try_qi("dev2", dev, "PrivA d14b6158", &IID_PrivA);
        try_qi("dev2", dev, "PrivB 28d6ad3d", &IID_PrivB);
        dev->lpVtbl->Release(dev);
    }
    return 0;
}
