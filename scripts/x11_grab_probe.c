/*
 * Native X11 client for the secure desktop acceptance (docs/ELEVATION_SECURE_DESKTOP_WORKORDER.md, phase 3).
 * It acts as a program outside Wine on the same display:
 *   paint            map a white full-screen window and stay (a known screen content for the dimming check; kill it)
 *   grab             try to grab the keyboard and the pointer; prints the result of each, releases and exits
 *   pixel <x> <y>    prints the colour of a screen pixel as seen on the display (R G B, 0 to 255)
 *   windows          prints "<width>x<height> override-redirect=<0|1> mapped=<0|1>" for each child of the root
 * Build: gcc -O2 -Wall -o x11_grab_probe x11_grab_probe.c -lX11
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *grab_name( int r )
{
    switch (r)
    {
    case GrabSuccess: return "GrabSuccess";
    case AlreadyGrabbed: return "AlreadyGrabbed";
    case GrabNotViewable: return "GrabNotViewable";
    case GrabFrozen: return "GrabFrozen";
    default: return "other";
    }
}

int main( int argc, char **argv )
{
    Display *d = XOpenDisplay( NULL );
    Window root, *kids = NULL, w;
    unsigned int n, i;

    if (!d || argc < 2) return 2;
    root = DefaultRootWindow( d );
    if (!strcmp( argv[1], "paint" ))
    {
        XSetWindowAttributes a = { .override_redirect = True, .background_pixel = WhitePixel( d, DefaultScreen( d ) ) };
        XEvent ev;

        w = XCreateWindow( d, root, 0, 0, DisplayWidth( d, DefaultScreen( d ) ), DisplayHeight( d, DefaultScreen( d ) ), 0,
                           CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel, &a );
        XMapRaised( d, w );
        XSync( d, False );
        for (;;) XNextEvent( d, &ev );
    }
    else if (!strcmp( argv[1], "grab" ))
    {
        w = XCreateSimpleWindow( d, root, 0, 0, 10, 10, 0, 0, 0 );
        XMapWindow( d, w );
        XSync( d, False );
        printf( "keyboard=%s\n", grab_name( XGrabKeyboard( d, w, True, GrabModeAsync, GrabModeAsync, CurrentTime ) ) );
        printf( "pointer=%s\n", grab_name( XGrabPointer( d, w, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime ) ) );
        XUngrabKeyboard( d, CurrentTime );
        XUngrabPointer( d, CurrentTime );
    }
    else if (!strcmp( argv[1], "pixel" ) && argc >= 4)
    {
        XImage *im = XGetImage( d, root, atoi( argv[2] ), atoi( argv[3] ), 1, 1, AllPlanes, ZPixmap );
        unsigned long p;
        if (!im) return 1;
        p = XGetPixel( im, 0, 0 );
        printf( "%lu %lu %lu\n", (p & im->red_mask) >> 16, (p & im->green_mask) >> 8, p & im->blue_mask );
        XDestroyImage( im );
    }
    else if (!strcmp( argv[1], "windows" ))
    {
        Window r, p;
        if (XQueryTree( d, root, &r, &p, &kids, &n ))
            for (i = 0; i < n; i++)
            {
                XWindowAttributes a;
                if (XGetWindowAttributes( d, kids[i], &a ))
                    printf( "%dx%d override-redirect=%d mapped=%d\n", a.width, a.height, a.override_redirect, a.map_state == IsViewable );
            }
        if (kids) XFree( kids );
    }
    XCloseDisplay( d );
    return 0;
}
