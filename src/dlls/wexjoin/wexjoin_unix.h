/*
 * wexjoin: the call from the PE side to the unix side
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

#ifndef __WEXJOIN_UNIX_H
#define __WEXJOIN_UNIX_H

#include <stdarg.h>

#include <windef.h>
#include <winternl.h>

#include "wine/unixlib.h"

/* The call returns STATUS_SUCCESS; the Win32 outcome of reaching the engine is in err (0: the answer is in reply, and the
 * engine's own status is in it). The 64-bit pointers come first so that the structure has the same layout for a 32-bit
 * caller. */
/* query: a question, sent to the users' socket (WEX_JOIN_QUERY_SOCKET) */
struct request_params { ULONG64 request; ULONG64 reply; DWORD request_length; DWORD reply_size; DWORD reply_length; DWORD err;
                        DWORD query; };

enum wexjoin_funcs
{
    unix_join_request,
    unix_funcs_count
};

#define WEXJOIN_CALL( func, params ) WINE_UNIX_CALL( unix_ ## func, params )

#endif
