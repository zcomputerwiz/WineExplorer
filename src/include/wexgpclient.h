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

/* a number that gpsvc keeps in the serial key: Refreshed (counts the finished refreshes), Stored (counts the passes over
 * the spool, raised when one ends) and StoreStatus (the error that the last pass could not leave in a NAME.error) */
static DWORD wex_gp_serial( const WCHAR *name )
{
    DWORD serial = 0, size = sizeof(serial);
    HKEY key;

    if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, WEX_GP_SERIAL_KEY, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key )) return 0;
    if (RegQueryValueExW( key, name, NULL, NULL, (BYTE *)&serial, &size )) serial = 0;
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

/* the wait is over when the number of that name is not `ref` (at_least FALSE), or has reached it (at_least TRUE; the
 * numbers count up and may wrap) */
static BOOL wex_gp_reached( const WCHAR *name, DWORD ref, BOOL at_least )
{
    DWORD now = wex_gp_serial( name );

    return at_least ? (LONG)(now - ref) >= 0 : now != ref;
}

static BOOL wex_gp_wait_cond( const WCHAR *name, DWORD before, BOOL at_least, LONG wait_seconds, BOOL pump )
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
        if (wex_gp_reached( name, before, at_least )) { ok = TRUE; break; }
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
        if (wait == WAIT_TIMEOUT && !wex_gp_reached( name, before, at_least )) break;
    }
    if (key) RegCloseKey( key );
    if (event) CloseHandle( event );
    return ok;
}

static BOOL wex_gp_wait_serial( const WCHAR *name, DWORD before, LONG wait_seconds, BOOL pump )
{
    return wex_gp_wait_cond( name, before, FALSE, wait_seconds, pump );
}

/* Waits for the refresh that serves the request just made: gpsvc numbers the requests (Requested), a run of the host
 * tells which it served (Served, set before Refreshed is raised). A run that began before the request, and ends after
 * it, served an older number and does not count. Call it after the request has been made (gpupdate: after
 * ControlService; the store: after the pass that asked for it ended). TRUE when the run has ended. */
static BOOL wex_gp_wait_served( LONG wait_seconds, BOOL pump )
{
    return wex_gp_wait_cond( L"Served", wex_gp_serial( L"Requested" ), TRUE, wait_seconds, pump );
}

/* what is left of a wait of wait_seconds (-1: without end) that began at start (GetTickCount64): -1, or 0 when it is over */
static LONG wex_gp_left( LONG wait_seconds, ULONGLONG start )
{
    ULONGLONG used = (GetTickCount64() - start) / 1000;

    if (wait_seconds < 0) return -1;
    return used >= (ULONGLONG)wait_seconds ? 0 : wait_seconds - (LONG)used;
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
 * wait for gpsvc to take the request and then for the refresh (0: do not wait, ERROR_SUCCESS means "handed over"; -1:
 * without end); pump dispatches the thread's messages while it waits (a window). ERROR_SUCCESS when gpsvc installed it
 * and the refresh ended; ERROR_ACCESS_DENIED when this process is not of the system group (not elevated);
 * ERROR_INVALID_DATA with the reason in message when gpsvc refused the file; another error when gpsvc could not look
 * at the spool (StoreStatus); ERROR_TIMEOUT when the request was not taken, or the refresh did not end. */
static DWORD wex_gp_store_write( DWORD kind, const void *data, DWORD size, LONG wait_seconds, WCHAR *message, DWORD chars,
                                  BOOL pump )
{
    WCHAR dir[MAX_PATH], path[MAX_PATH + 40];
    SC_HANDLE manager, service;
    SERVICE_STATUS status;
    DWORD stored, err;
    ULONGLONG start;

    message[0] = 0;
    if (kind >= WEX_GP_KIND_COUNT || (size && !data)) return ERROR_INVALID_PARAMETER;
    if (size > (kind == WEX_GP_MACHINE_TEMPLATE ? WEX_GP_TEMPLATE_LIMIT : WEX_GP_REQUEST_LIMIT)) return ERROR_FILE_TOO_LARGE;
    if (!GetSystemDirectoryW( dir, MAX_PATH - 20 )) return GetLastError();
    wcscat( dir, L"\\GroupPolicy\\Spool" );
    if ((err = wex_gp_leave_request( dir, wex_gp_request_names[kind], data, size ))) return err;

    stored = wex_gp_serial( L"Stored" );
    start = GetTickCount64();
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
    /* gpsvc handles the request in its own time (the control code only wakes it, and several requests are one pass):
     * every pass ends with a new Stored number, and a request that it took is gone from the spool. A pass that ended
     * before gpsvc read this request leaves it there: the wait goes on for the next pass. */
    swprintf( path, ARRAY_SIZE(path), L"%s\\%s", dir, wex_gp_request_names[kind] );
    if (!wait_seconds) return ERROR_SUCCESS;
    for (;;)
    {
        DWORD now = wex_gp_serial( L"Stored" );
        LONG left;

        if (now != stored)
        {
            stored = now;
            if (GetFileAttributesW( path ) == INVALID_FILE_ATTRIBUTES) break;
            /* still there: refused for a reason no NAME.error can hold (the tree is not as setup makes it) */
            if ((err = wex_gp_serial( L"StoreStatus" )))
            {
                DeleteFileW( path );
                return err;
            }
        }
        left = wex_gp_left( wait_seconds, start );
        if (!left || !wex_gp_wait_serial( L"Stored", stored, left, pump ))
        {
            DeleteFileW( path );   /* not taken: it must not wait there for the next time */
            return ERROR_TIMEOUT;
        }
    }
    wex_gp_take_error( dir, wex_gp_request_names[kind], message, chars );
    if (message[0]) return ERROR_INVALID_DATA;
    /* the refresh that the pass asked for (the pass ended before Stored was raised, so its request is numbered) */
    return wex_gp_wait_served( wex_gp_left( wait_seconds, start ) ? wex_gp_left( wait_seconds, start ) : 1, pump )
           ? ERROR_SUCCESS : ERROR_TIMEOUT;
}

#endif
