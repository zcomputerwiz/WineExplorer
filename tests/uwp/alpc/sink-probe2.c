/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/* Black-box probe (Windows host), second part: which blocks NtCreateCompositionInputSink(const void *block, HANDLE *out) accepts. The block is
 * 0xe8 bytes, a size and four entries of 0x38 bytes {kind (2 in the observed calls), hwnd, zeros}. */
#include <windows.h>
#include <stdio.h>

typedef LONG (NTAPI *create_t)(const void *block, HANDLE *out);
typedef LONG (NTAPI *query_luid_t)(HANDLE sink, LUID *luid);
typedef LONG (NTAPI *close_t)(HANDLE sink);

static create_t create;
static close_t close_sink;

static void attempt(const char *what, BYTE *block)
{
    HANDLE h = (HANDLE)(ULONG_PTR)0xdead;
    LONG st = create(block, &h);
    printf("%-58s %08lx handle %p\n", what, (ULONG)st, h);
    if (!st) close_sink(h);
}

static void fill(BYTE *block, ULONG_PTR kind, HWND a, HWND b, HWND c, HWND d)
{
    HWND w[4] = {a, b, c, d};
    int e;
    memset(block, 0, 0xe8);
    *(ULONG_PTR *)block = 0xe8;
    for (e = 0; e < 4; e++) { *(ULONG_PTR *)(block + 8 + e * 0x38) = kind; *(HWND *)(block + 16 + e * 0x38) = w[e]; }
}

int main(void)
{
    BYTE block[0xe8];
    HMODULE w32u;
    HWND w1, w2, w3, w4, child, other;

    GetDesktopWindow();
    w32u = LoadLibraryW(L"win32u.dll");
    create = (create_t)GetProcAddress(w32u, "NtCreateCompositionInputSink");
    close_sink = (close_t)GetProcAddress(w32u, "NtCloseCompositionInputSink");
    w1 = CreateWindowW(L"static", L"a", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0);
    w2 = CreateWindowW(L"static", L"b", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0);
    w3 = CreateWindowW(L"static", L"c", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0);
    w4 = CreateWindowW(L"static", L"d", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0);
    child = CreateWindowW(L"static", L"child", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, w1, 0, 0, 0);
    other = FindWindowW(L"Shell_TrayWnd", NULL);
    printf("windows %p %p %p %p child %p other-process %p\n", w1, w2, w3, w4, child, other);

    fill(block, 2, w1, w1, w1, w1); attempt("four times the same window, kind 2", block);
    fill(block, 2, w1, w2, w3, w4); attempt("four different windows, kind 2", block);
    fill(block, 2, w1, w1, w1, NULL); attempt("three windows and a NULL", block);
    fill(block, 2, NULL, NULL, NULL, NULL); attempt("all NULL", block);
    fill(block, 1, w1, w1, w1, w1); attempt("kind 1", block);
    fill(block, 0, w1, w1, w1, w1); attempt("kind 0", block);
    fill(block, 3, w1, w1, w1, w1); attempt("kind 3", block);
    fill(block, 2, child, child, child, child); attempt("a child window", block);
    fill(block, 2, other, other, other, other); attempt("a window of another process", block);
    fill(block, 2, w1, w1, w1, w1); *(ULONG_PTR *)(block + 24) = 0x1234; attempt("a nonzero reserved qword in entry 0", block);
    fill(block, 2, w1, w1, w1, w1); *(ULONG_PTR *)block = 0; attempt("size 0", block);
    fill(block, 2, w1, w1, w1, w1); *(ULONG_PTR *)block = 0x1000; attempt("size 0x1000", block);
    DestroyWindow(w1);
    fill(block, 2, w1, w1, w1, w1); attempt("a destroyed window", block);
    return 0;
}
