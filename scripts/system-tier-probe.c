/*
 * WineExplorer Desktop Environment - probe for the system tier's machine registry (session 0)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

/* Usage (KEY is relative to HKEY_LOCAL_MACHINE):
 *   probe hold KEY VALUE MARKER   reads VALUE, waits until the file MARKER exists, reads VALUE again through the
 *                                 same handle and creates the subkey "Held" under it; prints first=, second=, sub=
 *   probe dup KEY VALUE           reads VALUE through a duplicate of the key handle; prints dup=
 *   probe name KEY                prints name= (NtQueryObject) and sd= (size of the key's security descriptor)
 *   probe notify KEY MS           waits up to MS ms for a change below KEY; prints notify=changed or notify=timeout
 * Build: x86_64-w64-mingw32-gcc / i686-w64-mingw32-gcc -municode -o probe.exe system-tier-probe.c -ladvapi32 -lntdll */

#include <windows.h>
#include <winternl.h>
#include <stdio.h>

static void print_value( const char *label, HKEY key, const WCHAR *name )
{
    WCHAR data[256];
    DWORD size = sizeof(data), type;
    LSTATUS ret = RegQueryValueExW( key, name, NULL, &type, (BYTE *)data, &size );

    if (ret) printf( "%s=error%ld\n", label, ret );
    else printf( "%s=%ls\n", label, data );
    fflush( stdout );
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    HKEY key, sub;
    LSTATUS ret;

    if (argc < 3) return 2;
    if ((ret = RegOpenKeyExW( HKEY_LOCAL_MACHINE, argv[2], 0, KEY_ALL_ACCESS & ~WRITE_DAC & ~WRITE_OWNER, &key )) &&
        (ret = RegOpenKeyExW( HKEY_LOCAL_MACHINE, argv[2], 0, KEY_READ | KEY_NOTIFY, &key )))
    {
        printf( "open=error%ld\n", ret );
        return 1;
    }

    if (!lstrcmpW( argv[1], L"hold" ) && argc == 5)
    {
        int i;
        print_value( "first", key, argv[3] );
        for (i = 0; i < 1200 && GetFileAttributesW( argv[4] ) == INVALID_FILE_ATTRIBUTES; i++) Sleep( 100 );
        print_value( "second", key, argv[3] );
        ret = RegCreateKeyExW( key, L"Held", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, NULL );
        printf( "sub=%ld\n", ret );
        if (!ret) RegCloseKey( sub );
    }
    else if (!lstrcmpW( argv[1], L"dup" ) && argc == 4)
    {
        HANDLE copy;
        if (!DuplicateHandle( GetCurrentProcess(), key, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS ))
            printf( "dup=error%lu\n", GetLastError() );
        else
        {
            print_value( "dup", (HKEY)copy, argv[3] );
            RegCloseKey( (HKEY)copy );
        }
    }
    else if (!lstrcmpW( argv[1], L"name" ))
    {
        char buffer[1024];
        OBJECT_NAME_INFORMATION *info = (OBJECT_NAME_INFORMATION *)buffer;
        ULONG len;
        DWORD size = 0;
        NTSTATUS status = NtQueryObject( key, ObjectNameInformation, info, sizeof(buffer) - sizeof(WCHAR), &len );

        if (status) printf( "name=error%08lx\n", status );
        else printf( "name=%.*ls\n", (int)(info->Name.Length / sizeof(WCHAR)), info->Name.Buffer );
        ret = RegGetKeySecurity( key, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, NULL, &size );
        printf( "sd=%lu ret=%ld\n", size, ret );
    }
    else if (!lstrcmpW( argv[1], L"notify" ) && argc == 4)
    {
        HANDLE event = CreateEventW( NULL, FALSE, FALSE, NULL );
        ret = RegNotifyChangeKeyValue( key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, event, TRUE );
        if (ret) printf( "notify=error%ld\n", ret );
        else
        {
            printf( "armed\n" );
            fflush( stdout );
            printf( "notify=%s\n", WaitForSingleObject( event, _wtoi( argv[3] )) == WAIT_OBJECT_0 ? "changed" : "timeout" );
        }
    }
    else return 2;
    RegCloseKey( key );
    return 0;
}
