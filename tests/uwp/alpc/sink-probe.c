/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/* Black-box probe (Windows host): the composition input sink syscalls of win32u. Calls the exported stubs the way the Windows App Runtime
 * (Microsoft.InputStateManager.dll) calls them, as a trace shows them. The registers of a trace suggest four arguments, but 32-bit Windows removes only 8 bytes for
 * both: they are NtCreateCompositionInputSink(const void *params, HANDLE *out) and NtQueryCompositionInputSinkLuid(HANDLE sink, LUID *luid);
 * the other registers are the caller's (sink-probe2.c and sink-pops32.cpp use the real signatures). Prints the status
 * of each variation, the object type of the handle and whether the LUID is stable. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef NTSTATUS (NTAPI *create_t)(const void *params, HANDLE *out, ULONG_PTR flags, ULONG id);
typedef NTSTATUS (NTAPI *query_luid_t)(HANDLE sink, LUID *luid, const GUID *kind, void *out2);
typedef NTSTATUS (NTAPI *close_t)(HANDLE sink);
typedef NTSTATUS (NTAPI *query_obj_t)(HANDLE, int, void *, ULONG, ULONG *);
typedef NTSTATUS (NTAPI *dup_t)(HANDLE, HANDLE *);
typedef HRESULT (WINAPI *create_vh_t)(HANDLE *);

static const GUID kind_a = {0x921f9cc0, 0xae1f, 0x48bc, {0xa3, 0x54, 0x8f, 0x7a, 0x88, 0x00, 0xf2, 0xfb}};

static void type_of(query_obj_t qo, HANDLE h)
{
    BYTE buf[512];
    ULONG len = 0;
    NTSTATUS st = qo(h, 2 /* ObjectTypeInformation */, buf, sizeof(buf), &len);
    if (!st)
    {
        UNICODE_STRING *name = (UNICODE_STRING *)buf;
        printf("    type: %.*ls\n", name->Length / 2, name->Buffer);
    }
    else printf("    type query: %08lx\n", (ULONG)st);
}

int main(void)
{
    HMODULE w32u = LoadLibraryW(L"win32u.dll");
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    create_t create = (create_t)GetProcAddress(w32u, "NtCreateCompositionInputSink");
    query_luid_t qluid = (query_luid_t)GetProcAddress(w32u, "NtQueryCompositionInputSinkLuid");
    close_t close_sink = (close_t)GetProcAddress(w32u, "NtCloseCompositionInputSink");
    dup_t dup_sink = (dup_t)GetProcAddress(w32u, "NtDuplicateCompositionInputSink");
    query_obj_t qo = (query_obj_t)GetProcAddress(ntdll, "NtQueryObject");
    create_vh_t create_vh = (create_vh_t)GetProcAddress(LoadLibraryW(L"dcomp.dll"), "DCompositionCreateSharedVisualHandle");
    HANDLE comp = NULL, sink = NULL, sink2 = NULL;
    ULONG_PTR paramv[4] = {0};
    NTSTATUS st;
    LUID l1 = {0}, l2 = {0};
    int variant;

    printf("create %p query_luid %p close %p dup %p\n", create, qluid, close_sink, dup_sink);
    if (create_vh) printf("shared visual handle: %08lx %p\n", create_vh(&comp), comp);

    /* the first argument, as observed: a block of 0xe8 bytes, a size and four entries of 0x38 bytes {kind 2, hwnd, zeros} */
    {
        HWND hwnd = CreateWindowW(L"static", L"sink", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0);
        BYTE block[0xe8];
        int e;
        printf("window %p\n", hwnd);
        for (variant = 0; variant < 7; variant++)
        {
            const char *what;
            ULONG_PTR flags = (ULONG_PTR)0x7ffffffffffffffcull;
            ULONG id = 0x57 + variant;
            memset(block, 0, sizeof(block));
            *(ULONG_PTR *)block = 0xe8;
            for (e = 0; e < 4; e++) { *(ULONG_PTR *)(block + 8 + e * 0x38) = 2; *(HWND *)(block + 16 + e * 0x38) = hwnd; }
            switch (variant)
            {
            case 0: what = "the observed block with a window of this process"; break;
            case 1: *(HWND *)(block + 16 + 0x38) = NULL; *(HWND *)(block + 16 + 0x70) = NULL; *(HWND *)(block + 16 + 0xa8) = NULL; what = "only the first entry has a window"; break;
            case 2: *(ULONG_PTR *)block = 0x40; what = "size 0x40"; break;
            case 3: *(HWND *)(block + 16) = (HWND)0x1234; what = "first window is not a window"; break;
            case 4: flags = 0; what = "flags 0"; break;
            case 5: id = 0; what = "id 0"; break;
            default: *(HWND *)(block + 16) = GetDesktopWindow(); what = "first window is the desktop window"; break;
            }
            sink = (HANDLE)(ULONG_PTR)0xdead;
            __try { st = create(block, &sink, flags, id); } __except (EXCEPTION_EXECUTE_HANDLER) { st = 0xc0000005; }
            printf("create(%s): %08lx handle %p\n", what, (ULONG)st, sink);
            if (!st && sink && sink != (HANDLE)0xdead)
            {
                type_of(qo, sink);
                if (!sink2) sink2 = sink;
            }
        }
        {
            NTSTATUS s2;
            sink = (HANDLE)(ULONG_PTR)0xdead;
            __try { s2 = create(NULL, &sink, (ULONG_PTR)0x7ffffffffffffffcull, 1); } __except (EXCEPTION_EXECUTE_HANDLER) { s2 = 0xc0000005; }
            printf("create(NULL block): %08lx\n", (ULONG)s2);
            __try { s2 = create(block, NULL, (ULONG_PTR)0x7ffffffffffffffcull, 1); } __except (EXCEPTION_EXECUTE_HANDLER) { s2 = 0xc0000005; }
            printf("create(NULL out): %08lx\n", (ULONG)s2);
        }
    }
    if (sink2)
    {
        BYTE out2[64] = {0xcc};
        st = qluid(sink2, &l1, &kind_a, out2);
        printf("query luid #1: %08lx luid %08lx:%08lx out2[0] %02x\n", (ULONG)st, l1.HighPart, l1.LowPart, out2[0]);
        st = qluid(sink2, &l2, &kind_a, out2);
        printf("query luid #2: %08lx luid %08lx:%08lx (stable: %d)\n", (ULONG)st, l2.HighPart, l2.LowPart, l1.LowPart == l2.LowPart);
        st = qluid(sink2, NULL, &kind_a, out2);
        printf("query luid NULL luid: %08lx\n", (ULONG)st);
        st = qluid(sink2, &l2, NULL, out2);
        printf("query luid NULL kind: %08lx\n", (ULONG)st);
        st = qluid((HANDLE)0x12345678, &l2, &kind_a, out2);
        printf("query luid bad handle: %08lx\n", (ULONG)st);
        st = qluid(comp, &l2, &kind_a, out2);
        printf("query luid wrong type (shared visual handle): %08lx\n", (ULONG)st);

        if (dup_sink)
        {
            HANDLE d = NULL;
            __try { st = dup_sink(sink2, &d); } __except (EXCEPTION_EXECUTE_HANDLER) { st = 0xc0000005; }
            printf("duplicate(sink, &h): %08lx %p\n", (ULONG)st, d);
            if (!st && d) { st = qluid(d, &l2, &kind_a, out2); printf("  luid of duplicate: %08lx %08lx (same: %d)\n", (ULONG)st, l2.LowPart, l1.LowPart == l2.LowPart); }
        }
        if (close_sink)
        {
            __try { st = close_sink(sink2); } __except (EXCEPTION_EXECUTE_HANDLER) { st = 0xc0000005; }
            printf("close sink: %08lx\n", (ULONG)st);
            st = qluid(sink2, &l2, &kind_a, out2);
            printf("query luid after close: %08lx\n", (ULONG)st);
            st = close_sink(sink2);
            printf("close again: %08lx\n", (ULONG)st);
        }
        printf("NtClose(sink): %08lx\n", (ULONG)((NTSTATUS (NTAPI *)(HANDLE))GetProcAddress(ntdll, "NtClose"))(sink2));
    }
    return 0;
}
