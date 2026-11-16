/*
 * The client side of the local Group Policy store: hand a file to gpsvc (secedit, the policy editor)
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
 * docs/GROUP_POLICY.md, "Writing the local files: the spool". A process of the system group (an administrator's
 * elevated process) leaves the file in GroupPolicy\Spool as NAME, sends gpsvc the control
 * code 129 and waits for the refresh that gpsvc asks for, as gpupdate does. gpsvc, which is the store's only writer,
 * checks the file and installs it, or removes it and leaves NAME.error. Header only; include it once.
 */

#ifndef __WEXGPCLIENT_H
#define __WEXGPCLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winsvc.h>

#include "wexgpstore.h"

#define WEX_GP_SERIAL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Group Policy\\WineExplorer Applied"

static const WCHAR * const wex_gp_request_names[WEX_GP_KIND_COUNT] =
    { L"Machine.Registry.pol", L"User.Registry.pol", L"Machine.GptTmpl.inf" };

static DWORD wex_gp_refreshed(void)
{
    DWORD serial = 0, size = sizeof(serial);
    HKEY key;

    if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, WEX_GP_SERIAL_KEY, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key )) return 0;
    if (RegQueryValueExW( key, L"Refreshed", NULL, NULL, (BYTE *)&serial, &size )) serial = 0;
    RegCloseKey( key );
    return serial;
}

/* the request's text, written as NAME.tmp and renamed to NAME, so that gpsvc never sees a half-written request;
 * NAME.error of an earlier run is read by the caller, not here. (Until 2026-11-16 the file was written to NAME itself:
 * a rename in the spool by an elevated process failed, B28, a name lookup in the server without the system group; fixed
 * in the server, tested by c4_elevated_process_renames_in_a_group_only_directory.) */
static DWORD wex_gp_leave_request( const WCHAR *dir, const WCHAR *name, const void *data, DWORD size )
{
    WCHAR temp[MAX_PATH + 32], final[MAX_PATH + 32];
    HANDLE file;
    DWORD done, err = ERROR_SUCCESS;

    swprintf( temp, ARRAY_SIZE(temp), L"%s\\%s.tmp", dir, name );
    swprintf( final, ARRAY_SIZE(final), L"%s\\%s", dir, name );
    file = CreateFileW( temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    if (size && (!WriteFile( file, data, size, &done, NULL ) || done != size)) err = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    if (!CloseHandle( file ) && !err) err = GetLastError();
    if (!err && !MoveFileExW( temp, final, MOVEFILE_REPLACE_EXISTING )) err = GetLastError();
    if (err) DeleteFileW( temp );
    return err;
}

/* Waits until the refresh serial is not the one the caller saw before it asked (before), for wait_seconds (-1: without
 * end, 0: does not wait); TRUE when it changed. Event driven: a registry change notification on the Group Policy key,
 * which reaches every session through session 0 (docs/GROUP_POLICY.md, "Step 3: refresh signalling"); the event is
 * armed before the serial is looked at again, so that a change in between is not missed. With pump, the thread's
 * messages are dispatched meanwhile (a window that waits stays alive). If the key cannot be opened, the serial is
 * looked at every quarter of a second: the one place left that polls, for a machine where the service never ran. */
#define WEX_GP_WATCH_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Group Policy"

static BOOL wex_gp_wait_refreshed( DWORD before, LONG wait_seconds, BOOL pump )
{
    HKEY key = NULL;
    HANDLE event = CreateEventW( NULL, FALSE, FALSE, NULL );
    ULONGLONG start = GetTickCount64(), limit = wait_seconds < 0 ? ~0ull : (ULONGLONG)wait_seconds * 1000;
    BOOL ok = FALSE;

    if (!wait_seconds) { if (event) CloseHandle( event ); return TRUE; }
    if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, WEX_GP_WATCH_KEY, 0, KEY_NOTIFY | KEY_WOW64_64KEY, &key )) key = NULL;
    for (;;)
    {
        ULONGLONG elapsed = GetTickCount64() - start;
        DWORD timeout, wait;

        if (key && event) RegNotifyChangeKeyValue( key, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, event, TRUE );
        if (wex_gp_refreshed() != before) { ok = TRUE; break; }
        if (elapsed >= limit) break;
        timeout = limit == ~0ull ? INFINITE : (DWORD)(limit - elapsed > 0xfffffffeu ? 0xfffffffeu : limit - elapsed);
        if (!key || !event) { Sleep( 250 ); continue; }
        if (pump)
        {
            MSG msg;

            wait = MsgWaitForMultipleObjects( 1, &event, FALSE, timeout, QS_ALLINPUT );
            if (wait == WAIT_OBJECT_0 + 1)
            {
                while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
                /* the event was not what woke it: it stays armed, the serial is looked at again and the wait goes on */
                continue;
            }
        }
        else wait = WaitForSingleObject( event, timeout );
        if (wait == WAIT_TIMEOUT && wex_gp_refreshed() == before) break;
    }
    if (key) RegCloseKey( key );
    if (event) CloseHandle( event );
    return ok;
}

/* what gpsvc left in NAME.error (at most 200 bytes of ANSI text), removed; empty when there is none */
static void wex_gp_take_error( const WCHAR *dir, const WCHAR *name, WCHAR *message, DWORD chars )
{
    WCHAR path[MAX_PATH + 40];
    char text[WEX_GP_ERROR_LIMIT + 1];
    DWORD done = 0;
    HANDLE file;

    if (!chars) return;
    message[0] = 0;
    swprintf( path, ARRAY_SIZE(path), L"%s\\%s.error", dir, name );
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return;
    if (ReadFile( file, text, WEX_GP_ERROR_LIMIT, &done, NULL )) text[done] = 0; else text[0] = 0;
    CloseHandle( file );
    DeleteFileW( path );
    MultiByteToWideChar( CP_ACP, 0, text, -1, message, chars );
    message[chars - 1] = 0;
}

/* Hands the file of a kind (WEX_GP_MACHINE_POL, ...) to gpsvc; size 0 asks to remove it. wait_seconds is how long to
 * wait for the refresh (0: do not; -1: without end); pump dispatches the thread's messages while it waits (a window). ERROR_SUCCESS when gpsvc installed it and the refresh ended;
 * ERROR_ACCESS_DENIED when this process is not of the system group (not elevated); ERROR_INVALID_DATA with the reason
 * in message when gpsvc refused the file; ERROR_TIMEOUT when the refresh did not end, or the request was not taken.
 * (ControlService does not hand the service handler's error code back, so a request that gpsvc refused, for a tree that
 * is not as setup makes it for one, shows up as the timeout; the request stays in the spool.) */
static DWORD wex_gp_store_write( DWORD kind, const void *data, DWORD size, LONG wait_seconds, WCHAR *message, DWORD chars,
                                  BOOL pump )
{
    WCHAR dir[MAX_PATH], path[MAX_PATH + 40];
    SC_HANDLE manager, service;
    SERVICE_STATUS status;
    DWORD before, err;

    message[0] = 0;
    if (kind >= WEX_GP_KIND_COUNT || (size && !data)) return ERROR_INVALID_PARAMETER;
    if (size > (kind == WEX_GP_MACHINE_TEMPLATE ? WEX_GP_TEMPLATE_LIMIT : WEX_GP_REQUEST_LIMIT)) return ERROR_FILE_TOO_LARGE;
    if (!GetSystemDirectoryW( dir, MAX_PATH - 20 )) return GetLastError();
    wcscat( dir, L"\\GroupPolicy\\Spool" );
    if ((err = wex_gp_leave_request( dir, wex_gp_request_names[kind], data, size ))) return err;

    before = wex_gp_refreshed();
    err = ERROR_SERVICE_DOES_NOT_EXIST;
    if ((manager = OpenSCManagerW( NULL, NULL, SC_MANAGER_CONNECT )))
    {
        if ((service = OpenServiceW( manager, L"gpsvc", SERVICE_USER_DEFINED_CONTROL )))
        {
            err = ControlService( service, GPSVC_CONTROL_STORE, &status ) ? ERROR_SUCCESS : GetLastError();
            CloseServiceHandle( service );
        }
        else err = GetLastError();
        CloseServiceHandle( manager );
    }
    if (err)
    {
        swprintf( path, ARRAY_SIZE(path), L"%s\\%s", dir, wex_gp_request_names[kind] );
        DeleteFileW( path );   /* not taken: it must not wait there for the next time */
        return err;
    }
    /* gpsvc removes a request it took; one that is still there was not */
    swprintf( path, ARRAY_SIZE(path), L"%s\\%s", dir, wex_gp_request_names[kind] );
    if (GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES)
    {
        DeleteFileW( path );
        return ERROR_TIMEOUT;
    }
    wex_gp_take_error( dir, wex_gp_request_names[kind], message, chars );
    if (message[0]) return ERROR_INVALID_DATA;
    /* the refresh that gpsvc asked for ends with a new serial */
    return wex_gp_wait_refreshed( before, wait_seconds, pump ) ? ERROR_SUCCESS : ERROR_TIMEOUT;
}

#endif
