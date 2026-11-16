/*
 * wexgpstore: the calls from the PE side to the unix side
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

#ifndef __WEXGPSTORE_UNIX_H
#define __WEXGPSTORE_UNIX_H

#include <stdarg.h>

#include <windef.h>
#include <winternl.h>

#include "wine/unixlib.h"
#include "wexgpstore.h"

/* Every call returns STATUS_SUCCESS; the Win32 outcome is in params->err. The 64-bit pointers come first so that the
 * structures have the same layout for a 32-bit caller. */

struct open_params { DWORD err; };

struct read_params { ULONG64 buffer; DWORD size; DWORD kind; DWORD length; DWORD err; };

struct done_params { DWORD kind; DWORD has_error; char error[WEX_GP_ERROR_LIMIT + 1]; DWORD err; };

struct write_params { ULONG64 data; DWORD size; DWORD kind; DWORD err; };

enum wexgpstore_funcs
{
    unix_gpstore_open,
    unix_gpstore_read,
    unix_gpstore_done,
    unix_gpstore_write,
    unix_funcs_count
};

#define WEXGPSTORE_CALL( func, params ) WINE_UNIX_CALL( unix_ ## func, params )

#endif
