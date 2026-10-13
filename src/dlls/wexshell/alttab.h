/*
 * WineExplorer Desktop Environment - Alt+Tab task switcher
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
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

#ifndef __WINE_EXPLORER_ALTTAB_H
#define __WINE_EXPLORER_ALTTAB_H

#include <windows.h>

/* A key seen by the shell's low-level keyboard hook; TRUE when the switcher takes it (the hook drops it then). */
BOOL alttab_key( DWORD vk, BOOL down );

#endif
