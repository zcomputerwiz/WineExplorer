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

/* the target of a call: it goes with the kind that has one, and only with that one */
static BOOL copy_target( DWORD kind, const char *target, DWORD *given, char *to )
{
    size_t length;

    *given = 0;
    if ((kind == WEX_GP_USER_TARGET_POL) != (target != NULL)) return FALSE;
    if (!target) return TRUE;
    if (!wex_gp_target_valid( target )) return FALSE;
    length = strlen( target );
    memcpy( to, target, length + 1 );
    *given = 1;
    return TRUE;
}

DWORD WINAPI WexGpSpoolTargets( char (*targets)[WEX_GP_TARGET_MAX], DWORD max, DWORD *count )
{
    struct targets_params params = {0};

    if (count) *count = 0;
    if (!count || !targets || !max) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.targets = (ULONG_PTR)targets;
    params.max = max;
    WEXGPSTORE_CALL( gpstore_targets, &params );
    *count = params.count;
    return params.err;
}

DWORD WINAPI WexGpSpoolRead( DWORD kind, const char *target, void *buffer, DWORD size, DWORD *length )
{
    struct read_params params = {0};

    if (length) *length = 0;
    if (kind >= WEX_GP_KIND_COUNT || (size && !buffer) || !length) return ERROR_INVALID_PARAMETER;
    if (!copy_target( kind, target, &params.target_given, params.target )) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.buffer = (ULONG_PTR)buffer;
    params.size = size;
    params.kind = kind;
    WEXGPSTORE_CALL( gpstore_read, &params );
    *length = params.length;
    return params.err;
}

DWORD WINAPI WexGpSpoolDone( DWORD kind, const char *target, const char *error )
{
    struct done_params params = {0};

    if (kind >= WEX_GP_KIND_COUNT) return ERROR_INVALID_PARAMETER;
    if (!copy_target( kind, target, &params.target_given, params.target )) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.kind = kind;
    if (error)
    {
        size_t length = strlen( error );

        if (length > WEX_GP_ERROR_LIMIT) length = WEX_GP_ERROR_LIMIT;   /* the unix side stops at its limit too */
        params.has_error = TRUE;
        memcpy( params.error, error, length );
        params.error[length] = 0;
    }
    WEXGPSTORE_CALL( gpstore_done, &params );
    return params.err;
}

DWORD WINAPI WexGpStoreWrite( DWORD kind, const char *target, const void *data, DWORD size )
{
    struct write_params params = {0};

    if (kind >= WEX_GP_KIND_COUNT || (size && !data)) return ERROR_INVALID_PARAMETER;
    if (!copy_target( kind, target, &params.target_given, params.target )) return ERROR_INVALID_PARAMETER;
    if (!unix_ready) return ERROR_NOT_SUPPORTED;
    params.data = (ULONG_PTR)data;
    params.size = size;
    params.kind = kind;
    WEXGPSTORE_CALL( gpstore_write, &params );
    return params.err;
}
