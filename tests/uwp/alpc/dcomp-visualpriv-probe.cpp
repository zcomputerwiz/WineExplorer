// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Black-box probe (Windows host): the private interface {2c4eef28-...} of a dcomp visual. QueryInterface it, compare the pointers
// (the visual, the private interface, QueryInterface for IUnknown on each), check reference counting, and print where the first
// vtable slots point (vtable data only: module and offset).
#include <windows.h>
#include <stdio.h>
#include <initguid.h>
#include <dxgi.h>
#include <dcomp.h>

static const GUID IID_vpriv = {0x2c4eef28, 0x1bc0, 0x4736, {0xb7, 0xdd, 0xb6, 0x26, 0x92, 0xf9, 0xbd, 0x67}};

static void where(void *p, char *out, size_t n)
{
    HMODULE m = NULL;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &m) && m)
    {
        const char *b;
        GetModuleFileNameA(m, name, sizeof(name));
        b = strrchr(name, '\\');
        snprintf(out, n, "%s+%lx", b ? b + 1 : name, (unsigned long)((char *)p - (char *)m));
    }
    else snprintf(out, n, "%p", p);
}

typedef HRESULT (WINAPI *create3_t)(IUnknown *, REFIID, void **);

int main(void)
{
    create3_t create3 = (create3_t)GetProcAddress(LoadLibraryW(L"dcomp.dll"), "DCompositionCreateDevice3");
    IDCompositionDesktopDevice *desktop = NULL;
    IDCompositionVisual2 *visual = NULL;
    IUnknown *priv = NULL, *priv2 = NULL, *unk = NULL, *vunk = NULL, *vunk2 = NULL;
    HRESULT hr;

    hr = create3(NULL, __uuidof(IDCompositionDesktopDevice), (void **)&desktop);
    printf("CreateDevice3: %08lx\n", hr);
    if (FAILED(hr)) return 1;
    hr = desktop->CreateVisual(&visual);
    printf("CreateVisual: %08lx %p\n", hr, visual);
    visual->QueryInterface(IID_IUnknown, (void **)&vunk);
    hr = visual->QueryInterface(IID_vpriv, (void **)&priv);
    printf("QI private: %08lx %p (visual %p, visual IUnknown %p)\n", hr, priv, visual, vunk);
    if (FAILED(hr)) return 1;
    hr = priv->QueryInterface(IID_vpriv, (void **)&priv2);
    printf("QI private again: %08lx %p\n", hr, priv2);
    hr = priv->QueryInterface(IID_IUnknown, (void **)&unk);
    printf("private QI IUnknown: %08lx %p (same as visual IUnknown: %d)\n", hr, unk, unk == vunk);
    hr = priv->QueryInterface(__uuidof(IDCompositionVisual2), (void **)&vunk2);
    printf("private QI IDCompositionVisual2: %08lx %p (same as visual: %d)\n", hr, vunk2, (void *)vunk2 == (void *)visual);
    printf("refs: private AddRef %lu, Release %lu; visual AddRef %lu Release %lu\n", priv->AddRef(), priv->Release(), visual->AddRef(), visual->Release());
    for (int i = 0; i < 48; i++)
    {
        char w[300];
        where((*(void ***)priv)[i], w, sizeof(w));
        printf("   slot %2d %s\n", i, w);
    }
    {
        /* a version-1 device: does its visual answer the private IID? */
        typedef HRESULT (WINAPI *create1_t)(IDXGIDevice *, REFIID, void **);
        IDCompositionDevice *dev1 = NULL;
        IDCompositionVisual *v1 = NULL;
        void *p1 = NULL;
        create1_t create1 = (create1_t)GetProcAddress(GetModuleHandleW(L"dcomp.dll"), "DCompositionCreateDevice");
        hr = create1 ? create1(NULL, __uuidof(IDCompositionDevice), (void **)&dev1) : E_FAIL;
        printf("CreateDevice(v1, NULL dxgi): %08lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = dev1->CreateVisual(&v1);
            printf("v1 CreateVisual: %08lx\n", hr);
            if (SUCCEEDED(hr)) { hr = v1->QueryInterface(IID_vpriv, &p1); printf("v1 visual QI private: %08lx %p\n", hr, p1); }
        }
    }
    {
        /* slot 40 of the visual's vtable, called the way the Windows App Runtime calls it: (this, REFIID, void **) */
        typedef HRESULT (STDMETHODCALLTYPE *m40_t)(void *self, const GUID *iid, void **out);
        m40_t m40 = (*(m40_t **)visual)[40];
        const GUID *iids[3] = {&IID_vpriv, &IID_IUnknown, &IID_IDispatch};
        for (int k = 0; k < 3; k++)
        {
            void *out = (void *)(ULONG_PTR)0xdeadbeef00ull;
            ULONG before = visual->AddRef() - 1, mid, after;
            HRESULT h = E_FAIL;
            __try { h = m40(visual, iids[k], &out); } __except (EXCEPTION_EXECUTE_HANDLER) { h = 0xc0000005; }
            mid = visual->AddRef() - 1;
            visual->Release();
            after = visual->Release();
            printf("slot 40 iid #%d: hr %08lx out %p (visual %p) refs before %lu after-call %lu\n", k, h, out, visual, before, mid - 1);
            (void)after;
        }
        {
            HRESULT h = E_FAIL;
            __try { h = m40(visual, &IID_vpriv, NULL); } __except (EXCEPTION_EXECUTE_HANDLER) { h = 0xc0000005; }
            printf("slot 40 NULL out: hr %08lx\n", h);
        }
    }
    return 0;
}
