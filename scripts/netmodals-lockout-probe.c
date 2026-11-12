/*
 * WineExplorer Desktop Environment - probe for the account lockout policy (NetUserModals level 3)
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

/* Usage:
 *   probe get                                prints "get status=0 threshold=N duration=SECONDS window=SECONDS"
 *   probe set THRESHOLD DURATION WINDOW      seconds (DURATION "forever" is TIMEQ_FOREVER); prints "set status=N"
 * Build (both): i686-w64-mingw32-gcc / x86_64-w64-mingw32-gcc -municode -o probe.exe netmodals-lockout-probe.c -lnetapi32 */

#include <windows.h>
#include <lm.h>
#include <stdio.h>

int __cdecl wmain( int argc, WCHAR **argv )
{
    USER_MODALS_INFO_3 *info = NULL, set;
    NET_API_STATUS status;

    if (argc == 5 && !lstrcmpW( argv[1], L"set" ))
    {
        set.usrmod3_lockout_threshold = wcstoul( argv[2], NULL, 10 );
        set.usrmod3_lockout_duration = !lstrcmpW( argv[3], L"forever" ) ? TIMEQ_FOREVER : wcstoul( argv[3], NULL, 10 );
        set.usrmod3_lockout_observation_window = wcstoul( argv[4], NULL, 10 );
        status = NetUserModalsSet( NULL, 3, (BYTE *)&set, NULL );
        printf( "set status=%lu\n", (unsigned long)status );
        return status != 0;
    }
    status = NetUserModalsGet( NULL, 3, (BYTE **)&info );
    if (status || !info)
    {
        printf( "get status=%lu\n", (unsigned long)status );
        return 1;
    }
    printf( "get status=0 threshold=%lu duration=%lu window=%lu\n", (unsigned long)info->usrmod3_lockout_threshold,
            (unsigned long)info->usrmod3_lockout_duration, (unsigned long)info->usrmod3_lockout_observation_window );
    NetApiBufferFree( info );
    return 0;
}
