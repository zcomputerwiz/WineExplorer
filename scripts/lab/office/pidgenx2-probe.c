/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe of pidgenx.dll's PidGenX2: call it the way Office 2016 setup does and print what
 * it writes.  Run it against a native DLL and against the builtin and compare.  32- or 64-bit.
 *
 *   i686-w64-mingw32-gcc -municode -o pidgenx2-probe.exe pidgenx2-probe.c
 *   wine pidgenx2-probe.exe DLL KEY PKEYCONFIG MPC [GROUP...]
 * GROUP (decimal, up to 8) are the key groups passed in the fifth argument's block.  32-bit only as
 * written (the block holds a pointer in a DWORD).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef HRESULT (WINAPI *pidgenx2_proc)( const WCHAR *key, const WCHAR *config, const WCHAR *mpc,
                                         const WCHAR *oem, DWORD *info, WCHAR *pid, BYTE *dpid3, BYTE *dpid4 );

static void dump( const char *name, const BYTE *data, unsigned int size )
{
    unsigned int i, j;

    printf( "%s:\n", name );
    for (i = 0; i < size; i += 16)
    {
        for (j = 0; j < 16 && data[i + j] == 0; j++) ;
        if (j == 16) continue;
        printf( "  %04x ", i );
        for (j = 0; j < 16; j++) printf( "%02x", data[i + j] );
        printf( "\n" );
    }
}

int wmain( int argc, WCHAR **argv )
{
    static BYTE dpid3[0xa4], dpid4[0x4f8];
    static WCHAR pid[128];
    static DWORD groups[8];
    DWORD info[8], fill = 0xaaaaaaaa;
    pidgenx2_proc proc;
    HMODULE module;
    HRESULT hr;
    int i;

    if (argc < 5) return 2;
    if (!(module = LoadLibraryW( argv[1] )) || !(proc = (pidgenx2_proc)GetProcAddress( module, "PidGenX2" )))
    {
        printf( "no PidGenX2 (error %lu)\n", GetLastError() );
        return 2;
    }
    for (i = 0; i < 8; i++) info[i] = fill;
    /* what setup passes: size, 0, the key groups the product accepts, their count, one more word */
    info[0] = 0x14;
    info[1] = 0;
    info[2] = (DWORD)(UINT_PTR)groups;
    for (i = 5; i < argc && i < 5 + 8; i++) groups[i - 5] = wcstoul( argv[i], NULL, 10 );
    info[3] = i - 5;
    *(DWORD *)dpid3 = sizeof(dpid3);
    *(DWORD *)dpid4 = sizeof(dpid4);
    hr = proc( argv[2], argv[3], argv[4], NULL, info, pid, dpid3, dpid4 );
    printf( "result 0x%08lx\n", hr );
    printf( "info: %08lx %08lx %08lx %08lx %08lx | %08lx\n", info[0], info[1], info[2], info[3], info[4], info[5] );
    printf( "pid: %ls\n", pid );
    dump( "pid raw", (BYTE *)pid, sizeof(pid) );
    dump( "dpid3", dpid3, sizeof(dpid3) );
    dump( "dpid4", dpid4, sizeof(dpid4) );
    return 0;
}
