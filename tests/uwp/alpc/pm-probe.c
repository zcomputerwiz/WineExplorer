/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe of Windows.Management.Deployment.PackageManager as Office Click-to-Run uses it: the factory and agility, an empty
 * FindPackagesByPackageFamilyName, StagePackageOptions (activation, property round trip, the same list object twice),
 * QueryInterface of the manager for IPackageManager6 and, on Wine only (argument "stage"), the staging call of that interface.
 * Raw vtable slots, so it builds with a bare mingw: x86_64-w64-mingw32-gcc -O2 -o pm-probe.exe pm-probe.c -lole32 -loleaut32
 * (add -lcombase with Wine; on Windows combase is reached through the import library of mingw: -lruntimeobject).
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void *HSTRING_;
typedef HRESULT (WINAPI *RoGetActivationFactory_t)(HSTRING_, REFIID, void **);
typedef HRESULT (WINAPI *WindowsCreateString_t)(const WCHAR *, UINT32, HSTRING_ *);
typedef HRESULT (WINAPI *RoInitialize_t)(int);

#define SLOT(obj, n) ((*(void ***)(obj))[n])
#define CALL(obj, n, type, ...) ((type)SLOT(obj, n))(obj, __VA_ARGS__)

static const GUID IID_IActivationFactory_ = {0x00000035, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_IPackageManager = {0x9a7d4b65, 0x5e8f, 0x4fc7, {0xa2, 0xe5, 0x7f, 0x69, 0x25, 0xcb, 0x8b, 0x53}};
static const GUID IID_IPackageManager6 = {0x0847e909, 0x53cd, 0x4e4f, {0x83, 0x2e, 0x57, 0xd1, 0x80, 0xf6, 0xe4, 0x47}};
static const GUID IID_IStagePackageOptions = {0x0b110c9c, 0xb95d, 0x4c56, {0xbd, 0x36, 0x6d, 0x65, 0x68, 0x00, 0xd0, 0x6b}};
static const GUID IID_IAgileObject_ = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};

static RoGetActivationFactory_t pGetFactory;
static WindowsCreateString_t pCreate;

static HSTRING_ hs(const WCHAR *s)
{
    HSTRING_ h = NULL;
    pCreate(s, (UINT32)wcslen(s), &h);
    return h;
}

static void release(void *obj)
{
    if (obj) ((ULONG (WINAPI *)(void *))SLOT(obj, 2))(obj);
}

int main(int argc, char **argv)
{
    HMODULE combase = LoadLibraryA("combase.dll");
    void *factory = NULL, *pm = NULL, *pm1 = NULL, *pm6 = NULL, *list = NULL, *iter = NULL, *opts = NULL, *ofactory = NULL;
    void *l1 = NULL, *l2 = NULL, *agile = NULL;
    int stage = argc > 1 && !strcmp(argv[1], "stage");
    BOOLEAN has = 7;
    HRESULT hr;
    UINT32 size = 7;

    pGetFactory = (RoGetActivationFactory_t)GetProcAddress(combase, "RoGetActivationFactory");
    pCreate = (WindowsCreateString_t)GetProcAddress(combase, "WindowsCreateString");
    ((RoInitialize_t)GetProcAddress(combase, "RoInitialize"))(1 /* RO_INIT_MULTITHREADED */);

    hr = pGetFactory(hs(L"Windows.Management.Deployment.PackageManager"), &IID_IActivationFactory_, &factory);
    printf("PackageManager factory: hr=%08lx\n", hr);
    if (hr) return 1;
    hr = ((HRESULT (WINAPI *)(void *, const GUID *, void **))SLOT(factory, 0))(factory, &IID_IAgileObject_, &agile);
    printf("factory IAgileObject: hr=%08lx\n", hr);
    release(agile);
    hr = CALL(factory, 6, HRESULT (WINAPI *)(void *, void **), &pm);
    printf("ActivateInstance: hr=%08lx\n", hr);
    if (hr) return 1;
    hr = ((HRESULT (WINAPI *)(void *, const GUID *, void **))SLOT(pm, 0))(pm, &IID_IPackageManager, &pm1);
    printf("QI IPackageManager: hr=%08lx\n", hr);

    /* FindPackagesByPackageFamilyName(L"...") -> IIterable<Package>: slot 19 */
    hr = CALL(pm1, 19, HRESULT (WINAPI *)(void *, HSTRING_, void **), hs(L"Wine.NoSuchFamily_8wekyb3d8bbwe"), &list);
    printf("FindPackagesByPackageFamilyName: hr=%08lx list=%s\n", hr, list ? "set" : "null");
    if (!hr && list)
    {
        hr = CALL(list, 6, HRESULT (WINAPI *)(void *, void **), &iter);
        printf("  First: hr=%08lx\n", hr);
        if (!hr)
        {
            hr = CALL(iter, 7, HRESULT (WINAPI *)(void *, BOOLEAN *), &has);
            printf("  HasCurrent: hr=%08lx has=%d\n", hr, has);
        }
        release(iter);
        release(list);
    }

    hr = ((HRESULT (WINAPI *)(void *, const GUID *, void **))SLOT(pm, 0))(pm, &IID_IPackageManager6, &pm6);
    printf("QI IPackageManager6: hr=%08lx\n", hr);
    if (!hr && stage)
    {
        void *op = (void *)1;
        /* slot 10: StagePackage with 7 arguments (uri, dependencies, options, volume, optional names, uris to install, related) */
        hr = CALL(pm6, 10, HRESULT (WINAPI *)(void *, void *, void *, int, void *, void *, void *, void *, void **),
                  NULL, NULL, 0, NULL, NULL, NULL, NULL, &op);
        printf("IPackageManager6 stage: hr=%08lx op=%s\n", hr, op ? "set" : "null");
    }
    release(pm6);
    release(pm1);
    release(pm);
    release(factory);

    hr = pGetFactory(hs(L"Windows.Management.Deployment.StagePackageOptions"), &IID_IActivationFactory_, &ofactory);
    printf("StagePackageOptions factory: hr=%08lx\n", hr);
    if (hr) return 1;
    hr = CALL(ofactory, 6, HRESULT (WINAPI *)(void *, void **), &opts);
    printf("StagePackageOptions ActivateInstance: hr=%08lx\n", hr);
    if (!hr)
    {
        void *o = NULL;
        BOOLEAN b = 7;
        hr = ((HRESULT (WINAPI *)(void *, const GUID *, void **))SLOT(opts, 0))(opts, &IID_IStagePackageOptions, &o);
        printf("QI IStagePackageOptions: hr=%08lx\n", hr);
        if (!hr)
        {
            hr = CALL(o, 16, HRESULT (WINAPI *)(void *, BOOLEAN *), &b);
            printf("  DeveloperMode default: hr=%08lx value=%d\n", hr, b);
            hr = CALL(o, 17, HRESULT (WINAPI *)(void *, BOOLEAN), 1);
            hr = CALL(o, 16, HRESULT (WINAPI *)(void *, BOOLEAN *), &b);
            printf("  DeveloperMode after put TRUE: hr=%08lx value=%d\n", hr, b);
            hr = CALL(o, 6, HRESULT (WINAPI *)(void *, void **), &l1);
            hr = CALL(o, 6, HRESULT (WINAPI *)(void *, void **), &l2);
            printf("  DependencyPackageUris twice: hr=%08lx same=%d\n", hr, l1 == l2);
            if (l1)
            {
                hr = CALL(l1, 7, HRESULT (WINAPI *)(void *, UINT32 *), &size);
                printf("  list Size: hr=%08lx size=%u\n", hr, size);
            }
            release(l1);
            release(l2);
            release(o);
        }
        release(opts);
    }
    release(ofactory);
    return 0;
}
