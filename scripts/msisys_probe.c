/*
 * msisys_probe: the test side of msi's SYSTEM custom action host (docs/MSI_NOIMPERSONATE_SYSTEM.md)
 *
 * Built twice:
 *   - as a DLL (-DMSISYS_DLL), a custom action: its export Facts writes, to the path in CustomActionData, the token it
 *     runs with and what msi lets it see (the facts below);
 *   - as an EXE: "msisys_probe gate ARCH FLAGS" sends WexMsiServer's request itself and prints the service's answer
 *     (gate=STATUS), and when it is 0 whether the host started (host=ready).
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef MSISYS_DLL

#include <msi.h>
#include <msiquery.h>

extern IMAGE_DOS_HEADER __ImageBase;

static void token_facts( FILE *out )
{
    BYTE buf[4096];
    DWORD len, value, session = 0;
    HANDLE token;
    char *str;

    ProcessIdToSessionId( GetCurrentProcessId(), &session );
    fprintf( out, "session=%lu\nbits=%u\n", session, (unsigned)sizeof(void *) * 8 );
    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token )) return;
    if (GetTokenInformation( token, TokenUser, buf, sizeof(buf), &len ) &&
            ConvertSidToStringSidA( ((TOKEN_USER *)buf)->User.Sid, &str ))
    {
        fprintf( out, "user=%s\n", str );
        LocalFree( str );
    }
    if (GetTokenInformation( token, TokenElevationType, &value, sizeof(value), &len ))
        fprintf( out, "elevation=%lu\n", value );
    if (GetTokenInformation( token, TokenIntegrityLevel, buf, sizeof(buf), &len ))
    {
        SID *sid = ((TOKEN_MANDATORY_LABEL *)buf)->Label.Sid;
        fprintf( out, "integrity=%lu\n", sid->SubAuthority[sid->SubAuthorityCount - 1] );
    }
    CloseHandle( token );
}

static void property( FILE *out, MSIHANDLE h, const char *name )
{
    char value[512];
    DWORD size = sizeof(value);
    UINT r = MsiGetPropertyA( h, name, value, &size );

    fprintf( out, "prop_%s=%s\nprop_%s_status=%u\n", name, r ? "" : value, name, r );
}

/* exported undecorated (-Wl,--kill-at): msi asks for "Facts" */
__declspec(dllexport) UINT WINAPI Facts( MSIHANDLE h )
{
    char path[MAX_PATH], module[MAX_PATH], flag[MAX_PATH], *wait;
    DWORD size = sizeof(path);
    MSIHANDLE db, rec;
    FILE *out;
    int i;

    if (MsiGetPropertyA( h, "CustomActionData", path, &size ) || !path[0]) return ERROR_INSTALL_FAILURE;
    /* "OUT|WAITDIR": hold while the test looks at the running host (its folder's mode) or ends it: WAITDIR\waiting
     * says we run, WAITDIR\go lets us go on (120 s at most) */
    if ((wait = strchr( path, '|' )))
    {
        *wait++ = 0;
        snprintf( flag, sizeof(flag), "%s\\waiting", wait );
        if ((out = fopen( flag, "w" ))) fclose( out );
        snprintf( flag, sizeof(flag), "%s\\go", wait );
        for (i = 0; i < 1200 && GetFileAttributesA( flag ) == INVALID_FILE_ATTRIBUTES; i++) Sleep( 100 );
    }
    if (!(out = fopen( path, "w" ))) return ERROR_INSTALL_FAILURE;
    token_facts( out );
    GetModuleFileNameA( (HMODULE)&__ImageBase, module, sizeof(module) );  /* where the host wrote us */
    fprintf( out, "module=%s\n", module );
    property( out, h, "ProductCode" );
    property( out, h, "ProductName" );       /* not a deferred action's: empty in the SYSTEM host */
    property( out, h, "UserSID" );
    db = MsiGetActiveDatabase( h );          /* 0 for a deferred action */
    fprintf( out, "activedb=%s\n", db ? "handle" : "0" );
    if (db) MsiCloseHandle( db );
    fprintf( out, "setprop=%u\n", MsiSetPropertyA( h, "WexMsisysProbe", "1" ) );
    fprintf( out, "mode_scheduled=%d\n", MsiGetMode( h, MSIRUNMODE_SCHEDULED ) );
    rec = MsiCreateRecord( 1 );
    MsiRecordSetStringA( rec, 0, "msisys probe: [1]" );
    MsiRecordSetStringA( rec, 1, path );
    fprintf( out, "message=%d\n", MsiProcessMessage( h, INSTALLMESSAGE_INFO, rec ) );
    MsiCloseHandle( rec );
    fprintf( out, "done=1\n" );
    fclose( out );
    return ERROR_SUCCESS;
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    return TRUE;
}

#else  /* the EXE */

#include "wine/wex_msisys.h"

static BOOL read_full( HANDLE h, void *buffer, DWORD size )
{
    BYTE *p = buffer;
    DWORD got;

    while (size)
    {
        if (!ReadFile( h, p, size, &got, NULL ) || !got) return FALSE;
        p += got;
        size -= got;
    }
    return TRUE;
}

static int gate( int arch, int flags )
{
    struct wex_msisys_request request = { WEX_MSISYS_MAGIC, WEX_MSISYS_VERSION, arch, flags };
    struct wex_msisys_frame frame;
    struct wex_msisys_ready ready;
    UINT32 status;
    DWORD done;
    HANDLE pipe;

    lstrcpyW( request.product, L"{00000000-0000-0000-0000-000000000000}" );
    lstrcpyW( request.package, L"msisys_probe" );
    pipe = CreateFileW( WEX_MSISYS_PIPE, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL );
    if (pipe == INVALID_HANDLE_VALUE)
    {
        printf( "gate=open-failed:%lu\n", GetLastError() );
        return 1;
    }
    if (!WriteFile( pipe, &request, sizeof(request), &done, NULL ) || !read_full( pipe, &status, sizeof(status) ))
    {
        printf( "gate=no-answer:%lu\n", GetLastError() );
        return 1;
    }
    printf( "gate=%u\n", status );
    if (!status)
    {
        if (read_full( pipe, &frame, sizeof(frame) ) && frame.type == WEX_MSISYS_READY &&
                frame.size == sizeof(ready) && read_full( pipe, &ready, sizeof(ready) ))
            printf( "host=ready\nhost_pid=%u\n", ready.pid );
        else printf( "host=none:%lu\n", GetLastError() );
    }
    CloseHandle( pipe );  /* the host's connection ends: it removes its folder and exits */
    return 0;
}

/* a client that connects and sends nothing: the service must drop it after its deadline (silent=dropped:MS) */
static int silent( void )
{
    DWORD start = GetTickCount(), got;
    BYTE byte;
    HANDLE pipe;

    pipe = CreateFileW( WEX_MSISYS_PIPE, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL );
    if (pipe == INVALID_HANDLE_VALUE)
    {
        printf( "silent=open-failed:%lu\n", GetLastError() );
        return 1;
    }
    if (ReadFile( pipe, &byte, 1, &got, NULL )) printf( "silent=answered\n" );
    else printf( "silent=dropped:%lu\nsilent_error=%lu\n", GetTickCount() - start, GetLastError() );
    CloseHandle( pipe );
    return 0;
}

int main( int argc, char **argv )
{
    if (argc == 4 && !strcmp( argv[1], "gate" )) return gate( atoi( argv[2] ), atoi( argv[3] ) );
    if (argc == 2 && !strcmp( argv[1], "silent" )) return silent();
    fprintf( stderr, "usage: msisys_probe gate ARCH FLAGS | silent\n" );
    return 2;
}

#endif
