/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: which handles the private method 29 of IDCompositionDesktopDevice (it opens a handle of
 * DCompositionCreateSharedVisualHandle as a visual or a target) accepts. Prints the result for a shared visual handle, a duplicate
 * of it, a handle of another kind (an event, a composition surface handle), a closed handle, and a handle opened as a target
 * by a second device while the first device holds the visual; then the releases of the two devices in both orders.
 * Build: x86_64-w64-mingw32-gcc -O2 -o dcomp-sharedopen-probe.exe dcomp-sharedopen-probe.c -luuid -lole32
 */
#include <stdio.h>
#include <windows.h>
#include <initguid.h>

DEFINE_GUID(IID_IDCompositionDesktopDevice_, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);
DEFINE_GUID(IID_IDCompositionVisual_, 0x4d93059d, 0x097b, 0x4651, 0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3);
DEFINE_GUID(IID_IDCompositionTarget_, 0xeacdd04c, 0x117e, 0x4e17, 0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89);

typedef HRESULT (WINAPI *create_handle_t)(HANDLE *out);
typedef HRESULT (WINAPI *surface_handle_t)(DWORD access, SECURITY_ATTRIBUTES *sa, HANDLE *out);
typedef HRESULT (WINAPI *create_device3_t)(IUnknown *, REFIID, void **);
typedef HRESULT (STDMETHODCALLTYPE *open_t)(void *iface, HANDLE handle, REFIID iid, void **out);
typedef ULONG (STDMETHODCALLTYPE *release_t)(void *iface);
typedef HRESULT (STDMETHODCALLTYPE *set_root_t)(void *iface, void *visual);
typedef HRESULT (STDMETHODCALLTYPE *commit_t)(void *iface);

static open_t open_of(void *device) { return (*(open_t **)device)[29]; }
static ULONG release(void *obj) { return obj ? (*(release_t **)obj)[2](obj) : 0; }

static void try_open(const char *what, void *device, HANDLE h, REFIID iid)
{
    void *out = (void *)0xdeadbeef;
    HRESULT hr = open_of(device)(device, h, iid, &out);
    printf("%-40s hr %08lx out %p\n", what, hr, out);
    if (SUCCEEDED(hr) && out && out != (void *)0xdeadbeef) release(out);
}

static void release_orders(create_device3_t create3, create_handle_t create_handle, int viewer_first)
{
    void *viewer = NULL, *portal_dev = NULL, *visual = NULL, *target = NULL;
    HANDLE h = NULL, dup = NULL;
    HRESULT hr;

    create_handle(&h);
    DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);
    create3(NULL, &IID_IDCompositionDesktopDevice_, &viewer);
    create3(NULL, &IID_IDCompositionDesktopDevice_, &portal_dev);
    hr = open_of(viewer)(viewer, h, &IID_IDCompositionVisual_, &visual);
    printf("order %s: viewer visual hr %08lx\n", viewer_first ? "viewer first" : "portal first", hr);
    hr = open_of(portal_dev)(portal_dev, dup, &IID_IDCompositionTarget_, &target);
    printf("  portal target (duplicate) hr %08lx\n", hr);
    if (target)
    {
        void *root = NULL;
        /* CreateVisual is slot 6 of IDCompositionDesktopDevice (IDCompositionDevice2: Commit, WaitForCommitCompletion,
         * GetFrameStatistics, CreateVisual) */
        typedef HRESULT (STDMETHODCALLTYPE *create_visual_t)(void *, void **);
        hr = (*(create_visual_t **)portal_dev)[6](portal_dev, &root);
        printf("  portal CreateVisual hr %08lx\n", hr);
        if (root)
        {
            hr = (*(set_root_t **)target)[3](target, root);
            printf("  portal SetRoot hr %08lx\n", hr);
        }
        hr = (*(commit_t **)portal_dev)[3](portal_dev);
        printf("  portal Commit hr %08lx\n", hr);
        hr = (*(commit_t **)viewer)[3](viewer);
        printf("  viewer Commit hr %08lx\n", hr);
        release(root);
    }
    release(target);
    release(visual);
    if (viewer_first)
    {
        printf("  viewer release %lu\n", release(viewer));
        printf("  portal device release %lu\n", release(portal_dev));
    }
    else
    {
        printf("  portal device release %lu\n", release(portal_dev));
        printf("  viewer release %lu\n", release(viewer));
    }
    CloseHandle(h);
    CloseHandle(dup);
}

int main(void)
{
    HMODULE dcomp = LoadLibraryA("dcomp.dll");
    create_handle_t create_handle = (create_handle_t)GetProcAddress(dcomp, "DCompositionCreateSharedVisualHandle");
    surface_handle_t surface_handle = (surface_handle_t)GetProcAddress(dcomp, "DCompositionCreateSurfaceHandle");
    create_device3_t create3 = (create_device3_t)GetProcAddress(dcomp, "DCompositionCreateDevice3");
    HANDLE shared = NULL, dup = NULL, closed = NULL, event, surface = NULL;
    void *device = NULL;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!create_handle) create_handle = (create_handle_t)GetProcAddress(dcomp, (LPCSTR)1040);
    printf("create_handle %p surface_handle %p create3 %p\n", create_handle, surface_handle, create3);
    if (!create_handle || !create3) return 1;

    hr = create3(NULL, &IID_IDCompositionDesktopDevice_, &device);
    printf("device hr %08lx\n", hr);
    if (FAILED(hr)) return 1;

    create_handle(&shared);
    DuplicateHandle(GetCurrentProcess(), shared, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);
    create_handle(&closed);
    CloseHandle(closed);
    event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (surface_handle)
    {
        hr = surface_handle(0x3, NULL, &surface);
        printf("surface handle hr %08lx\n", hr);
    }

    try_open("shared as visual", device, shared, &IID_IDCompositionVisual_);
    try_open("duplicate as target", device, dup, &IID_IDCompositionTarget_);
    try_open("event as visual", device, event, &IID_IDCompositionVisual_);
    try_open("event as target", device, event, &IID_IDCompositionTarget_);
    if (surface)
    {
        try_open("surface handle as visual", device, surface, &IID_IDCompositionVisual_);
        try_open("surface handle as target", device, surface, &IID_IDCompositionTarget_);
    }
    try_open("closed as visual", device, closed, &IID_IDCompositionVisual_);
    try_open("closed as target", device, closed, &IID_IDCompositionTarget_);
    try_open("process as target", device, GetCurrentProcess(), &IID_IDCompositionTarget_);

    printf("device release %lu\n", release(device));
    CloseHandle(shared);
    CloseHandle(dup);
    CloseHandle(event);
    if (surface) CloseHandle(surface);

    release_orders(create3, create_handle, 1);
    release_orders(create3, create_handle, 0);
    printf("done\n");
    return 0;
}
