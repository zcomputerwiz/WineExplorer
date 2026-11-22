/*
 * Console output helper: writes Unicode directly to console or falls back to console code page
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

#ifndef __WEXCONSOLE_H
#define __WEXCONSOLE_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static inline void wex_console_write_to( HANDLE handle, const WCHAR *text )
{
    DWORD done;

    if (!text) return;

    /* a console gets the text as it is; a pipe or file gets it in the console's code page */
    if (!WriteConsoleW( handle, text, wcslen( text ), &done, NULL ))
    {
        int len = WideCharToMultiByte( GetConsoleOutputCP(), 0, text, -1, NULL, 0, NULL, NULL );
        char *buf = malloc( len );
        if (!buf) return;
        WideCharToMultiByte( GetConsoleOutputCP(), 0, text, -1, buf, len, NULL, NULL );
        WriteFile( handle, buf, len - 1, &done, NULL );
        free( buf );
    }
}

static inline void wex_console_write( const WCHAR *text )
{
    wex_console_write_to( GetStdHandle( STD_OUTPUT_HANDLE ), text );
}

/* formats into a local buffer of at most 2048 WCHARs; no call in gpresult or netdom formats more than this */
static inline void wex_console_printf( const WCHAR *format, ... )
{
    WCHAR text[2048];
    va_list args;

    if (!format) return;
    va_start( args, format );
    vswprintf( text, ARRAY_SIZE(text), format, args );
    va_end( args );
    text[ARRAY_SIZE(text) - 1] = 0;
    wex_console_write( text );
}

#endif /* __WEXCONSOLE_H */
