/*
 * UWP application frameworks probe: a packaged full-trust program that depends on the five framework packages of the
 * uwp-frameworks component (the dependencies of the Windows Calculator 11.2607). It checks its package graph and
 * loads one library of each framework by name.
 *
 *   fwprobe.exe MODE OUTPUT_FILE PACKAGE_FULL_NAME     (started through the U1.0 launcher)
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif
#define PACKAGE_FILTER_HEAD   0x00000010
#define PACKAGE_FILTER_DIRECT 0x00000020

typedef struct
{
    UINT32 reserved;
    UINT32 processorArchitecture;
    UINT64 version;
    WCHAR *name, *publisher, *resourceId, *publisherId;
} probe_package_id;

typedef struct
{
    UINT32 reserved, flags;
    WCHAR *path, *packageFullName, *packageFamilyName;
    probe_package_id packageId;
} probe_package_info;

typedef LONG (WINAPI *GetCurrentPackageInfo_t)( UINT32, UINT32 *, BYTE *, UINT32 * );
typedef LONG (WINAPI *GetCurrentPackageFullName_t)( UINT32 *, WCHAR * );

static const struct
{
    const WCHAR *full_name, *library;
}
frameworks[] =
{
    { L"Microsoft.UI.Xaml.2.8_8.2501.31001.0_x64__8wekyb3d8bbwe", L"Microsoft.UI.Xaml.dll" },
    { L"Microsoft.NET.Native.Framework.2.2_2.2.29512.0_x64__8wekyb3d8bbwe", L"SharedLibrary.dll" },
    { L"Microsoft.NET.Native.Runtime.2.2_2.2.28604.0_x64__8wekyb3d8bbwe", L"mrt100_app.dll" },
    { L"Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe", L"vcruntime140_app.dll" },
    { L"Microsoft.VCLibs.140.00.UWPDesktop_14.0.33728.0_x64__8wekyb3d8bbwe", L"mfc140u.dll" },
};

static int failures;

static void check( const char *name, BOOL ok )
{
    printf( "%s %s\n", ok ? "PASS" : "FAIL", name );
    if (!ok) failures++;
}

int wmain( int argc, WCHAR **argv )
{
    HMODULE kernelbase = GetModuleHandleW( L"kernelbase.dll" );
    GetCurrentPackageInfo_t pGetCurrentPackageInfo;
    GetCurrentPackageFullName_t pGetCurrentPackageFullName;
    probe_package_info *infos;
    WCHAR full[256], path[MAX_PATH];
    UINT32 len = ARRAY_SIZE(full), size = 0, count = 0, i, j;
    BYTE *buffer;
    BOOL all_found = TRUE, all_in_store = TRUE;

    if (argc != 4 || !_wfreopen( argv[2], L"w", stdout )) return 2;
    pGetCurrentPackageInfo = (GetCurrentPackageInfo_t)GetProcAddress( kernelbase, "GetCurrentPackageInfo" );
    pGetCurrentPackageFullName = (GetCurrentPackageFullName_t)GetProcAddress( kernelbase, "GetCurrentPackageFullName" );
    check( "package_functions", pGetCurrentPackageInfo && pGetCurrentPackageFullName );
    if (!pGetCurrentPackageInfo || !pGetCurrentPackageFullName) return 1;

    check( "package_full_name_is_the_activated_one",
           !pGetCurrentPackageFullName( &len, full ) && !wcscmp( full, argv[3] ) );

    pGetCurrentPackageInfo( PACKAGE_FILTER_DIRECT, &size, NULL, &count );
    buffer = calloc( 1, size ? size : 1 );
    if (!buffer || pGetCurrentPackageInfo( PACKAGE_FILTER_DIRECT, &size, buffer, &count )) count = 0;
    infos = (probe_package_info *)buffer;
    check( "five_direct_dependencies", count == ARRAY_SIZE(frameworks) );
    for (i = 0; i < count; i++)
    {
        static const WCHAR store[] = L"C:\\Program Files\\WindowsApps\\";
        printf( "INFO dependency %ls at %ls\n", infos[i].packageFullName, infos[i].path );
        if (_wcsnicmp( infos[i].path, store, wcslen( store ) ) || _wcsicmp( infos[i].path + wcslen( store ), infos[i].packageFullName ))
            all_in_store = FALSE;
    }
    for (j = 0; j < ARRAY_SIZE(frameworks); j++)
    {
        BOOL found = FALSE;
        for (i = 0; i < count; i++) if (!wcscmp( infos[i].packageFullName, frameworks[j].full_name )) found = TRUE;
        if (!found) all_found = FALSE;
    }
    check( "dependencies_are_the_pinned_frameworks", count && all_found );
    check( "frameworks_are_in_the_machine_store", count && all_in_store );

    for (j = 0; j < ARRAY_SIZE(frameworks); j++)
    {
        char name[128];
        HMODULE module = LoadLibraryExW( frameworks[j].library, NULL, 0 );
        DWORD error = module ? 0 : GetLastError();
        BOOL in_framework = FALSE;

        path[0] = 0;
        if (module) GetModuleFileNameW( module, path, ARRAY_SIZE(path) );
        in_framework = module && wcsstr( path, frameworks[j].full_name ) != NULL;
        printf( "INFO %ls: %s %ls (error %lu)\n", frameworks[j].library, module ? "loaded from" : "not loaded", path, error );
        snprintf( name, sizeof(name), "%ls_loads_from_its_framework", frameworks[j].library );
        check( name, in_framework );
    }
    printf( "exit %d\n", failures );
    fflush( stdout );
    return failures ? 1 : 0;
}
