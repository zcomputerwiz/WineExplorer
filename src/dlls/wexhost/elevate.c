/*
 * WineExplorer host services - the elevation client for stock callers (docs/ELEVATION_CALLERS_WORKORDER.md, E1)
 *
 * shell32 and msi load wexhost.dll only while elevation is enforced and call WexElevateRun, which sends one
 * request to the elevation broker (wexelevate.h). Keeping the client here means stock Wine needs no copy of it.
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "wexelevate.h"
#include "wexhost.h"

DWORD WINAPI WexElevateRun( const WCHAR *image, const WCHAR *cmdline, const WCHAR *cwd, const WCHAR *env,
                            DWORD env_len, DWORD show, DWORD flags, HANDLE *process, DWORD *pid )
{
    if (!image) return ERROR_INVALID_PARAMETER;
    return wexelevate_run( image, cmdline, cwd, env, env_len, show, flags, process, pid );
}
