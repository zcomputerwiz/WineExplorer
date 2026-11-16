/*
 * wexgpstore: the local Group Policy store, PE side
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

/* docs/GROUP_POLICY.md, "Writing the local files: the spool": thin calls into the unix side, which does the file work
 * as session 0's account. */

#include <stdarg.h>
#include <string.h>

#include <windef.h>
#include <winbase.h>

#include "wexgpstore_unix.h"

static BOOL unix_ready;

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls( inst );
        unix_ready = !__wine_init_unix_call();
    }
    return TRUE;
}

DWORD WINAPI WexGpStoreOpen(void)
{
    struct open_params params = {0};

    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    WEXGPSTORE_CALL( gpstore_open, &params );
    return params.err;
}

DWORD WINAPI WexGpSpoolRead( DWORD kind, void *buffer, DWORD size, DWORD *length )
{
    struct read_params params = {0};

    if (length) *length = 0;
    if (kind >= WEX_GP_KIND_COUNT || (size && !buffer) || !length) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.buffer = (ULONG_PTR)buffer;
    params.size = size;
    params.kind = kind;
    WEXGPSTORE_CALL( gpstore_read, &params );
    *length = params.length;
    return params.err;
}

DWORD WINAPI WexGpSpoolDone( DWORD kind, const char *error )
{
    struct done_params params = {0};

    if (kind >= WEX_GP_KIND_COUNT) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.kind = kind;
    if (error)
    {
        params.has_error = TRUE;
        strncpy( params.error, error, WEX_GP_ERROR_LIMIT );   /* the unix side stops at its limit too */
        params.error[WEX_GP_ERROR_LIMIT] = 0;
    }
    WEXGPSTORE_CALL( gpstore_done, &params );
    return params.err;
}

DWORD WINAPI WexGpStoreWrite( DWORD kind, const void *data, DWORD size )
{
    struct write_params params = {0};

    if (kind >= WEX_GP_KIND_COUNT || (size && !data)) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.data = (ULONG_PTR)data;
    params.size = size;
    params.kind = kind;
    WEXGPSTORE_CALL( gpstore_write, &params );
    return params.err;
}
