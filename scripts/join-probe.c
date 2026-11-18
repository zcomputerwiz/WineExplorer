/*
 * Probe of the domain join API (NetJoinDomain, NetUnjoinDomain, NetValidateName), for scripts/test_sysrun_server.sh
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*   join-probe validate NAME TYPE
 *   join-probe join SERVER|- DOMAIN OU|- ACCOUNT|- PASSWORD|- FLAGS
 *   join-probe unjoin ACCOUNT|- PASSWORD|- FLAGS
 *   join-probe rename NEWNAME
 * Prints "status=N" and, from wexjoin, "message=TEXT". "-" is a NULL argument. */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <lm.h>

static WCHAR *arg( WCHAR *a ) { return a[0] == '-' && !a[1] ? NULL : a; }

int wmain( int argc, WCHAR **argv )
{
    DWORD status = ERROR_INVALID_PARAMETER;
    typedef DWORD (WINAPI *message_fn)( WCHAR *, DWORD );
    HMODULE module;
    message_fn message;
    WCHAR text[256] = { 0 };

    if (argc == 4 && !wcscmp( argv[1], L"validate" ))
        status = NetValidateName( NULL, argv[2], NULL, NULL, wcstoul( argv[3], NULL, 10 ) );
    else if (argc == 8 && !wcscmp( argv[1], L"join" ))
        status = NetJoinDomain( arg( argv[2] ), arg( argv[3] ), arg( argv[4] ), arg( argv[5] ), arg( argv[6] ), wcstoul( argv[7], NULL, 0 ) );
    else if (argc == 5 && !wcscmp( argv[1], L"unjoin" ))
        status = NetUnjoinDomain( NULL, arg( argv[2] ), arg( argv[3] ), wcstoul( argv[4], NULL, 0 ) );
    else if (argc == 3 && !wcscmp( argv[1], L"rename" ))
        status = NetRenameMachineInDomain( NULL, argv[2], NULL, NULL, 0 );
    else
    {
        printf( "usage\n" );
        return 2;
    }
    printf( "status=%lu\n", status );
    if ((module = LoadLibraryW( L"wexjoin.dll" )) && (message = (message_fn)GetProcAddress( module, "WexJoinLastMessage" )))
    {
        message( text, sizeof(text) / sizeof(text[0]) );
        printf( "message=%ls\n", text );
    }
    return 0;
}
