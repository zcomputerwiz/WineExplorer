/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: where (module and offset) do the vtable slots of a desktop DirectComposition device and of its private
 * interface {28d6ad3d-...} point? Only reads the vtable data of an object the system hands out; nothing of the module is read.
 * The offsets let wintrace.c put an entry breakpoint on a private method to log its arguments.
 * Build: x86_64-w64-mingw32-gcc -O2 -o dcomp-vtable-probe.exe dcomp-vtable-probe.c
 */
#include <stdio.h>
#include <windows.h>

static const GUID IID_Desktop = {0x5f4633fe, 0x1e08, 0x4cb8, {0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02}};
static const GUID IID_PrivB = {0x28d6ad3d, 0xee2a, 0x4bcd, {0x94, 0x19, 0x7d, 0x54, 0x80, 0x04, 0x35, 0xb1}};

typedef HRESULT (WINAPI *create_t)(IUnknown *, REFIID, void **);

static void where(const char *what, void *p)
{
    HMODULE m = NULL;
    char name[MAX_PATH] = "?";

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, p, &m))
        GetModuleFileNameA(m, name, sizeof(name));
    printf("  %-10s %p  %s+%#lx\n", what, p, name, (unsigned long)((char *)p - (char *)m));
}

static void dump(const char *label, void *obj, int from, int to)
{
    void **vtbl = *(void ***)obj;
    int i;

    printf("%s: object %p vtable %p\n", label, obj, (void *)vtbl);
    for (i = from; i <= to; i++)
    {
        char l[16];
        sprintf(l, "slot %2d", i);
        where(l, vtbl[i]);
    }
}

int main(void)
{
    HMODULE dcomp = LoadLibraryA("dcomp.dll");
    create_t create2 = dcomp ? (create_t)GetProcAddress(dcomp, "DCompositionCreateDevice2") : NULL;
    void *dev = NULL, *priv = NULL;
    HRESULT hr;

    if (!create2) { printf("no DCompositionCreateDevice2\n"); return 1; }
    hr = create2(NULL, &IID_Desktop, &dev);
    printf("CreateDevice2(NULL, IDCompositionDesktopDevice) hr %08lx dev %p\n", hr, dev);
    if (FAILED(hr)) return 1;
    dump("desktop device", dev, 0, 49);
    hr = ((HRESULT (WINAPI *)(void *, const GUID *, void **))(*(void ***)dev)[0])(dev, &IID_PrivB, &priv);
    printf("QI {28d6ad3d} hr %08lx obj %p (same as device: %s)\n", hr, priv, priv == dev ? "yes" : "no");
    if (SUCCEEDED(hr) && priv && priv != dev) dump("private interface", priv, 0, 49);
    return 0;
}
