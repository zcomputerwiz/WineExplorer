/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: how many bytes of arguments does a stdcall export take off the stack?
 * It calls the export with 24 identical argument words and measures the stack pointer after
 * the return.  Nothing of the DLL is read or disassembled.  32-bit only.
 *
 *   i686-w64-mingw32-gcc -O1 -fno-omit-frame-pointer -o stdcall-args-probe.exe stdcall-args-probe.c
 *   wine stdcall-args-probe.exe DLL EXPORT [null|buffer]
 */
#include <windows.h>
#include <stdio.h>

static LONG WINAPI on_fault( EXCEPTION_POINTERS *info )
{
    printf( "fault 0x%08lx at %p\n", info->ExceptionRecord->ExceptionCode,
            info->ExceptionRecord->ExceptionAddress );
    fflush( stdout );
    ExitProcess( 3 );
}

int main( int argc, char **argv )
{
    static WCHAR buffer[4096];
    unsigned int ret, taken, fill;
    HMODULE module;
    void *proc;

    if (argc < 3) return 2;
    if (!(module = LoadLibraryA( argv[1] )) || !(proc = GetProcAddress( module, argv[2] )))
    {
        printf( "cannot find %s in %s (error %lu)\n", argv[2], argv[1], GetLastError() );
        return 2;
    }
    fill = (argc > 3 && !strcmp( argv[3], "buffer" )) ? (unsigned int)(UINT_PTR)buffer : 0;
    SetUnhandledExceptionFilter( on_fault );
    __asm__ volatile( "movl %%esp, %%edi\n"
                      "movl $24, %%ecx\n"
                      "1: pushl %%ebx\n"
                      "decl %%ecx\n"
                      "jnz 1b\n"
                      "movl %%esp, %%esi\n"
                      "call *%%eax\n"
                      "movl %%esp, %%edx\n"
                      "subl %%esi, %%edx\n"
                      "movl %%edi, %%esp\n"
                      : "=a" (ret), "=d" (taken)
                      : "a" (proc), "b" (fill)
                      : "ecx", "esi", "edi", "memory", "cc" );
    printf( "%s: returned 0x%08x, took %u bytes (%u arguments)\n", argv[2], ret, taken, taken / 4 );
    return 0;
}
