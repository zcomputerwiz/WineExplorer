/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Read-only probe of sppc.dll: what the licensing client returns for ids that are not installed.
 * Run it on Windows and on Wine and compare.  It installs and changes nothing, unless "junk" is the
 * second argument: then it also offers the service a license file that is not one (a write request the
 * service must refuse), to see whether the caller is let through to that point.  First argument: the
 * client DLL (default sppc.dll; Office 2016 in Windows 7 mode has its own, OSPPC.DLL).
 *
 *   i686-w64-mingw32-gcc -municode -o sppc-probe.exe sppc-probe.c     (or x86_64-w64-mingw32-gcc)
 */
#include <windows.h>
#include <stdio.h>

typedef void *HSLC;

static FARPROC get( HMODULE module, const char *name )
{
    FARPROC proc = GetProcAddress( module, name );
    if (!proc) printf( "%s: not exported\n", name );
    return proc;
}

int wmain( int argc, WCHAR **argv )
{
    /* made-up ids */
    static const GUID nothing = { 0x5eedf00d, 0x1234, 0x4321, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    HMODULE module = LoadLibraryW( argc > 1 ? argv[1] : L"sppc.dll" );
    HRESULT (WINAPI *open)( HSLC * ) = (void *)get( module, "SLOpen" );
    HRESULT (WINAPI *close)( HSLC ) = (void *)get( module, "SLClose" );
    HRESULT (WINAPI *info)( HSLC, const GUID *, const WCHAR *, DWORD *, UINT *, BYTE ** );
    HRESULT (WINAPI *list)( HSLC, int, const GUID *, int, UINT *, GUID ** ) = (void *)get( module, "SLGetSLIDList" );
    HRESULT (WINAPI *status)( HSLC, const GUID *, const GUID *, const WCHAR *, UINT *, void ** );
    HRESULT (WINAPI *policy)( HSLC, const WCHAR *, DWORD *, UINT *, BYTE ** );
    HRESULT (WINAPI *service)( HSLC, const WCHAR *, DWORD *, UINT *, BYTE ** );
    HRESULT (WINAPI *keys)( HSLC, const GUID *, UINT *, GUID ** );
    static const char *const info_names[] = { "SLGetProductSkuInformation", "SLGetLicenseInformation",
                                              "SLGetApplicationInformation", "SLGetPKeyInformation" };
    static const WCHAR *const values[] = { L"BuildVersion", L"LicenseType", L"Name", L"NoSuchValue" };
    static const WCHAR *const service_values[] = { L"Version", L"ActivePlugins", L"NoSuchValue" };
    HSLC handle = NULL;
    DWORD type;
    UINT size, count, i, j;
    BYTE *data;
    GUID *ids;
    void *st;
    HRESULT hr;

    if (!module || !open || !close) return 2;
    hr = open( &handle );
    printf( "SLOpen: 0x%08lx\n", hr );
    if (FAILED( hr )) return 1;

    for (i = 0; i < 4; i++)
    {
        if (!(info = (void *)get( module, info_names[i] ))) continue;
        for (j = 0; j < 4; j++)
        {
            type = size = 0xdead;
            data = (BYTE *)0xdead;
            hr = info( handle, &nothing, values[j], &type, &size, &data );
            printf( "%s(unknown id, %ls): 0x%08lx size %u data %p\n", info_names[i], values[j], hr, size, data );
        }
    }
    if (list) for (i = 0; i < 5; i++) for (j = 0; j < 5; j++)
    {
        count = 0xdead;
        ids = (GUID *)0xdead;
        hr = list( handle, i, &nothing, j, &count, &ids );
        printf( "SLGetSLIDList(type %u unknown id -> type %u): 0x%08lx count %u ids %p\n", i, j, hr, count, ids );
    }
    if ((status = (void *)get( module, "SLGetLicensingStatusInformation" )))
    {
        count = 0xdead; st = (void *)0xdead;
        hr = status( handle, &nothing, NULL, NULL, &count, &st );
        printf( "SLGetLicensingStatusInformation(unknown app): 0x%08lx count %u %p\n", hr, count, st );
        count = 0xdead; st = (void *)0xdead;
        hr = status( handle, NULL, &nothing, NULL, &count, &st );
        printf( "SLGetLicensingStatusInformation(unknown product): 0x%08lx count %u %p\n", hr, count, st );
        count = 0xdead; st = (void *)0xdead;
        hr = status( handle, &nothing, &nothing, NULL, &count, &st );
        printf( "SLGetLicensingStatusInformation(unknown app and product): 0x%08lx count %u %p\n", hr, count, st );
    }
    if ((policy = (void *)get( module, "SLGetPolicyInformation" )))
    {
        hr = policy( handle, L"NoSuchPolicy", &type, &size, &data );
        printf( "SLGetPolicyInformation(nothing consumed): 0x%08lx\n", hr );
    }
    if ((service = (void *)get( module, "SLGetServiceInformation" ))) for (i = 0; i < 3; i++)
    {
        type = size = 0;
        data = NULL;
        hr = service( handle, service_values[i], &type, &size, &data );
        printf( "SLGetServiceInformation(%ls): 0x%08lx type %lu size %u", service_values[i], hr, type, size );
        if (SUCCEEDED( hr ) && type == REG_SZ) printf( " %ls", (WCHAR *)data );
        printf( "\n" );
    }
    if ((keys = (void *)get( module, "SLGetInstalledProductKeyIds" )))
    {
        count = 0xdead; ids = (GUID *)0xdead;
        hr = keys( handle, &nothing, &count, &ids );
        printf( "SLGetInstalledProductKeyIds(unknown product): 0x%08lx count %u ids %p\n", hr, count, ids );
    }
    if (argc > 2 && !wcscmp( argv[2], L"junk" ))
    {
        HRESULT (WINAPI *install)( HSLC, UINT, const BYTE *, GUID * ) = (void *)get( module, "SLInstallLicense" );
        static const BYTE junk[] = "<not-a-license/>";
        GUID id;

        if (install) printf( "SLInstallLicense(junk): 0x%08lx\n", install( handle, sizeof(junk) - 1, junk, &id ) );
    }
    printf( "SLClose: 0x%08lx\n", close( handle ) );
    return 0;
}
