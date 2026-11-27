/*
 * wex-vpointer: drives a virtual pointer on a wlroots compositor (zwlr_virtual_pointer_v1), for headless tests
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

/* wex-vpointer WIDTH HEIGHT COMMAND...: on WAYLAND_DISPLAY, with the output's size in logical pixels, runs
 *   move X Y     absolute motion to X,Y
 *   down / up    left button press / release
 *   sleep MS     wait
 * and prints each command with a realtime timestamp (seconds.milliseconds) when it has been flushed. Build:
 *   wayland-scanner client-header wlr-virtual-pointer-unstable-v1.xml vp.h
 *   wayland-scanner private-code wlr-virtual-pointer-unstable-v1.xml vp.c
 *   cc -o wex-vpointer wex-vpointer.c vp.c -lwayland-client   (with vp.h in the include path) */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "vp.h"

static struct zwlr_virtual_pointer_manager_v1 *manager;
static struct wl_seat *seat;

static void global_add( void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version )
{
    if (!strcmp( interface, zwlr_virtual_pointer_manager_v1_interface.name ))
        manager = wl_registry_bind( registry, name, &zwlr_virtual_pointer_manager_v1_interface, 1 );
    else if (!strcmp( interface, wl_seat_interface.name ) && !seat)
        seat = wl_registry_bind( registry, name, &wl_seat_interface, 1 );
}

static void global_remove( void *data, struct wl_registry *registry, uint32_t name )
{
}

static const struct wl_registry_listener registry_listener = { global_add, global_remove };

static uint32_t now_ms( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void stamp( const char *what, int a, int b )
{
    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );
    printf( "%lld.%03ld %s %d %d\n", (long long)ts.tv_sec, ts.tv_nsec / 1000000, what, a, b );
    fflush( stdout );
}

int main( int argc, char **argv )
{
    struct wl_display *display;
    struct zwlr_virtual_pointer_v1 *pointer;
    int width, height, i;

    if (argc < 3) { fprintf( stderr, "usage: wex-vpointer WIDTH HEIGHT COMMAND...\n" ); return 2; }
    width = atoi( argv[1] );
    height = atoi( argv[2] );
    if (!(display = wl_display_connect( NULL ))) { fprintf( stderr, "no Wayland display\n" ); return 1; }
    wl_registry_add_listener( wl_display_get_registry( display ), &registry_listener, NULL );
    wl_display_roundtrip( display );
    if (!manager) { fprintf( stderr, "no zwlr_virtual_pointer_manager_v1\n" ); return 1; }
    pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer( manager, seat );

    for (i = 3; i < argc; i++)
    {
        if (!strcmp( argv[i], "move" ) && i + 2 < argc)
        {
            int x = atoi( argv[i + 1] ), y = atoi( argv[i + 2] );
            zwlr_virtual_pointer_v1_motion_absolute( pointer, now_ms(), x, y, width, height );
            zwlr_virtual_pointer_v1_frame( pointer );
            wl_display_flush( display );
            stamp( "move", x, y );
            i += 2;
        }
        else if (!strcmp( argv[i], "down" ) || !strcmp( argv[i], "up" ))
        {
            int down = !strcmp( argv[i], "down" );
            zwlr_virtual_pointer_v1_button( pointer, now_ms(), BTN_LEFT,
                                            down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED );
            zwlr_virtual_pointer_v1_frame( pointer );
            wl_display_flush( display );
            stamp( argv[i], 0, 0 );
        }
        else if (!strcmp( argv[i], "sleep" ) && i + 1 < argc)
        {
            usleep( atoi( argv[i + 1] ) * 1000 );
            wl_display_roundtrip( display );
            i += 1;
        }
    }
    wl_display_roundtrip( display );
    zwlr_virtual_pointer_v1_destroy( pointer );
    wl_display_disconnect( display );
    return 0;
}
