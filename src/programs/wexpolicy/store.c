/*
 * wexpolicy: the local Group Policy store, the requests of the spool
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
 * docs/GROUP_POLICY.md, "Writing the local files: the spool". The service gpsvc gets the control code 129
 * (GPSVC_CONTROL_STORE) from secedit /configure and the policy editor, after their process (an administrator's,
 * elevated: the system group) left a request in the spool. For each kind of file that may be stored (the fixed list
 * of wexgpstore.h) this reads the request through wexgpstore, which does the file work as session 0's account
 * without following links, checks its content completely with the parsers the writer itself uses, installs it in
 * the store (always session 0's own file) and removes the request. A request that does not pass is removed and
 * leaves NAME.error with the reason. When any request was handled, a refresh is asked for (as gpupdate does), so
 * that root's wex-gpupdate.path applies it, and the client waits for the refresh's end.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include "wexpreg.h"
#include "wexgpstore.h"
#include "wexpolicy.h"

/* a registry policy file: the signature, the version and every entry, as "wexpolicy apply" reads it */
static DWORD check_preg( const BYTE *data, DWORD size )
{
    return preg_validate( data, size ) == PREG_END ? ERROR_SUCCESS : ERROR_BAD_FORMAT;
}

/* a security template: UTF-16LE with a byte order mark, and what "wexpolicy security" checks before it writes. The
 * setup library reads a file, so the bytes go into a temporary file of this account's own temporary directory. */
static DWORD check_template( const BYTE *data, DWORD size )
{
    WCHAR dir[MAX_PATH], path[MAX_PATH];
    HANDLE file;
    DWORD done, err = ERROR_SUCCESS;

    if (size < 4 || (size & 1) || data[0] != 0xff || data[1] != 0xfe) return ERROR_BAD_FORMAT;
    if (!GetTempPathW( ARRAY_SIZE(dir), dir ) || !GetTempFileNameW( dir, L"gps", 0, path )) return GetLastError();
    file = CreateFileW( path, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE)
    {
        err = GetLastError();
        DeleteFileW( path );
        return err;
    }
    if (!WriteFile( file, data, size, &done, NULL ) || done != size) err = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    CloseHandle( file );
    if (!err) err = check_security_template( path );
    DeleteFileW( path );
    return err;
}

static DWORD check_request( DWORD kind, const BYTE *data, DWORD size )
{
    return kind == WEX_GP_MACHINE_TEMPLATE ? check_template( data, size ) : check_preg( data, size );
}

/* the request is handled; the reason (or NULL) goes to NAME.error */
static DWORD finish_request( DWORD kind, const char *target, DWORD err, const char *why )
{
    char text[WEX_GP_ERROR_LIMIT + 1];

    if (!err) return WexGpSpoolDone( kind, target, NULL );
    else
    {
        snprintf( text, sizeof(text), "error %lu: %s", err, why );
        return WexGpSpoolDone( kind, target, text );
    }
}

static void note_stuck( DWORD *stuck, DWORD *first, DWORD err )
{
    if (!err) return;
    (*stuck)++;
    if (!*first) *first = err;
}

#define MAX_TARGETS 32

/* one request, of a kind and (for the users' objects) a target: read, checked completely, installed, and removed from
 * the spool with its NAME.error (or none) */
static void handle_request( DWORD kind, const char *target, BYTE *buffer, DWORD *handled, DWORD *stuck, DWORD *first )
{
    DWORD room = kind == WEX_GP_MACHINE_TEMPLATE ? WEX_GP_TEMPLATE_LIMIT : WEX_GP_REQUEST_LIMIT, length, err;

    err = WexGpSpoolRead( kind, target, buffer, room, &length );
    if (err == ERROR_FILE_NOT_FOUND) return;
    (*handled)++;
    if (err == ERROR_ACCESS_DENIED) { note_stuck( stuck, first, finish_request( kind, target, err, "refused: not a plain file with one link" )); return; }
    if (err == ERROR_FILE_TOO_LARGE) { note_stuck( stuck, first, finish_request( kind, target, err, "refused: too large" )); return; }
    if (err) { note_stuck( stuck, first, finish_request( kind, target, err, "the request could not be read" )); return; }
    /* zero bytes: every setting back to Not Configured, the store file goes */
    if (length && (err = check_request( kind, buffer, length )))
    {
        note_stuck( stuck, first, finish_request( kind, target, err, kind == WEX_GP_MACHINE_TEMPLATE ? "not a security template" : "not a policy file" ));
        return;
    }
    if ((err = WexGpStoreWrite( kind, target, length ? buffer : NULL, length )))
    {
        note_stuck( stuck, first, finish_request( kind, target, err, "the store could not be written" ));
        return;
    }
    note_stuck( stuck, first, finish_request( kind, target, ERROR_SUCCESS, NULL ));
}

/* stop: the service's stop event, looked at between the files (a stop does not wait for the whole pass); the result is
 * the first error that could not be left in a NAME.error (the request stays in the spool), else ERROR_SUCCESS */
DWORD process_spool( HANDLE stop )
{
    BYTE *buffer;
    DWORD kind, err, round, handled = 0, stuck = 0, first = ERROR_SUCCESS;   /* stuck: requests that could not be taken out of the spool */
    BOOL stopped = FALSE;

    /* the tree has to be as setup makes it, else nothing is read or written (and stale STORE.new files go) */
    if ((err = WexGpStoreOpen())) return err;
    if (!(buffer = HeapAlloc( GetProcessHeap(), 0, WEX_GP_REQUEST_LIMIT ))) return ERROR_NOT_ENOUGH_MEMORY;

    for (kind = 0; kind < WEX_GP_KIND_COUNT && !stopped; kind++)
    {
        /* the kind that has a target is handled below, for each target that has a request */
        if (kind == WEX_GP_USER_TARGET_POL) continue;
        if (stop && !WaitForSingleObject( stop, 0 )) { stopped = TRUE; break; }
        handle_request( kind, NULL, buffer, &handled, &stuck, &first );
    }
    /* the local objects of the users: the requests that name a target (a few rounds at most: the ones that were handled
     * are gone from the spool, so the next list has the rest) */
    for (round = 0; round < 4 && !stopped; round++)
    {
        char targets[MAX_TARGETS][WEX_GP_TARGET_MAX];
        DWORD count = 0, i;

        if (WexGpSpoolTargets( targets, MAX_TARGETS, &count ) || !count) break;
        for (i = 0; i < count; i++)
        {
            if (stop && !WaitForSingleObject( stop, 0 )) { stopped = TRUE; break; }
            handle_request( WEX_GP_USER_TARGET_POL, targets[i], buffer, &handled, &stuck, &first );
        }
        if (count < MAX_TARGETS) break;
    }
    HeapFree( GetProcessHeap(), 0, buffer );
    /* a refresh also when only errors were reported: the client waits for the refresh's end, and then reads them */
    /* (a request that stays in the spool is read again by every control code: it must not make a refresh each time) */
    if (handled > stuck) request_refresh();
    return first;
}
