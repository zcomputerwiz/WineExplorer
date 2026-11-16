/*
 * wexgpstore: the local Group Policy store, unix side (session 0's account)
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

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

#include "wexgpstore_unix.h"
#include "native.h"

/* The machine root is the one in the root-owned configuration of the system tier (the same file and rule as the
 * server's: not a link, owned by root, not writable by others, "machine_root=" given exactly once), never something
 * from the process's environment. */
#define SYSRUN_CONF "/etc/wineexplorer/sysrun.conf"

static int read_machine_root( char *root, size_t size )
{
    char text[8192], *line, *next;
    struct stat st;
    ssize_t length;
    int fd, found = 0;

    if ((fd = open( SYSRUN_CONF, O_RDONLY | O_NOFOLLOW | O_CLOEXEC )) < 0) return 0;
    if (fstat( fd, &st ) || !S_ISREG( st.st_mode ) || st.st_uid != 0 || (st.st_mode & 022) ||
        (length = read( fd, text, sizeof(text) - 1 )) <= 0)
    {
        close( fd );
        return 0;
    }
    close( fd );
    text[length] = 0;
    for (line = text; line; line = next)
    {
        if ((next = strchr( line, '\n' ))) *next++ = 0;
        if (strncmp( line, "machine_root=", 13 )) continue;
        if (found || line[13] != '/' || strlen( line + 13 ) >= size) return 0;
        strcpy( root, line + 13 );
        found = 1;
    }
    return found;
}

static DWORD win32_error( int result )
{
    switch (result)
    {
    case GPS_OK:           return ERROR_SUCCESS;
    case GPS_NONE:         return ERROR_FILE_NOT_FOUND;
    case GPS_REFUSED:      return ERROR_ACCESS_DENIED;
    case GPS_TOO_LARGE:    return ERROR_FILE_TOO_LARGE;
    case GPS_NO_MEMORY:    return ERROR_NOT_ENOUGH_MEMORY;
    case GPS_BAD_ARGUMENT: return ERROR_INVALID_PARAMETER;
    case GPS_BAD_TREE:     return ERROR_INVALID_DATA;
    default:               return ERROR_IO_DEVICE;
    }
}

/* the store, opened for one call: the tree is checked every time */
static int open_store( struct gps_store *store )
{
    char root[PATH_MAX];

    if (!read_machine_root( root, sizeof(root) )) return GPS_BAD_TREE;
    return gps_open( root, store );
}

static NTSTATUS gpstore_open( void *args )
{
    struct open_params *params = args;
    struct gps_store store;
    int result = open_store( &store );

    if (result == GPS_OK)
    {
        gps_clean( &store );
        gps_close( &store );
    }
    params->err = win32_error( result );
    return STATUS_SUCCESS;
}

static NTSTATUS gpstore_read( void *args )
{
    struct read_params *params = args;
    struct gps_store store;
    unsigned char *data = NULL;
    size_t size = 0;
    int result = open_store( &store );

    params->length = 0;
    if (result == GPS_OK)
    {
        result = gps_spool_read( &store, params->kind, &data, &size );
        gps_close( &store );
    }
    params->err = win32_error( result );
    if (result == GPS_OK)
    {
        params->length = size;
        if (size > params->size) params->err = ERROR_INSUFFICIENT_BUFFER;
        else if (size) memcpy( (void *)(ULONG_PTR)params->buffer, data, size );
    }
    free( data );
    return STATUS_SUCCESS;
}

static NTSTATUS gpstore_done( void *args )
{
    struct done_params *params = args;
    struct gps_store store;
    int result = open_store( &store );

    params->error[WEX_GP_ERROR_LIMIT] = 0;
    if (result == GPS_OK)
    {
        result = gps_spool_done( &store, params->kind, params->has_error ? params->error : NULL );
        gps_close( &store );
    }
    params->err = win32_error( result );
    return STATUS_SUCCESS;
}

static NTSTATUS gpstore_write( void *args )
{
    struct write_params *params = args;
    struct gps_store store;
    int result = open_store( &store );

    if (result == GPS_OK)
    {
        result = gps_store_write( &store, params->kind, (const unsigned char *)(ULONG_PTR)params->data, params->size );
        gps_close( &store );
    }
    params->err = win32_error( result );
    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    gpstore_open,
    gpstore_read,
    gpstore_done,
    gpstore_write,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count );

#ifdef _WIN64
/* The parameters use fixed-width scalars and ULONG64 pointer slots, so a 32-bit caller uses the same entry points */
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    gpstore_open,
    gpstore_read,
    gpstore_done,
    gpstore_write,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_wow64_funcs) == unix_funcs_count );
#endif
