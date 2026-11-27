/*
 * WineExplorer elevation: the caller class of a pipe client, as session 0's server decides it (E1-S18e R9, lab only)
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
 * A unit of its own: the server request (wine/wex_caller.h) needs wine/server.h, whose protocol header defines a
 * tag "request" that the broker's own struct request (broker_internal.h) collides with. broker0.c calls
 * r9b_caller_class() and sees only the class constants. In the Wine tree (__WINESRC__, the lab build) it asks the
 * server; the plain MinGW builds of scripts/test_broker_e1s18.sh are compile checks, never run, and get no class.
 */

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)   /* first: a shipping build compiles nothing of this unit */

#include <windows.h>

#ifdef __WINESRC__
#include "wine/wex_caller.h"

unsigned int r9b_caller_class( HANDLE pipe )
{
    return wex_caller_class_of_pipe( pipe );
}
#else
#define WEX_CALLER_CONSTANTS_ONLY
#include "wine/wex_caller.h"

unsigned int r9b_caller_class( HANDLE pipe )
{
    (void)pipe;
    return WEX_CALLER_NONE;
}
#endif

#else
typedef int wexelevate_caller_class_unused;   /* ISO C: a translation unit is not empty */
#endif /* _WIN32 && WEXELEVATE_E1S18 */
