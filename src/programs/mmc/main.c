/*
 * Microsoft Management Console: opens the consoles (.msc) of the snap-ins Wine has
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* There is no console framework: a console file names its snap-ins by CLSID, and the snap-ins Wine has show
 * their own window, as Device Manager (devmgr.dll) does on Windows XP when started by itself. */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <shellapi.h>

#include "resource.h"

typedef BOOL (WINAPI *EXECUTE)( HWND, HINSTANCE, const WCHAR *, int );

static const struct { const char *clsid; const WCHAR *dll; const char *entry; } snapins[] =
{
    { "{74246BFC-4C96-11D0-ABEF-0020AF6B0B7A}", L"devmgr.dll", "DeviceManager_ExecuteW" },   /* Device Manager */
};

static char *read_console( const WCHAR *path )
{
    HANDLE file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    DWORD size, read;
    char *data = NULL;

    if (file == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize( file, NULL );
    if (size < 1 << 24 && (data = malloc( size + 1 )) && ReadFile( file, data, size, &read, NULL ))
    {
        data[read] = 0;
        _strupr( data );
    }
    else
    {
        free( data );
        data = NULL;
    }
    CloseHandle( file );
    return data;
}

static void cannot_open( HINSTANCE instance, const WCHAR *path )
{
    WCHAR format[512], text[1024], title[128];

    LoadStringW( instance, IDS_CANNOT_OPEN, format, ARRAY_SIZE(format) );
    LoadStringW( instance, IDS_TITLE, title, ARRAY_SIZE(title) );
    swprintf( text, ARRAY_SIZE(text), format, path );
    MessageBoxW( NULL, text, title, MB_OK | MB_ICONERROR );
}

int WINAPI wWinMain( HINSTANCE instance, HINSTANCE previous, WCHAR *cmdline, int show )
{
    WCHAR **argv, path[MAX_PATH], *file;
    unsigned int i;
    char *console;
    int argc;

    argv = CommandLineToArgvW( GetCommandLineW(), &argc );
    if (argc < 2)
    {
        cannot_open( instance, L"" );
        return 1;
    }
    /* "devmgmt.msc" is in system32, as on Windows */
    if (!SearchPathW( NULL, argv[1], NULL, ARRAY_SIZE(path), path, &file )) lstrcpynW( path, argv[1], MAX_PATH );
    if (!(console = read_console( path )))
    {
        cannot_open( instance, path );
        return 1;
    }
    for (i = 0; i < ARRAY_SIZE(snapins); i++)
    {
        HMODULE dll;
        EXECUTE execute;

        if (!strstr( console, snapins[i].clsid )) continue;
        free( console );
        if (!(dll = LoadLibraryW( snapins[i].dll )) || !(execute = (EXECUTE)GetProcAddress( dll, snapins[i].entry )))
            break;
        return !execute( NULL, instance, NULL, show );
    }
    cannot_open( instance, path );
    return 1;
}
