/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * What does NtQueryInformationThread return for the class ThreadIoPriority (22)?  Calls the function with made-up
 * buffers and prints the status, the length and the value.  Run on Windows and on Wine and compare.
 *
 *   x86_64-w64-mingw32-gcc -o thread-io-priority-probe.exe thread-io-priority-probe.c
 */
#include <windows.h>
#include <stdio.h>

typedef LONG (WINAPI *query_fn)( HANDLE, int, void *, ULONG, ULONG * );

int main( void )
{
    query_fn query = (query_fn)GetProcAddress( GetModuleHandleA( "ntdll.dll" ), "NtQueryInformationThread" );
    static const ULONG lengths[] = { 0, 1, 4, 8 };
    unsigned int i;

    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
    {
        ULONG value = 0xdeadbeef, ret_len = 0xdead;
        LONG status = query( GetCurrentThread(), 22, &value, lengths[i], &ret_len );

        printf( "length %lu: status 0x%08lx, returned length %lu, value 0x%08lx\n", lengths[i], status, ret_len, value );
    }

    /* the setter, on the probe's own thread only; the value is put back at the end */
    {
        typedef LONG (WINAPI *set_fn)( HANDLE, int, void *, ULONG );
        set_fn set = (set_fn)GetProcAddress( GetModuleHandleA( "ntdll.dll" ), "NtSetInformationThread" );
        ULONG value, ret_len;
        LONG status;

        for (value = 0; value <= 5; value++)
        {
            ULONG now = 0xdeadbeef;

            status = set( GetCurrentThread(), 22, &value, sizeof(value) );
            query( GetCurrentThread(), 22, &now, sizeof(now), &ret_len );
            printf( "set %lu: status 0x%08lx, then reads %lu\n", value, status, now );
        }
        value = 2;
        status = set( GetCurrentThread(), 22, &value, 8 );
        printf( "set with length 8: status 0x%08lx\n", status );
        status = set( GetCurrentThread(), 22, &value, 2 );
        printf( "set with length 2: status 0x%08lx\n", status );
        value = 2;
        set( GetCurrentThread(), 22, &value, sizeof(value) );
    }
    return 0;
}
