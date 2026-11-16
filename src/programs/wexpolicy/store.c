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

#include "preg.h"
#include "wexgpstore.h"
#include "wexpolicy.h"

/* a registry policy file: the signature, the version and every entry, as "wexpolicy apply" reads it */
static DWORD check_preg( const BYTE *data, DWORD size )
{
    struct preg_entry e;
    size_t off;
    int ret;

    if (preg_begin( data, size, &off ) != PREG_OK) return ERROR_BAD_FORMAT;
    while ((ret = preg_next( data, size, &off, &e )) == PREG_OK) {}
    return ret == PREG_END ? ERROR_SUCCESS : ERROR_BAD_FORMAT;
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
static DWORD finish_request( DWORD kind, DWORD err, const char *why )
{
    char text[WEX_GP_ERROR_LIMIT + 1];

    if (!err) return WexGpSpoolDone( kind, NULL );
    else
    {
        snprintf( text, sizeof(text), "error %lu: %s", err, why );
        return WexGpSpoolDone( kind, text );
    }
}

DWORD process_spool(void)
{
    BYTE *buffer;
    DWORD kind, err, length, handled = 0, stuck = 0;   /* stuck: requests that could not be taken out of the spool */

    /* the tree has to be as setup makes it, else nothing is read or written (and stale STORE.new files go) */
    if ((err = WexGpStoreOpen())) return err;
    if (!(buffer = HeapAlloc( GetProcessHeap(), 0, WEX_GP_REQUEST_LIMIT ))) return ERROR_NOT_ENOUGH_MEMORY;

    for (kind = 0; kind < WEX_GP_KIND_COUNT; kind++)
    {
        DWORD room = kind == WEX_GP_MACHINE_TEMPLATE ? WEX_GP_TEMPLATE_LIMIT : WEX_GP_REQUEST_LIMIT;

        err = WexGpSpoolRead( kind, buffer, room, &length );
        if (err == ERROR_FILE_NOT_FOUND) continue;
        handled++;
        if (err == ERROR_ACCESS_DENIED) { stuck += !!finish_request( kind, err, "refused: not a plain file with one link" ); continue; }
        if (err == ERROR_FILE_TOO_LARGE) { stuck += !!finish_request( kind, err, "refused: too large" ); continue; }
        if (err) { stuck += !!finish_request( kind, err, "the request could not be read" ); continue; }
        /* zero bytes: every setting back to Not Configured, the store file goes */
        if (length && (err = check_request( kind, buffer, length )))
        {
            stuck += !!finish_request( kind, err, kind == WEX_GP_MACHINE_TEMPLATE ? "not a security template" : "not a policy file" );
            continue;
        }
        if ((err = WexGpStoreWrite( kind, length ? buffer : NULL, length )))
        {
            stuck += !!finish_request( kind, err, "the store could not be written" );
            continue;
        }
        stuck += !!finish_request( kind, ERROR_SUCCESS, NULL );
    }
    HeapFree( GetProcessHeap(), 0, buffer );
    /* a refresh also when only errors were reported: the client waits for the refresh's end, and then reads them */
    /* (a request that stays in the spool is read again by every control code: it must not make a refresh each time) */
    if (handled > stuck) request_refresh();
    return ERROR_SUCCESS;
}
