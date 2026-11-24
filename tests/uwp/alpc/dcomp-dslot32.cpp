// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Black-box probe (32-bit Windows): bytes of arguments popped by methods 29 and 46 of the desktop device interface and by slot 8 of the
// interface {6b556968-...} of the device, and what the commit interface does on a failed Commit.
#include <windows.h>
#include <stdio.h>
#include <dxgi.h>
#include <dcomp.h>

typedef HRESULT (WINAPI *create3_t)(IUnknown *, REFIID, void **);
static const GUID IID_commit = {0x6b556968, 0x2d25, 0x11e2, {0x99, 0xf6, 0x8b, 0xc3, 0x60, 0x88, 0x70, 0x9b}};

static int pops(void *fn, void *self, void *a1, void *a2, void *a3, int *hr_out)
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
            push a3
            push a2
            push a1
            push self
            call fn
            mov hr, eax
            mov after, esp
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1000; }
    *hr_out = hr;
    return after - (before - 40);
}

int main(void)
{
    create3_t create3 = (create3_t)GetProcAddress(LoadLibraryW(L"dcomp.dll"), "DCompositionCreateDevice3");
    IDCompositionDesktopDevice *desktop = NULL;
    void *commit = NULL;
    HRESULT hr = create3 ? create3(NULL, __uuidof(IDCompositionDesktopDevice), (void **)&desktop) : E_FAIL;
    int h = 0, p;
    UINT64 block = 0;
    printf("CreateDevice3: %08lx\n", hr);
    if (FAILED(hr)) return 1;

    p = pops((*(void ***)desktop)[46], desktop, &block, NULL, (void *)1, &h);
    printf("desktop slot 46 (a, NULL b, code 1): popped %d bytes, hr %08x, block %I64x\n", p, h, block);
    p = pops((*(void ***)desktop)[29], desktop, (void *)0, (void *)0, (void *)0, &h);
    printf("desktop slot 29 (handle 0, iid 0, out 0): popped %d bytes, hr %08x\n", p, h);

    hr = desktop->QueryInterface(IID_commit, &commit);
    printf("QI {6b556968}: %08lx %p\n", hr, commit);
    if (SUCCEEDED(hr))
    {
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        p = pops((*(void ***)commit)[8], commit, ev, NULL, NULL, &h);
        printf("commit slot 8 (event): popped %d bytes, hr %08x\n", p, h);
        printf("event signaled before Commit: %d\n", WaitForSingleObject(ev, 0) == WAIT_OBJECT_0);
        hr = desktop->Commit();
        Sleep(50);
        printf("Commit: %08lx, event signaled after: %d\n", hr, WaitForSingleObject(ev, 0) == WAIT_OBJECT_0);
        ResetEvent(ev);
        /* a failed Commit: a target for a window that is destroyed before the commit */
        {
            IDCompositionVisual2 *vis = NULL;
            IDCompositionTarget *target = NULL;
            HWND w = CreateWindowW(L"static", L"t", WS_POPUP, 0, 0, 10, 10, 0, 0, 0, 0);
            desktop->CreateVisual(&vis);
            hr = desktop->CreateTargetForHwnd(w, TRUE, &target);
            printf("CreateTargetForHwnd: %08lx\n", hr);
            if (SUCCEEDED(hr))
            {
                target->SetRoot(vis);
                DestroyWindow(w);
                hr = desktop->Commit();
                Sleep(50);
                printf("Commit with a destroyed target window: %08lx, event signaled: %d\n", hr, WaitForSingleObject(ev, 0) == WAIT_OBJECT_0);
            }
        }
        p = pops((*(void ***)commit)[8], commit, NULL, NULL, NULL, &h);
        printf("commit slot 8 (NULL event): hr %08x\n", h);
    }
    return 0;
}
