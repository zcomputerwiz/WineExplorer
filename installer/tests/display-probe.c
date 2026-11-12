/*
 * Display modes as Display Properties (desk.cpl) sees and sets them, through wexhost's WexDisplayGet and
 * WexDisplaySet: the compositor's output in a Wayland session, XRandR on X11. installer/tests/safe-mode.sh runs it
 * in the logged-on session (session-run.py). Built with MinGW as a 32-bit and a 64-bit program:
 *   i686-w64-mingw32-gcc -O2 -o display-probe32.exe display-probe.c
 *   x86_64-w64-mingw32-gcc -O2 -o display-probe64.exe display-probe.c
 * Usage:
 *   display-probe.exe                    one line: bits= err= output= current=WxH@mHz modes= has=<the modes>
 *   display-probe.exe set W H [persist]  the mode of that size nearest 60 Hz; then the line above for the result
 *
 * Copyright 2026 WineExplorer contributors
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

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* src/include/wexhost.h */
#define MAX_MODES 128
typedef struct { DWORD width, height, refresh; } MODE;
typedef struct { char output[64]; MODE current; DWORD count; MODE modes[MAX_MODES]; } DISPLAY;
typedef DWORD (WINAPI *GET)(DISPLAY *);
typedef DWORD (WINAPI *SET)(const MODE *, BOOL);

static void report( GET get, DWORD set_result, BOOL set_done )
{
    static DISPLAY display;
    DWORD err = get( &display ), i;

    printf( "bits=%d", (int)(sizeof(void *) * 8) );
    if (set_done) printf( " set=%lu", (unsigned long)set_result );
    printf( " err=%lu output=%s current=%lux%lu@%lu modes=%lu has=", (unsigned long)err,
            display.output[0] ? display.output : "none", (unsigned long)display.current.width,
            (unsigned long)display.current.height, (unsigned long)display.current.refresh,
            (unsigned long)display.count );
    for (i = 0; !err && i < display.count; i++)
        printf( "%s%lux%lu", i ? "," : "", (unsigned long)display.modes[i].width,
                (unsigned long)display.modes[i].height );
    printf( "\n" );
}

int main( int argc, char **argv )
{
    HMODULE wexhost = LoadLibraryA( "wexhost.dll" );
    GET get = wexhost ? (GET)GetProcAddress( wexhost, "WexDisplayGet" ) : NULL;
    SET set = wexhost ? (SET)GetProcAddress( wexhost, "WexDisplaySet" ) : NULL;
    static DISPLAY display;
    MODE *best = NULL;
    DWORD width, height, i, result;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (!get || !set)
    {
        printf( "bits=%d err=wexhost\n", (int)(sizeof(void *) * 8) );
        return 1;
    }
    if (argc < 4 || strcmp( argv[1], "set" ))
    {
        report( get, 0, FALSE );
        return 0;
    }
    width = strtoul( argv[2], NULL, 10 );
    height = strtoul( argv[3], NULL, 10 );
    if (get( &display )) { report( get, 0, FALSE ); return 1; }
    for (i = 0; i < display.count; i++)
    {
        MODE *mode = &display.modes[i];
        if (mode->width != width || mode->height != height) continue;
        if (!best || labs( (long)mode->refresh - 60000 ) < labs( (long)best->refresh - 60000 )) best = mode;
    }
    result = best ? set( best, argc > 4 && !strcmp( argv[4], "persist" ) ) : ERROR_INVALID_PARAMETER;
    report( get, result, TRUE );
    return result ? 1 : 0;
}
