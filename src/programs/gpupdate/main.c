/*
 * gpupdate: refresh Group Policy
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

/*
 * docs/GROUP_POLICY.md, "gpupdate". XP's command line:
 *
 *   gpupdate [/target:{computer|user}] [/force] [/wait:value] [/logoff] [/boot] [/sync]
 *
 * The refresh is asked of the Group Policy service (gpsvc, session 0) with a user-defined control, which the
 * service manager lets every authenticated user send. The service hands it to the host (wex-gpupdate.service),
 * which fetches the domain's policy through Samba and has session 0 apply it, machine and users in one run; at
 * its end the run raises the Refreshed number, which this program waits for. /force is what every run does
 * (samba-gpupdate --force). Nothing here needs a restart or a logoff, so /logoff, /boot and /sync change nothing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winsvc.h>

#define GPSVC_CONTROL_REFRESH  128
#define SERIAL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Group Policy\\WineExplorer Applied"

static void out( const WCHAR *text )
{
    DWORD done;
    HANDLE handle = GetStdHandle( STD_OUTPUT_HANDLE );

    /* a console gets the text as it is; a pipe or file gets it in the console's code page */
    if (!WriteConsoleW( handle, text, wcslen( text ), &done, NULL ))
    {
        int len = WideCharToMultiByte( GetConsoleOutputCP(), 0, text, -1, NULL, 0, NULL, NULL );
        char *buf = malloc( len );
        if (!buf) return;
        WideCharToMultiByte( GetConsoleOutputCP(), 0, text, -1, buf, len, NULL, NULL );
        WriteFile( handle, buf, len - 1, &done, NULL );
        free( buf );
    }
}

static void usage(void)
{
    out( L"Refreshes local and Active Directory-based Group Policy settings, including security settings.\n\n"
         L"GPUPDATE [/Target:{Computer | User}] [/Force] [/Wait:<value>] [/Logoff] [/Boot] [/Sync]\n\n"
         L"Parameters:\n"
         L"    /Target:{Computer | User}  Specifies that only Computer or User policy settings are refreshed.\n"
         L"                               By default, both are refreshed.\n"
         L"    /Force                     Reapplies all policy settings.\n"
         L"    /Wait:{value}              Seconds to wait for the refresh to finish (default 600; 0: do not\n"
         L"                               wait; -1: wait indefinitely).\n"
         L"    /Logoff                    Logs off after the refresh if a setting needs it.\n"
         L"    /Boot                      Restarts after the refresh if a setting needs it.\n"
         L"    /Sync                      Applies the next policy at startup or logon synchronously.\n" );
}

static DWORD refreshed(void)
{
    DWORD serial = 0, size = sizeof(serial);
    HKEY key;

    if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, SERIAL_KEY, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key )) return 0;
    if (RegQueryValueExW( key, L"Refreshed", NULL, NULL, (BYTE *)&serial, &size )) serial = 0;
    RegCloseKey( key );
    return serial;
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    BOOL computer = TRUE, user = TRUE;
    LONG wait = 600;
    DWORD before, start, err = ERROR_SUCCESS;
    SC_HANDLE manager, service;
    SERVICE_STATUS status;
    WCHAR *end;
    int i;

    for (i = 1; i < argc; i++)
    {
        const WCHAR *arg = argv[i];

        if (arg[0] != '/' && arg[0] != '-') { usage(); return 1; }
        arg++;
        if (!_wcsicmp( arg, L"target:computer" )) user = FALSE;
        else if (!_wcsicmp( arg, L"target:user" )) computer = FALSE;
        else if (!_wcsnicmp( arg, L"wait:", 5 ))
        {
            wait = wcstol( arg + 5, &end, 10 );
            if (!arg[5] || *end || wait < -1) { usage(); return 1; }
        }
        else if (!_wcsicmp( arg, L"force" ) || !_wcsicmp( arg, L"logoff" ) || !_wcsicmp( arg, L"boot" ) ||
                 !_wcsicmp( arg, L"sync" )) continue;
        else { usage(); return 1; }
    }

    out( L"Refreshing Policy...\n\n" );
    before = refreshed();
    if (!(manager = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT ))) err = GetLastError();
    else
    {
        if (!(service = OpenServiceW( manager, L"gpsvc", SERVICE_USER_DEFINED_CONTROL ))) err = GetLastError();
        else
        {
            if (!ControlService( service, GPSVC_CONTROL_REFRESH, &status )) err = GetLastError();
            CloseServiceHandle( service );
        }
        CloseServiceHandle( manager );
    }
    if (err)
    {
        WCHAR text[512], msg[400];
        if (!FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, msg,
                             ARRAY_SIZE(msg), NULL )) swprintf( msg, ARRAY_SIZE(msg), L"%lu\n", err );
        swprintf( text, ARRAY_SIZE(text), L"Failed to refresh policy. Error - %s", msg );
        out( text );
        return 1;
    }

    /* the run of the host ends with a new Refreshed number */
    start = GetTickCount();
    while (wait && refreshed() == before)
    {
        if (wait > 0 && GetTickCount() - start >= (DWORD)wait * 1000)
        {
            if (computer) out( L"Computer Policy update has not completed in the expected time. Exiting...\n" );
            if (user) out( L"User Policy update has not completed in the expected time. Exiting...\n" );
            return 0;
        }
        Sleep( 250 );
    }
    if (!wait) return 0;
    /* what Windows does when a policy refresh ends: the programs of the desktop (the shell among them) are told to
     * read their restrictions again */
    SendMessageTimeoutW( HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Policy", SMTO_ABORTIFHUNG, 2000, NULL );
    if (user) out( L"User Policy Refresh has completed.\n" );
    if (computer) out( L"Computer Policy Refresh has completed.\n" );
    return 0;
}
