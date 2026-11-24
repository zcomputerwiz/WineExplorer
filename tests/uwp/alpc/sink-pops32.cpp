// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Black-box probe (32-bit Windows): bytes of arguments removed by the composition input sink syscalls of win32u (stdcall callee cleanup),
// called with invalid arguments only (the calls fail with a status).
#include <windows.h>
#include <stdio.h>

static int pops(void *fn, int nargs)
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
            call fn
            mov hr, eax
            mov after, esp
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1000; }
    (void)hr; (void)nargs;
    return after - (before - 32);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    GetDesktopWindow();
    HMODULE w = LoadLibraryW(L"win32u.dll");
    const char *names[] = {"NtCreateCompositionInputSink", "NtQueryCompositionInputSinkLuid", "NtCloseCompositionInputSink",
                           "NtDuplicateCompositionInputSink", "NtCreateImplicitCompositionInputSink", "NtQueryCompositionInputSink",
                           "NtQueryCompositionInputSinkViewId"};
    for (int i = 0; i < 7; i++)
    {
        void *fn = (void *)GetProcAddress(w, names[i]);
        fflush(stdout); printf("%s: callee removed %d bytes\n", names[i], fn ? pops(fn, 8) : -1);
    }
    return 0;
}
