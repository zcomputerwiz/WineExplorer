/*
 * WineExplorer Desktop Environment - Host command helpers
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Run Linux programs (nmcli, wpctl, ...) with an argv vector.  Never goes through a shell, so
 * values such as SSIDs cannot inject commands.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_EXPLORER_HOSTCMD_H
#define __WINE_EXPLORER_HOSTCMD_H

#include <windows.h>

/* Start argv (NULL terminated; argv[0] may be a bare program name).  With out_read the child's stdout is
 * returned as a readable pipe, otherwise it is discarded.  Returns the process handle or NULL. */
HANDLE hostcmd_spawn(const char *const *argv, HANDLE *out_read);

/* Fire and forget. */
void hostcmd_run_async(const char *const *argv);

/* Run to completion (or timeout) and collect stdout (NUL terminated). */
BOOL hostcmd_capture(const char *const *argv, char *buf, DWORD size, DWORD timeout_ms);

#endif
