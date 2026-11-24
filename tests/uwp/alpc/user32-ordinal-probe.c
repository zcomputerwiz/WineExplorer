/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: what does an unnamed export of user32.dll do when it is called with no arguments, from a plain console process and from
 * a process with a window and a message queue? The Windows App Runtime (Microsoft.InputStateManager.dll) gets user32 ordinal 2574 with
 * GetProcAddress and calls it; Wine has no such export and aborts. This prints the return value (and its class: null, small integer,
 * pointer into user32, handle) and GetLastError, without arguments, for the ordinals given.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o user32-ordinal-probe.exe user32-ordinal-probe.c -luser32
 *   user32-ordinal-probe.exe 2574 [ORDINAL...]
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static LONG CALLBACK veh(EXCEPTION_POINTERS *p)
{
    printf("  exception %08lx at %p\n", p->ExceptionRecord->ExceptionCode, p->ExceptionRecord->ExceptionAddress);
    ExitProcess(3);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void classify(ULONG_PTR v)
{
    HMODULE m = NULL;
    char name[MAX_PATH] = "";

    if (!v) { printf("  value 0\n"); return; }
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (void *)v, &m))
        GetModuleFileNameA(m, name, sizeof(name));
    printf("  value %#llx%s%s%s\n", (unsigned long long)v, name[0] ? " (in " : "", name, name[0] ? ")" : "");
    if (IsWindow((HWND)v)) printf("  it is a window handle\n");
}

int main(int argc, char **argv)
{
    HMODULE user32 = LoadLibraryA("user32.dll");
    HWND hwnd;
    MSG msg;
    int i, pass;

    setvbuf(stdout, NULL, _IONBF, 0);
    AddVectoredExceptionHandler(1, veh);
    for (pass = 0; pass < 2; pass++)
    {
        if (pass == 1)
        {
            WNDCLASSA wc = {0};
            wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "probe";
            RegisterClassA(&wc);
            hwnd = CreateWindowA("probe", "probe", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
            PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE);
            printf("-- with a window %p and a message queue\n", hwnd);
        }
        else printf("-- plain process\n");
        for (i = 1; i < argc; i++)
        {
            int ord = atoi(argv[i]);
            ULONG_PTR (WINAPI *fn)(void) = (void *)GetProcAddress(user32, (LPCSTR)(ULONG_PTR)ord);
            ULONG_PTR v;

            printf("ordinal %d: %p\n", ord, (void *)fn);
            if (!fn) continue;
            if (getenv("ONE_ARG"))
            {
                /* with one argument: NULL, the desktop window, a window of this process (pass 2), a bad handle */
                ULONG_PTR (WINAPI *fn1)(ULONG_PTR) = (void *)fn;
                ULONG_PTR args[4] = { 0, (ULONG_PTR)GetDesktopWindow(), pass ? (ULONG_PTR)hwnd : 0, 0x12345678 };
                int k;

                for (k = 0; k < 4; k++)
                {
                    SetLastError(0xdeadbeef);
                    v = fn1(args[k]);
                    printf("  (%#llx): returns %#llx, GetLastError %lu\n", (unsigned long long)args[k], (unsigned long long)v, GetLastError());
                }
                continue;
            }
            SetLastError(0xdeadbeef);
            v = fn();
            printf("  GetLastError %lu\n", GetLastError());
            classify(v);
        }
    }
    return 0;
}
