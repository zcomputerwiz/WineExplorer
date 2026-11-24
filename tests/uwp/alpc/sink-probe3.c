/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/* Black-box probe (Windows host), third part: how many bytes of the block NtCreateCompositionInputSink(const void *block, HANDLE *out) reads.
 * The block is put at the end of a committed page followed by an inaccessible page, so that a call that reads past the end of the buffer it is
 * given fails with an access violation status. The call is made with the buffer ending after N bytes for N from 0 to 0x100 (the bytes that are
 * there are the 0xe8-byte block of a size and four entries of 0x38 bytes, a kind and a window each; the rest of the page is zero). The first N
 * for which the call stops failing with STATUS_ACCESS_VIOLATION tells how many bytes it reads; the entries are also probed one at a time. */
#include <windows.h>
#include <stdio.h>

typedef LONG (NTAPI *create_t)(const void *block, HANDLE *out);
typedef LONG (NTAPI *close_t)(HANDLE sink);

int main(void)
{
    BYTE full[0x100] = {0};
    BYTE *page, *block;
    DWORD old;
    HMODULE w32u;
    create_t create;
    close_t close_sink;
    HWND hwnd;
    int e, n, first_ok = -1;
    SYSTEM_INFO si;

    GetDesktopWindow();
    GetSystemInfo(&si);
    w32u = LoadLibraryW(L"win32u.dll");
    create = (create_t)GetProcAddress(w32u, "NtCreateCompositionInputSink");
    close_sink = (close_t)GetProcAddress(w32u, "NtCloseCompositionInputSink");
    hwnd = CreateWindowW(L"static", L"sink", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0);

    *(ULONG_PTR *)full = 0xe8;
    for (e = 0; e < 4; e++) { *(ULONG_PTR *)(full + 8 + e * 0x38) = 2; *(HWND *)(full + 16 + e * 0x38) = hwnd; }

    page = VirtualAlloc(NULL, 2 * si.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    VirtualProtect(page + si.dwPageSize, si.dwPageSize, PAGE_NOACCESS, &old);

    for (n = 0; n <= 0x100; n++)
    {
        HANDLE h = (HANDLE)(ULONG_PTR)0xdead;
        LONG st;
        block = page + si.dwPageSize - n;
        memcpy(block, full, n);
        st = create(block, &h);
        if (!st) { close_sink(h); if (first_ok < 0) first_ok = n; }
        if (n == 0 || n == 0x08 || n == 0x10 || n == 0x40 || n == 0x48 || n == 0x80 || n == 0xb0 || n == 0xe0 || n == 0xe7 || n == 0xe8 || n == 0xe9 || n == 0x100 ||
            (first_ok == n))
            printf("buffer of %#x bytes: status %08lx\n", n, (ULONG)st);
    }
    printf("first buffer size that works: %#x\n", first_ok);
    /* where the four windows have to be: clear one at a time (stride 0x38 from offset 16), or move them to a stride of 0x30 */
    for (e = 0; e < 4; e++)
    {
        BYTE b[0xe8];
        HANDLE h = (HANDLE)(ULONG_PTR)0xdead;
        LONG st;
        memcpy(b, full, sizeof(b));
        *(HWND *)(b + 16 + e * 0x38) = NULL;
        st = create(b, &h);
        printf("window of entry %d cleared: status %08lx\n", e, (ULONG)st);
        if (!st) close_sink(h);
    }
    {
        BYTE b[0xe8] = {0};
        HANDLE h = (HANDLE)(ULONG_PTR)0xdead;
        LONG st;
        *(ULONG_PTR *)b = 0xe8;
        for (e = 0; e < 4; e++) { *(ULONG_PTR *)(b + 8 + e * 0x30) = 2; *(HWND *)(b + 16 + e * 0x30) = hwnd; }
        st = create(b, &h);
        printf("windows at a stride of 0x30: status %08lx\n", (ULONG)st);
        if (!st) close_sink(h);
    }
    return 0;
}
