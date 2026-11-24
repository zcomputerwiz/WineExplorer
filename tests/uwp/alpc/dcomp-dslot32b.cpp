// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Black-box probe (32-bit Windows): which arguments the interface {6b556968-...} of a dcomp device accepts in slot 8 (it removes 12 bytes: this and
// two arguments), and whether the event is signaled by a Commit then, also for a Commit that fails.
#include <windows.h>
#include <stdio.h>
#include <dxgi.h>
#include <dcomp.h>

typedef HRESULT (WINAPI *create3_t)(IUnknown *, REFIID, void **);
static const GUID IID_commit = {0x6b556968, 0x2d25, 0x11e2, {0x99, 0xf6, 0x8b, 0xc3, 0x60, 0x88, 0x70, 0x9b}};

typedef HRESULT (__stdcall *m8_t)(void *self, HANDLE event, ULONG_PTR arg);

int main(void)
{
    create3_t create3 = (create3_t)GetProcAddress(LoadLibraryW(L"dcomp.dll"), "DCompositionCreateDevice3");
    IDCompositionDesktopDevice *desktop = NULL;
    void *commit = NULL;
    HRESULT hr = create3(NULL, __uuidof(IDCompositionDesktopDevice), (void **)&desktop);
    ULONG_PTR variants[5] = {0, 1, 2, 0x10, (ULONG_PTR)-1};
    DWORD dummy[4] = {0};
    printf("CreateDevice3: %08lx\n", hr);
    hr = desktop->QueryInterface(IID_commit, &commit);
    printf("QI {6b556968}: %08lx\n", hr);
    if (FAILED(hr)) return 1;
    m8_t m8 = (m8_t)(*(void ***)commit)[8];
    for (int i = 0; i < 6; i++)
    {
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        ULONG_PTR a = i < 5 ? variants[i] : (ULONG_PTR)dummy;
        HRESULT h = m8(commit, ev, a);
        BOOL committed;
        ResetEvent(ev);
        hr = desktop->Commit();
        Sleep(30);
        committed = WaitForSingleObject(ev, 0) == WAIT_OBJECT_0;
        printf("slot 8 (event, %#lx): hr %08lx; Commit %08lx, event signaled %d\n", (unsigned long)a, h, hr, committed);
        CloseHandle(ev);
    }
    /* other handle kinds and a bad handle */
    {
        HANDLE sem = CreateSemaphoreW(NULL, 0, 1, NULL);
        printf("slot 8 (semaphore, 0): hr %08lx\n", m8(commit, sem, 0));
        printf("slot 8 (bad handle 0x12345678, 0): hr %08lx\n", m8(commit, (HANDLE)0x12345678, 0));
        printf("slot 8 (NULL, 0): hr %08lx\n", m8(commit, NULL, 0));
    }
    return 0;
}
