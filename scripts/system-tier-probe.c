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
 *   probe setdacl KEY SDDL        sets the key's DACL from an SDDL string; prints setdacl=<error code>
 *   probe getdacl KEY             prints dacl=<the key's DACL as SDDL>
 *   probe whoami                  prints user=<the token user's SID> and session=<the process's session id>
 *   probe rpcbind PIPE            opens \\.\pipe\PIPE in message mode, sends a DCE/RPC bind for the service control
 *                                 manager's interface and prints pipe=open (or pipe=error<n>) and bind=<the answer's
 *                                 packet type: 12 bind_ack, 13 bind_nak>
 *   probe pipecreate PIPE         tries to create \\.\pipe\PIPE; prints create=ok or create=error<n>
 * Build: x86_64-w64-mingw32-gcc / i686-w64-mingw32-gcc -municode -o probe.exe system-tier-probe.c -ladvapi32 -lntdll */

#include <windows.h>
#include <winternl.h>
#include <sddl.h>
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

    if (argc == 2 && !lstrcmpW( argv[1], L"whoami" ))
    {
        char buffer[256];
        TOKEN_USER *user = (TOKEN_USER *)buffer;
        HANDLE token;
        DWORD size;
        WCHAR *str;

        if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ) ||
            !GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &size ) ||
            !ConvertSidToStringSidW( user->User.Sid, &str ))
        {
            printf( "user=error%lu\n", GetLastError() );
            return 1;
        }
        printf( "user=%ls\n", str );
        LocalFree( str );
        if (ProcessIdToSessionId( GetCurrentProcessId(), &size )) printf( "session=%lu\n", size );
        return 0;
    }
    if (argc == 3 && !lstrcmpW( argv[1], L"rpcbind" ))
    {
        static const GUID svcctl = { 0x367abb81, 0x9844, 0x35f1, { 0xad, 0x32, 0x98, 0xf0, 0x38, 0x00, 0x10, 0x03 } };
        static const GUID ndr = { 0x8a885d04, 0x1ceb, 0x11c9, { 0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60 } };
        unsigned char bind[72] = { 5, 0, 11, 3, 0x10, 0, 0, 0, 72, 0, 0, 0, 1, 0, 0, 0,
                                   0xb8, 0x10, 0xb8, 0x10, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0 };
        unsigned char answer[1024];
        WCHAR path[MAX_PATH];
        DWORD mode = PIPE_READMODE_MESSAGE, done;
        HANDLE pipe;

        memcpy( bind + 32, &svcctl, 16 );
        bind[48] = 2;                       /* interface version 2.0 */
        memcpy( bind + 52, &ndr, 16 );
        bind[68] = 2;                       /* NDR version 2 */
        lstrcpyW( path, L"\\\\.\\pipe\\" );
        lstrcpynW( path + 9, argv[2], MAX_PATH - 9 );
        pipe = CreateFileW( path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
        if (pipe == INVALID_HANDLE_VALUE)
        {
            printf( "pipe=error%lu\n", GetLastError() );
            return 1;
        }
        printf( "pipe=open\n" );
        if (!SetNamedPipeHandleState( pipe, &mode, NULL, NULL )) printf( "mode=error%lu\n", GetLastError() );
        if (!WriteFile( pipe, bind, sizeof(bind), &done, NULL ) || done != sizeof(bind))
            printf( "bind=writeerror%lu\n", GetLastError() );
        else if (!ReadFile( pipe, answer, sizeof(answer), &done, NULL ) || done < 16)
            printf( "bind=readerror%lu\n", GetLastError() );
        else
            printf( "bind=%u size=%lu\n", answer[2], done );
        CloseHandle( pipe );
        return 0;
    }
    if (argc == 3 && !lstrcmpW( argv[1], L"pipecreate" ))
    {
        WCHAR path[MAX_PATH];
        HANDLE pipe;

        lstrcpyW( path, L"\\\\.\\pipe\\" );
        lstrcpynW( path + 9, argv[2], MAX_PATH - 9 );
        pipe = CreateNamedPipeW( path, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, 1, 4096, 4096,
                                 0, NULL );
        if (pipe == INVALID_HANDLE_VALUE) printf( "create=error%lu\n", GetLastError() );
        else
        {
            printf( "create=ok\n" );
            CloseHandle( pipe );
        }
        return 0;
    }
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
    else if (!lstrcmpW( argv[1], L"setdacl" ) && argc == 4)
    {
        PSECURITY_DESCRIPTOR sd;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW( argv[3], SDDL_REVISION_1, &sd, NULL ))
            printf( "setdacl=error%lu\n", GetLastError() );
        else
        {
            printf( "setdacl=%ld\n", RegSetKeySecurity( key, DACL_SECURITY_INFORMATION, sd ));
            LocalFree( sd );
        }
    }
    else if (!lstrcmpW( argv[1], L"getdacl" ))
    {
        char buffer[4096];
        DWORD size = sizeof(buffer);
        WCHAR *sddl;
        ret = RegGetKeySecurity( key, DACL_SECURITY_INFORMATION, buffer, &size );
        if (ret) printf( "dacl=error%ld\n", ret );
        else if (!ConvertSecurityDescriptorToStringSecurityDescriptorW( buffer, SDDL_REVISION_1, DACL_SECURITY_INFORMATION,
                                                                       &sddl, NULL ))
            printf( "dacl=error%lu\n", GetLastError() );
        else
        {
            printf( "dacl=%ls\n", sddl );
            LocalFree( sddl );
        }
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
