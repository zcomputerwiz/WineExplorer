/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe of the documented SLSetAuthenticationData / SLGetAuthenticationResult pair (slpublic.h): which
 * error does the licensing client return for data of various sizes and first bytes?  Only the public API is called,
 * with made-up data; no license, key or setting is read or changed.  Run on Windows and compare with Wine.
 *
 *   x86_64-w64-mingw32-gcc -municode -o sppc-auth-probe.exe sppc-auth-probe.c
 *   sppc-auth-probe.exe [DLL]          DLL: sppc.dll (default) or Office's OSPPC.DLL
 */
#include <windows.h>
#include <stdio.h>

typedef void *HSLC;

int wmain( int argc, WCHAR **argv )
{
    static const unsigned int sizes[] = { 0, 1, 16, 64, 256, 288, 289, 512 };
    HMODULE module = LoadLibraryW( argc > 1 ? argv[1] : L"sppc.dll" );
    HRESULT (WINAPI *open)( HSLC * ), (WINAPI *close)( HSLC );
    HRESULT (WINAPI *set)( HSLC, UINT, const BYTE * );
    HRESULT (WINAPI *get)( HSLC, UINT *, BYTE ** );
    unsigned int i, version;
    BYTE data[512];
    HSLC handle;

    if (!module) return 2;
    open = (void *)GetProcAddress( module, "SLOpen" );
    close = (void *)GetProcAddress( module, "SLClose" );
    set = (void *)GetProcAddress( module, "SLSetAuthenticationData" );
    get = (void *)GetProcAddress( module, "SLGetAuthenticationResult" );
    if (!open || !close || !set || !get) return 2;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        for (version = 0; version < 4; version++)
        {
            UINT size = 0;
            BYTE *result = NULL;
            HRESULT hr_set, hr_get;

            if (FAILED( open( &handle ))) return 1;
            memset( data, 0x5a, sizeof(data) );
            data[0] = version;
            hr_set = set( handle, sizes[i], sizes[i] ? data : NULL );
            hr_get = get( handle, &size, &result );
            printf( "size %3u first byte %u: set 0x%08lx, get 0x%08lx (result %u bytes)\n", sizes[i], version,
                    hr_set, hr_get, SUCCEEDED( hr_get ) ? size : 0 );
            if (SUCCEEDED( hr_get ) && result) LocalFree( result );
            close( handle );
        }
    }
    return 0;
}
