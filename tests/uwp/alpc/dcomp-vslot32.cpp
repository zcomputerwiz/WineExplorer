// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Black-box probe (32-bit Windows): how many bytes of arguments do slots 36..47 of a dcomp visual's vtable pop (stdcall callee cleanup)?
// Each slot is called with 8 dummy dwords pushed after `this`; esp before and after the call gives the bytes the callee removed.
#include <windows.h>
#include <stdio.h>
#include <dxgi.h>
#include <dcomp.h>

typedef HRESULT (WINAPI *create1_t)(IDXGIDevice *, REFIID, void **);

static int pops(void *fn, void *self, int *hr_out)
{
    int before = 0, after = 0, hr = 0;
    __try
    {
        __asm
        {
            mov before, esp
            push 0
            push 0
            push 0
            push 0
            push 0
            push 0
            push 0
            push 0
            push self
            call fn
            mov hr, eax
            mov after, esp
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1000; }
    *hr_out = hr;
    /* the pushes are 9 dwords = 36 bytes; the callee removed (36 - (before - after)) bytes... after the call esp is before - 36 + popped */
    return (after - (before - 36));
}

int main(void)
{
    create1_t create1 = (create1_t)GetProcAddress(LoadLibraryW(L"dcomp.dll"), "DCompositionCreateDevice");
    IDCompositionDevice *dev = NULL;
    IDCompositionVisual *visual = NULL;
    HRESULT hr = create1 ? create1(NULL, __uuidof(IDCompositionDevice), (void **)&dev) : E_FAIL;
    printf("CreateDevice: %08lx\n", hr);
    if (FAILED(hr)) return 1;
    hr = dev->CreateVisual(&visual);
    printf("CreateVisual: %08lx\n", hr);
    if (FAILED(hr)) return 1;
    for (int slot = 36; slot <= 47; slot++)
    {
        int h = 0;
        void *fn = (*(void ***)visual)[slot];
        HMODULE m = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)fn, &m);
        int p = (slot == 40 || slot == 41) ? pops(fn, visual, &h) : -2000;
        printf("slot %d fn offset %lx: callee popped %d bytes (hr %08x)%s\n", slot, (unsigned long)((char *)fn - (char *)m), p, h,
               p == -2000 ? " [not called]" : "");
    }
    return 0;
}
