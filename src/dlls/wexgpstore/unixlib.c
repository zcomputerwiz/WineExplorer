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
#include "winerror.h"
#include "winternl.h"

#include "wexgpstore_unix.h"
#include "native.h"
#include "wexgpstore.h"
#include "wine/wex_sysrun_conf.h"

/* the limits are written twice (the PE side's header and the native code's): they have to agree */
typedef char limits_agree[(WEX_GP_REQUEST_LIMIT == GPS_REQUEST_LIMIT && WEX_GP_TEMPLATE_LIMIT == GPS_TEMPLATE_LIMIT &&
                           WEX_GP_ERROR_LIMIT == GPS_ERROR_LIMIT && WEX_GP_TARGET_MAX == GPS_TARGET_MAX &&
                           WEX_GP_KIND_COUNT == GPS_KIND_COUNT) ? 1 : -1];

/* the target of a call: NULL when none was given; the text is zero-terminated here, whatever the caller sent */
static const char *call_target( char *target, DWORD given )
{
    target[WEX_GP_TARGET_MAX - 1] = 0;
    return given ? target : NULL;
}

/* The machine root is the one in the root-owned configuration of the system tier (the same file and rule as the
 * server's: not a link, owned by root, not writable by others, "machine_root=" given exactly once), never something
 * from the process's environment. */
#define SYSRUN_CONF "/etc/wineexplorer/sysrun.conf"

static int read_machine_root( char *root, size_t size )
{
    struct wex_sysrun_conf conf;

    return wex_sysrun_conf_load( SYSRUN_CONF, &conf ) &&
           wex_sysrun_conf_get( &conf, "machine_root", root, size ) == WEX_SYSRUN_CONF_VALUE && root[0] == '/';
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
        result = gps_spool_read( &store, params->kind, call_target( params->target, params->target_given ), &data, &size );
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
        result = gps_spool_done( &store, params->kind, call_target( params->target, params->target_given ), params->has_error ? params->error : NULL );
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
        result = gps_store_write( &store, params->kind, call_target( params->target, params->target_given ), (const unsigned char *)(ULONG_PTR)params->data, params->size );
        gps_close( &store );
    }
    params->err = win32_error( result );
    return STATUS_SUCCESS;
}

static NTSTATUS gpstore_targets( void *args )
{
    struct targets_params *params = args;
    struct gps_store store;
    int result = open_store( &store );

    params->count = 0;
    if (result == GPS_OK)
    {
        int count = gps_spool_targets( &store, (char (*)[GPS_TARGET_MAX])(ULONG_PTR)params->targets, params->max );

        if (count < 0) result = GPS_IO_ERROR;
        else params->count = count;
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
    gpstore_targets,
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
    gpstore_targets,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_wow64_funcs) == unix_funcs_count );
#endif
